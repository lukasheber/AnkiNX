# Card page runtime

The JavaScript that card templates run in. QuickJS (`app/library/quickjs`)
has no browser, so this builds one for card pages: linkedom's DOM, a browser
layer on top of it, the real jQuery, and Anki's reviewer page. It is bundled
into `app/resources/script/runtime.js`, which `CardScript`
(`app/src/script/card_script.cpp`) loads into each card's QuickJS context.

The bundle is committed: building the app needs no Node. After changing
anything here, run (inside WSL):

```sh
bash app/js/build.sh      # Node from tools/js_toolchain.sh; versions in package-lock.json
bash app/test_scripts.sh  # host tests: app/tests/script_test.cpp
```

## Parts (`src/`, in load order)

| File | Role |
| --- | --- |
| `native.js` | The app's bridge (`__native`): logging, running scripts, media files, storage, commands, layout |
| `dom.js` | The page (Anki's `<div id="qa">`) and linkedom's classes on the global object; what linkedom lacks for a page: element classes for every tag, constructible `Text`/`Range`/..., `document.implementation`, cookies, `readyState`, attribute order |
| `events.js` | DOM Standard event dispatch (capture, target, bubble), listener options, event classes, event handlers (`onclick = f` and `onclick="..."`) |
| `errors.js` | Uncaught errors: logged, and an `error` event on `window` |
| `style.js` | `element.style` (CSSOM), written through the `style` attribute |
| `url.js` | `URL`, `URLSearchParams` |
| `window.js` | `navigator`, `location`, `history`, storage, `console`, timers, `matchMedia`, selection, observers, audio |
| `forms.js` | Control state (checked, values, selected), `:checked`, activation behaviour (checkbox, radio, label, submit, `<summary>`, links), focus |
| `scripts.js` | `<script>` runs when inserted (inline at once, `src` from the media folder), never from `innerHTML`; `document.currentScript`, `document.write()` |
| `net.js` | `fetch`, `XMLHttpRequest`, `Blob`, `FileReader`, `FormData`: media files only, no network |
| `canvas.js` | `<canvas>` with a 2D context that records its drawing as SVG (paths, gradients, text, images, transforms, `clearRect`); images' `naturalWidth`/`naturalHeight` from their files |
| `serialize.js` | The body as HTML for litehtml, with node serials and control state; a canvas becomes an `<img>` of its SVG |
| `layout.js` | `getComputedStyle`, `offset*`, `client*`, `getBoundingClientRect` from litehtml's layout (`CardView::measure`) |
| `jquery.js` | jQuery 3.7.1 (Anki's version) as `$`/`jQuery`, set up on first use |
| `scheduling.js` | Custom scheduling: Anki's `ts/reviewer/answering.ts` (`anki.mutateNextCardStates`), with protobuf-es 1.10's JSON reading and writing for the scheduling states |
| `reviewer.js` | Anki's `ts/reviewer/index.ts`: `_showQuestion`, `_showAnswer`, hooks, `pycmd`; the app's entry points (`__ankinx`) |

## Fixing linkedom

linkedom is a DOM library, not a browser, and a few of its choices differ
from browsers. The browser layer fixes them where they matter to pages and
says so in a comment; when updating linkedom, run the host tests and drop
fixes it no longer needs:

- event dispatch without a capture phase, listeners called on the target;
- element classes not created for many tags (`form`, `label`, `details`,
  `div`, `li`, `audio`...), audio/video not media elements;
- `:checked` and `:selected` matched on attributes, not control state
  (css-select's alias table, changed in `forms.js`);
- `element.style` writing the attribute without mutation records;
- new attributes put first; `childList` observers missing text nodes;
- `DOMParser` not putting fragments in `<body>`.

Two internals are reached by relative path, as the packages don't export
them: linkedom's node-link symbols (`dom.js`) and css-select's pseudo-class
tables (`forms.js`). Both resolve to the same modules linkedom itself uses.

## Not covered

Reading canvas pixels back (`getImageData` gives a blank image), canvas
patterns, shadows, compositing and `clip()`; CSS transitions and animations
(litehtml draws the final state); network requests; MathJax in JavaScript
(maths is typeset by the app). Typing into template text fields goes through
the app: a tap that focuses one sends a `keyboard:` command, and the app
calls `typed()` with the text. See `docs/full-js-plan.md`.
