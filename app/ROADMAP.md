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
| MathJax `\(...\)`, `\[...\]` | ⬜ | See section 3 and the JavaScript evaluation |
| Template JavaScript | ⬜ | See the JavaScript evaluation |
| CSS: flexbox, grid, gradients, web fonts (`@font-face` from media) | 🔶 | litehtml covers block/inline/tables; flexbox partial; to audit against popular shared-deck styles |
| Night mode as AnkiDroid applies it | 🔶 | `.nightMode` classes set; colour inversion of card CSS not done |
| RTL scripts, ruby (furigana `{{furigana:...}}`) | 🔶 | Furigana renders through rslib as `<ruby>`; litehtml's ruby layout to check |

## 1. Study and review

| Feature | Status | Notes |
| --- | --- | --- |
| Deck list: hierarchy, expand/collapse, New/Learn/Due colours | ✅ | Verified in preview |
| Review flow: show answer, Again/Hard/Good/Easy with intervals | ✅ | Verified; Anki's own scheduler (FSRS/SM-2 as configured) |
| Controller mapping (A, ZL/L/R/ZR, X hints, −, B, Y) | ✅ | |
| Touch: tap to flip, tap buttons/links, drag to scroll; stick-click zoom | ✅ | |
| Hint fields (`{{hint:...}}`) | ✅ | Native, no JavaScript: tap the link or press X |
| Custom key bindings | ⬜ | Settings screen; map actions (show answer, ease 1–4, undo, replay, hints) to buttons; store in `/switch/ankinx/settings.json`; the reviewer already registers actions in one place (`ReviewerActivity::onContentAvailable`) |
| Deck options (new cards/day, max reviews/day, steps) | ⬜ | Backend: `Collection::get_deck_config`/`update_deck_configs` exposed through new JSON commands; UI: borealis list of number/selector cells |
| Custom study, tag filters | ⬜ | Backend: `custom_study` and filtered decks (`add_or_update_filtered_deck`) with search strings such as `tag:verbs is:due`; UI: dialog like AnkiDroid's custom study menu |

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
| Images PNG/JPEG/GIF/BMP | ✅ | NanoVG/stb_image |
| WebP | ✅ | libwebp (devkitPro `switch-libwebp`); verified in preview |
| SVG incl. Anki's LaTeX SVGs | ✅ | lunasvg (as in Switchfin). Chosen over nanosvg because dvisvgm output draws glyphs with `<use>`, which nanosvg doesn't support; verified in preview |
| Audio MP3/OGG/Opus/WAV | ✅ | SDL2_mixer (built with mpg123, vorbisidec, opusfile, modplug) |
| Audio M4A/AAC, ALAC, FLAC, WebM/MP4 audio | ✅ | FFmpeg fallback (decoders and demuxers confirmed in the Switch binary); needs an on-console listen |
| TTS tags (`{{tts ...}}`) | ⬜ | No TTS engine on Horizon; options: skip (current) or an offline engine such as eSpeak NG |
| MathJax (`\(...\)`, `\[...\]`) | ⬜ | See the JavaScript evaluation: native math renderer recommended |
| Card JavaScript in general | ⬜ | See below |
| Font weights | 🔶 | Synthetic bold/italic; real weights would need bundled fonts (licence + size) |

## 4. System integration and safety

| Feature | Status | Notes |
| --- | --- | --- |
| Clean close + backup on exit, including HOME | ✅ | `.colpkg` written by our code after closing (Horizon can't reopen a file that is open for writing, which Anki's `export_colpkg` does); restorable with Anki's importer (host test) |
| Rust thread-locals beyond libnx's TLS slots | ✅ | Found on device: std aborted "out of TLS keys" when loading a collection and syncing; keys now come from the shim (`nx_posix.c`) |
| Applet-mode warning | ✅ | |
| Horizon filesystem semantics | ✅ | `stat()` on open files and `rename()` over existing files fixed in the shim (found on device: `SQLITE_IOERR_FSTAT` 1802) |

## JavaScript evaluation (QuickJS / Duktape + litehtml)

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
- **Recommended order:** (1) hint fields natively ✅; (2) MathJax via MicroTeX;
  (3) QuickJS + minimal DOM, behind a per-note-type setting so a broken script
  can't break studying.
