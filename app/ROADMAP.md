# AnkiNX roadmap (toward AnkiDroid parity)

Status: ✅ done · 🔶 partial · ⬜ planned. "Verified" means checked in the desktop
preview (`app/preview.sh`) and/or host tests; on-console checks are noted.

## Priority: card display parity

**Cards must look and behave as they do in Anki desktop and AnkiDroid. This is
one of the most important features of the app.** People study shared decks
whose templates were written for those apps. A card that shows raw template
syntax (such as `[[type:Back]]`, seen on hardware), loses formatting, or drops
interaction gets in the way of studying, whatever else works. Rules:

- Anki's own code decides what a card shows. Rendering, answer comparison and
  cloze extraction come from rslib; the app only replaces the parts that need
  a web view, following `qt/aqt/reviewer.py` and AnkiDroid's reviewer.
- Every template feature Anki supports gets a native equivalent or a planned
  entry below, never silent raw output.
- Test with real shared decks as well as the sample collection: the
  Japanese, language and medical decks most people download.

| Template feature | Status | Notes |
| --- | --- | --- |
| Fields, conditionals, `{{FrontSide}}`, cloze | ✅ | Rendered by rslib |
| `{{hint:...}}` | ✅ | Native links, X reveals all |
| `{{type:...}}`, `{{type:cloze:...}}`, `{{type:nc:...}}` | ✅ | Answer box opens the Switch keyboard (A or tap); the answer side shows Anki's `compare_answer` diff, as in desktop. Host-tested |
| Image Occlusion (stock note type, Anki 23.10+) | ✅ | Masks drawn natively over the image (rect, ellipse, polygon, text) following `review.ts`; "occlude inactive"; X toggles masks on the answer. Verified in preview and host tests; needs an on-console check |
| `[sound:...]`, replay buttons | ✅ | |
| `{{tts ...}}` | ⬜ | See section 3 |
| MathJax `\(...\)`, `\[...\]` | ✅ | Native: LaTeX → Typst (MiTeX) → SVG, cached in `/switch/ankinx/cache/math`, sized in `em` and on the text baseline (`switch/anki_switch_core/src/math.rs`). ~25 ms per new formula on PC; adds ~25 MB to the `.nro`. Verified on console. Chemistry (`\ce`, `\pu`, from MathJax's mhchem) turned into plain LaTeX first (`mhchem.rs`): formulas, charges, states, arrows with conditions, hydrates, gas/precipitate arrows; host-tested, console check pending |
| Template JavaScript | ✅ | A browser page for QuickJS-ng (`app/js`, bundled as `resources/script/runtime.js`): linkedom's DOM with DOM-Standard events (capture/bubble, `on…` attributes), form controls and activation behaviour, `<script>` execution as in browsers (inline, `src` and modules from the media folder, `document.write`), the real jQuery 3.7.1, `fetch`/`XMLHttpRequest` for media files, `getComputedStyle` and element geometry from litehtml's layout, and Anki desktop's reviewer page (`_showQuestion`/`_showAnswer`, `onUpdateHook`/`onShownHook`, `pycmd`). Globals kept from question to answer; the next card's page is prepared while the answer is read. 500 ms / 64 MB budget per run; off switch in Settings > Study. Host-tested (`app/tests/script_test.cpp`, incl. the shared MCQ note type); verified in the desktop preview. `<canvas>` 2D drawing is recorded as SVG and drawn as an image (paths, curves, gradients, text, images, transforms; no pixel read-back); tapping a template's text field opens the keyboard, and the text arrives with input, change and Enter. Not covered: CSS animation, network. Template buttons, links and form controls: tap, or highlight with the D-pad (Left/Right, then Up/Down) and press A; B removes the highlight. Verified on console |
| CSS: flexbox, grid, gradients, web fonts (`@font-face` from media) | ✅ | litehtml covers block/inline/tables, flexbox, `vh`/`vw`, `:checked`, `::before`/`::after`; inline `<svg>` and `data:` images drawn; card fonts from media via `@font-face` (TTF/OTF, picked by weight and style; WOFF not readable, the next `src` is tried; system fonts fill missing glyphs; files over 24 MB skipped). `box-shadow` (outer; blur approximated) and 2D `transform` (translate, rotate, scale, skew, matrix; `transform-origin`) through litehtml patch 0003; absolutely positioned elements inside a relatively positioned inline (badges) through patch 0004. CSS grid through patch 0005 (columns from `grid-template-columns` with lengths, `fr`, `repeat()` incl. auto-fill/auto-fit and `minmax()`; spans, gaps, alignment). Missing: `grid-template-rows`/`-areas` and `grid-row`, inset shadows, 3D transforms, `transition`, `filter`, `zoom`. To audit against popular shared-deck styles |
| Night mode as AnkiDroid applies it | ✅ | `.nightMode` / `.night_mode` classes and Anki's night colours, as Anki and AnkiDroid do (neither recolours the card's own colours). One kindness beyond them: text left in night mode's light colour on a light background the card paints (a highlight, a code block) is drawn dark, so it stays readable |
| RTL scripts (Persian, Arabic, Urdu, Hebrew) | ✅ | Arabic-script letters joined via presentation forms (incl. Persian letters, lam-alef, ZWNJ), right-to-left runs reversed per word (`app/src/view/rtl_text.cpp`) and per line (litehtml patch: simplified bidi; `dir="rtl"` makes lines read right to left and right-aligns left-aligned text). Fallback font DejaVu Sans (the Switch fonts have no Arabic). Not covered: CSS `direction`, harakat placement beyond the font's defaults, complex Urdu Nastaliq. Verified in preview |
| Thai, Hindi (Devanagari) | 🔶 | Bundled Noto Sans Thai / Devanagari as fallback fonts; Devanagari's vowel sign i drawn before its consonant (`rtl_text.cpp`). No shaping: Devanagari conjuncts show as consonant + virama, Thai stacked marks use the font's default positions (HarfBuzz would fix both) |
| Ruby (furigana `{{furigana:...}}`) | ✅ | litehtml has no ruby layout: each base/reading pair is rewritten as an inline block (`app/src/util/ruby.cpp`), reading above, base on the baseline; `<rp>` dropped; deck CSS on `rt` still applies. Verified in preview |

## 1. Study and review

| Feature | Status | Notes |
| --- | --- | --- |
| Deck list: hierarchy, expand/collapse, New/Learn/Due colours | ✅ | Verified in preview |
| Review flow: show answer, Again/Hard/Good/Easy with intervals | ✅ | Verified; Anki's own scheduler (FSRS/SM-2 as configured) |
| Controller mapping (A, ZL/L/R/ZR, X hints, −, B, Y) | ✅ | |
| Touch: tap to flip, tap buttons/links, drag to scroll; stick-click zoom | ✅ | |
| Hint fields (`{{hint:...}}`) | ✅ | Native, no JavaScript: tap the link or press X |
| Custom key bindings | ✅ | See the backlog: Settings > Controls |
| Deck options (new cards/day, max reviews/day, steps) | ✅ | Y on the deck overview: limits, steps, intervals, lapses and leeches, burying, audio, maximum interval, FSRS (collection-wide) with desired retention, or SM-2 ease and multipliers; through Anki's `get_deck_configs_for_update` / `update_deck_configs`. Not covered: preset management (add, rename, switch), display order, timer, FSRS parameter optimisation |
| Custom study, tag filters | ✅ | X on the deck overview: Anki's choices (raise today's limits, forgotten, ahead, preview, by card state with one tag), studied at once in "Custom Study Session". Not covered: excluding tags, several tags |

## 2. Sync and account

| Feature | Status | Notes |
| --- | --- | --- |
| AnkiWeb normal sync, full upload/download conflict choice | ✅ | Verified on console (0.2.0) |
| Automatic sync: on open, after studying, before + exits | ✅ | Like Anki desktop / AnkiDroid's option; toggle in the account menu (−). Closing from HOME can't sync (no time), hence the sync after studying. Needs an on-console check |
| Login with the Switch keyboard | ✅ | Only the host key is stored (`sync.json`) |
| Background media sync with completion notice | ✅ | |

## 3. Media and rendering

| Feature | Status | Notes |
| --- | --- | --- |
| HTML/CSS cards via litehtml (note type CSS, night mode) | ✅ | Verified: cloze, lists, images, Japanese |
| Images PNG/JPEG/GIF/BMP | ✅ | NanoVG/stb_image; GIFs animate |
| WebP | ✅ | libwebp (devkitPro `switch-libwebp`); verified in preview |
| SVG incl. Anki's LaTeX SVGs | ✅ | lunasvg (as in Switchfin). Chosen over nanosvg because dvisvgm output draws glyphs with `<use>`, which nanosvg doesn't support; verified in preview |
| Audio MP3/OGG/Opus/WAV | ✅ | SDL2_mixer (built with mpg123, vorbisidec, opusfile, modplug) |
| Audio M4A/AAC, ALAC, FLAC, WebM/MP4 audio | ✅ | FFmpeg fallback (decoders and demuxers confirmed in the Switch binary); needs an on-console listen |
| TTS tags (`{{tts ...}}`) | ⬜ | No TTS engine on Horizon; options: skip (current) or an offline engine such as eSpeak NG |
| MathJax (`\(...\)`, `\[...\]`) | ✅ | See the parity table: Typst instead of MicroTeX |
| Card JavaScript in general | ✅ | See the parity table |
| Font weights | 🔶 | Synthetic bold/italic for the system font; a card font's own bold/italic faces (`@font-face`) are used when the deck has them |

## 4. System integration and safety

| Feature | Status | Notes |
| --- | --- | --- |
| Clean close + backup on exit, including HOME | ✅ | `.colpkg` written by our code after closing (Horizon can't reopen a file that is open for writing, which Anki's `export_colpkg` does); restorable with Anki's importer (host test) |
| Rust thread-locals beyond libnx's TLS slots | ✅ | Found on device: std aborted "out of TLS keys" when loading a collection and syncing; keys now come from the shim (`nx_posix.c`) |
| Applet-mode warning | ✅ | |
| Horizon filesystem semantics | ✅ | `stat()` on open files and `rename()` over existing files fixed in the shim (found on device: `SQLITE_IOERR_FSTAT` 1802) |

## Backlog: review and study features

Brainstormed from AnkiDroid's reviewer, Anki desktop and the Switch's own
situation (handheld/docked, touch, Joy-Cons, shared family consoles, no
microphone). Scope stays review-only: no note editing. Priority: **P1** is
expected by most users, **P2** is often requested, **P3** is nice to have.
Current order of work: maths, then single Joy-Con and key bindings, then
template JavaScript, then this list, taken in groups of related features
(those sharing screens or code) rather than by priority.

### In the reviewer

| Feature | Priority | Notes |
| --- | --- | --- |
| Single Joy-Con (sideways) and custom key bindings | ✅ | Automatic layout detection, SL/Y/A/SR grading, Settings > Controls to rebind (`util/controls.cpp`). Verified on console: both Joy-Cons detached (grip screen) or only one detached. Card menu on holding Y comes with the card actions |
| Card actions menu: flag (colours), mark, bury card/note, suspend card/note, forget, set due date | ✅ | Hold the replay button (Y; stick press on one Joy-Con) or tap ⋯ in the header: a list of actions (borealis dropdown); flag and ★ shown in the header, flag colour along the card's top. Every action is undoable. Needs an on-console check |
| Card info: review history, interval, ease or FSRS stability/difficulty, previous card's info | ✅ | From the card menu: rslib `card_stats` as a page of tables (card view); B closes |
| Leech, daily-limit and "learning card due in N min" messages | ✅ | Leech dialog after answering; the finished screen says when the next learning card is due (and resumes then), and when limits stopped cards or some were buried |
| Answer timer and auto advance (show answer / answer after N seconds) | ✅ | Timer in the header (deck option; red at the cap, frozen on the answer with "Stop timer on answer"). Auto advance started per session from the card menu, as in Anki: seconds per side, waits for audio, question and answer actions. Both set in deck options |
| Whiteboard (draw on the card with the touchscreen) | ✅ | Pen in the header (handheld mode): drawing over the card instead of scrolling; bin clears; cleared on the next card. No eraser or colours yet |
| Tap zones and swipe gestures (handheld) | 🔶 | Swipes (Settings > Study, off by default): ← Again, ↓ Hard, → Good, ↑ Easy; vertical ones only on cards that don't scroll. Tap zones not done |
| Persistent card zoom / text size per deck | ✅ | Stick-press zoom remembered per deck. No pinch zoom (borealis has no pinch gesture) |
| Audio options: autoplay on/off, replay question only, playback speed | 🔶 | "Don't play audio automatically" honoured; "Skip question when replaying answer" applied (replay on the answer side plays the question's audio first unless set); no playback speed (SDL_mixer can't change it) |
| Redo, multiple undo levels shown | ✅ | Redo in the card menu (Anki's redo stack) |
| Rumble on answer (HD Rumble) | ✅ | Settings > Study, off by default; a 60 ms pulse on the handheld or player-1 controller. Needs an on-console check |

### Around studying

| Feature | Priority | Notes |
| --- | --- | --- |
| Deck overview screen before studying (counts, description, "Study now", custom study) | ✅ | R or ⋯ on a deck row: counts, description, Study now (A), Custom study (X), Options (Y) |
| Deck options (limits, learning steps, FSRS on/off, desired retention) | ✅ | See section 1 |
| Custom study and filtered decks (study ahead, forgotten cards, by tag, increase today's limit) | ✅ | See section 1 |
| Statistics: today (cards, time), forecast, review heatmap, retention | ✅ | R on the deck overview (that deck and subdecks) or Settings > Statistics (all decks): Today, Forecast, Heatmap, Retention panels drawn natively from rslib's graph data; L/R or tap to switch |
| Multiple profiles, one per Switch user | — | Not planned (2026-10-18): one profile per console |
| Custom sync server (self-hosted Anki sync server) | ✅ | Settings > Account > Sync server; used at login (changing it logs out) |
| Read-only card browser (search, preview, suspend/flag from the list) | ✅ | L (or Browse) on the deck overview: an Anki search (starts with the deck), up to 200 cards by sort field with deck, flag and suspension; preview of the answer side with X flag and Y suspend/unsuspend. No editing |
| Deck list actions: collapse all, filter, show only decks with due cards | — | Not planned (2026-10-24) |
| Study streak and time estimate on the deck list | ✅ | Streak (days in a row with reviews) on the status line; "about N min" next to the due count, from the last 30 days' pace. Computed once per Anki day from rslib's graph data |
| Restore a backup, Check Database, back up now | ✅ | Settings > Collection. Restore unpacks the .colpkg to a checked temp file renamed into place (as full download, for Horizon), backs up the current collection first and requires a full sync, as Anki's colpkg import |
| Update check and download from GitHub releases | ✅ | On open (can be turned off) and in Settings > About; the .nro is downloaded next to the running one and swapped in at exit (after romfs is unmounted). Needs an on-console check |
| Import .apkg from the SD card | — | Not planned (2026-10-18): needs a PC and a file browser; AnkiWeb covers it |
| Audio playback speed | — | Not planned (2026-10-18) |

### Interface and platform

| Feature | Priority | Notes |
| --- | --- | --- |
| Interface translations (Portuguese, Spanish, Japanese, ...) | ✅ | Portuguese (Brazil; also used for Portugal), Spanish (also Latin America), Japanese; follows the console, or Settings > About > Language (after a restart). Anki's own strings come from rslib |
| Docked/TV layout: larger card text and buttons at 1080p | ✅ | borealis already renders at 1080p docked; card text is scaled up on a TV (Settings > Study > On a TV: 100/125/150%, default 125%), on top of the deck's zoom, switching live on dock/undock |
| In-app theme choice (light/dark/follow system) | P3 | Follows the system theme today |
| Resume after sleep: keep the current card, sync on wake if automatic sync is on | P3 | |

## JavaScript evaluation (QuickJS / Duktape + litehtml)

The hand-written DOM below has been replaced by a full
browser page in JavaScript (linkedom, jQuery, Anki's reviewer): see
`docs/full-js-plan.md` and `app/js/README.md`. The findings stay for context.

**Embedding an engine is easy; the DOM is the real work.** Findings:

- **Engine.** QuickJS (quickjs-ng) or Duktape compile for devkitA64; both are
  plain C99 and need nothing beyond what the port's newlib + shim provides.
  QuickJS adds ~1 MB, supports ES2023 and is the better fit (Duktape is ES5,
  and modern templates use `let`, arrow functions and template strings).
- **DOM.** litehtml has an element tree with attribute access and CSS selectors,
  but no live-mutation + relayout API, no events, and no scripting hooks.
  Template scripts expect `document.getElementById`/`querySelector`,
  `element.style`, `classList`, `textContent`/`innerHTML` and `onclick`/
  `addEventListener`. The workable design is a small DOM implemented on our
  side (C++ objects exposed to QuickJS), built from the card HTML. Scripts
  mutate it, it is serialised back to HTML, and litehtml re-lays it out after
  each script run or event. Cards are small, so re-layout per event is cheap.
- **Coverage this would give:** show/hide toggles, randomised content,
  text/class changes, simple click handlers: most hand-written template JS.
  Not: timers that animate, canvas, fetch/XHR, layout measurement
  (`offsetHeight`, `getBoundingClientRect`).
- **MathJax is not realistic through JS:** it needs extensive DOM, CSS and font
  measurement, ships megabytes of script, and would be slow in an interpreter
  on the Tegra X1. Recommended instead: a **native LaTeX math renderer**
  (MicroTeX, C++, with a small NanoVG drawing backend) for Anki's `\(...\)` and
  `\[...\]` delimiters, rendered inline by the card view. Cards using Anki's
  `[latex]` images already work (SVG/PNG).
- **Recommended order:** (1) hint fields natively ✅; (2) MathJax natively ✅ (Typst via MiTeX, chosen over MicroTeX: complete and already rendering SVG);
  (3) QuickJS + minimal DOM, behind a per-note-type setting so a broken script
  can't break studying.
