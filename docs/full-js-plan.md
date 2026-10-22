# Full JavaScript in cards (experimental branch `experimental/full-js`)

## Why

The released app runs card scripts with QuickJS against a DOM written for the
port (`app/src/script/dom.cpp`, ~830 lines of C++) and a 700-line prelude that
imitates jQuery. It covers the common patterns, but every new template that
uses something else needs another hand-written piece. That is not parity with
Anki desktop or AnkiDroid, which give templates a real browser and the real
jQuery, and it grows into a maintenance burden.

This branch replaces the hand-written DOM with maintained, standards-based
JavaScript running in the same engine, and keeps the port-specific code
to the parts a browser does but a DOM library doesn't: event dispatch per the
DOM spec, form controls, script execution, layout queries and the reviewer
page itself.

No Android wrapper and no system web applet: the Switch's web applet can't
render inside the app's own view, and the litehtml renderer stays.

## Design

```
 card HTML ──► runtime.js (QuickJS) ─────────────────────► HTML with node serials ──► litehtml
              ├─ linkedom 0.18.13: parser, tree, selectors,       ▲                       │
              │  MutationObserver, Range, TreeWalker, template     │ taps, timers,         │ boxes and
              ├─ browser layer (app/js/src): events per spec,      │ keys (native)         │ computed
              │  forms and activation, script execution, timers,   │                       │ styles
              │  window/location/storage, layout queries  ◄────────┼───────────────────────┘
              ├─ jQuery 3.7.1, the version Anki bundles
              └─ reviewer.js: Anki's ts/reviewer/index.ts flow
```

* **Engine**: QuickJS-ng as today (ES2023, 500 ms / 64 MB budget per run,
  interrupt handler, off switch in Settings > Study).
* **DOM**: linkedom, pinned with npm (`app/js/package-lock.json`), bundled
  by esbuild into `app/resources/script/runtime.js`. The bundle is committed,
  so building the app needs no Node; `app/js/build.sh` regenerates it.
* **Browser layer** (`app/js/src`), each part written against the spec it
  replaces, with a test per behaviour so a linkedom upgrade shows which
  patches it made obsolete:
  * `events.js`: DOM event dispatch (capture, target, bubble; `this` is the
    current target; `once`, `capture`, `passive`; handler properties and
    `on…` attributes for elements, document and window); `UIEvent`,
    `MouseEvent`, `PointerEvent`, `KeyboardEvent`, `FocusEvent`,
    `TouchEvent`, `InputEvent`.
  * `forms.js`: `checked`/`value`/`selected` state, activation behaviour
    (checkbox, radio groups, labels, `<details>`/`<summary>`), focus and
    `document.activeElement`.
  * `scripts.js`: a `<script>` runs when inserted into the document, as in
    browsers (inline at once, `src` from the media folder as a task, then
    `load`/`error`), `document.currentScript`; scripts set with `innerHTML`
    don't run, as in browsers.
  * `window.js`: the global object (DOM classes, `location`, `navigator`,
    `screen`, storage, `console`, timers and `requestAnimationFrame`,
    `matchMedia`, `fetch` and `XMLHttpRequest` for media files,
    `document.implementation`, `DOMParser`, `alert`/`confirm`).
  * `layout.js`: `getComputedStyle`, `offset*`, `client*`, `scroll*`,
    `getBoundingClientRect`, `getClientRects` from litehtml's layout of the
    current DOM. Reading them after a change lays the card out again first
    (a forced reflow, as browsers do).
  * `serialize.js`: the body as HTML for litehtml, every element tagged with
    `data-ankinx-node` (taps, controller focus, layout answers) and
    `data-ankinx-click` (has click handlers).
  * `reviewer.js`: Anki's reviewer page (`ts/reviewer/index.ts`):
    `_showQuestion`/`_showAnswer`/`_updateQA` (side set with `innerHTML` in
    `#qa`, scripts re-created so they run), `onUpdateHook`, `onShownHook`,
    `ankiPlatform`, `pycmd`/`bridgeCommand`, `anki.imageOcclusion`,
    `_drawFlag`/`_drawMark`, browser classes, `$`/`jQuery`.
* **Native** (`app/src/script/card_script.cpp`): runtime per card, the
  bundle compiled once per app session and loaded as bytecode after that;
  a small event loop (microtasks, zero-delay tasks and script loads run
  before the side is drawn; later timers through borealis); media reads;
  commands to the reviewer; the layout bridge to `CardView`.

## Steps

1. Branch, Node toolchain for development (`tools/js_toolchain.sh`), npm
   project in `app/js` with pinned versions. ✅
2. Runtime: linkedom + browser layer + reviewer + jQuery, bundled. ✅
3. Native: new `CardScript` on the runtime (same role, simpler interface),
   bytecode cache, event loop; delete `dom.cpp`/`dom.hpp` and `prelude.js`. ✅
   (The ruby rewrite still needs an HTML tree in C++: kept as
   `util/html_tree`, without the selector engine.)
4. Reviewer integration: side shown through `_showQuestion`/`_showAnswer`,
   timers, commands, body class and styles added by scripts. ✅
   The next card's page is made while the answer is read.
5. Layout bridge: `CardView::measure()` lays out the script's HTML off
   screen and returns boxes and computed styles by serial. ✅
6. Tests: every existing case in `app/tests/script_test.cpp` (the MCQ note
   type included) plus spec behaviours, real jQuery features, dynamic
   scripts, layout answers, budgets; startup time per card measured. ✅
7. Desktop preview and Switch builds; ROADMAP and README notes ✅;
   on-console check ⬜.

## Not covered yet (after this branch)

* `<canvas>` (would need a 2D context on NanoVG), CSS transitions and
  animations (litehtml has none; final states apply at once), `transform`.
* Text fields in templates: typing through the Switch keyboard into
  `<input>`/`<textarea>` with `input`/`change` events.
* Network: `fetch` reads media files only, as AnkiWeb-synced decks expect.
* MathJax stays native (Typst); `MathJax.typesetPromise` resolves at once.

## Results so far

* Host tests (`app/tests/script_test.cpp`): every case of the old suite
  (MCQ note type included) and the spec, jQuery, script-loading, network,
  forms and layout cases pass.
* Desktop preview: the sample `Scripts` decks work (clue button, globals
  from question to answer, the MCQ note type ticking an option and flipping
  to the answer through `pycmd`), as does controller access to template
  buttons.
* Cost per card on the development laptop (i5-7200U): a page takes about
  17-26 ms to make (2 MB of bytecode compiled once per session, read in a few
  ms; the rest is setting up linkedom), a side under 1 ms, the first use of
  jQuery about 4 ms. The page for the next card is made while the answer is
  read. Expect several times that on the Tegra X1: the log's "card page
  ready in N ms" lines give the console's figure.
* The `.nro` grows by about 0.6 MB.

## To check on the console

* Decks with template JavaScript: taps, the D-pad highlight and A, timers,
  auto-flip through `pycmd`, the answer side keeping the question's globals.
* "card page ready in N ms" in the log for the first card and the next ones.
* Memory over a long session with script decks (each page: about 2.5 MB at
  first, 64 MB at most).
