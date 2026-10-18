# Changelog

AnkiNX follows [semantic versioning](https://semver.org/) (0.x while
features are still arriving). The version lives in `VERSION`: the `.nro`
(hbmenu shows it) and `switch/anki_switch_core/Cargo.toml` must match, and
`switch/build_core.sh` checks this.

How to release: update `VERSION` and the crate version, add a section here,
build, then commit and tag (`git tag v0.2.0`).

## Unreleased

- Maths in cards (`\(...\)` inline, `\[...\]` display, as Anki's MathJax):
  typeset natively with Typst (LaTeX converted by MiTeX), drawn as SVG on the
  text baseline, cached on the SD card. The `.nro` grows to about 76 MB.
- Footer shows "Toggle masks" for X on Image Occlusion answers.
- Controls: one Joy-Con held sideways is detected and grades with SL / Y / A /
  SR; every reviewer button can be rebound per layout.
- Settings screen on − (Account & sync, Controls, Study, About), replacing the
  account dialog.
- JavaScript in card templates (QuickJS-ng with a small DOM): scripts run on
  load, onclick and listeners on tap, timers, state kept from question to
  answer as in Anki. Can be turned off in Settings > Study.
- Card scripts written for jQuery work without it (a built-in subset of
  jQuery), as do `pycmd("ans")` / `pycmd("ease3")`, checkbox and radio taps,
  `<label>` taps and more selectors (`:nth-of-type`, `:checked`, ...).
  Tested with a shared multiple-choice note type.
- Footer: only button hints, centred (no battery, Wi-Fi or clock); X shows
  only on cards with hints (or masks to toggle); A only shows the answer
  and has no hint (the Show Answer button shows it); Y reads "Hold: card
  options" on sides without audio.
- Replay buttons work in templates with broken HTML (an unclosed `<div`
  that swallows the field's play link), as they do in Anki; and a replay
  started as another sound ended is no longer silenced.
- Card menu: hold the replay audio button (Y; the stick press on one
  Joy-Con) or tap ⋯ in the header to flag the card, mark the note, bury,
  suspend, forget, set the due date, or see the card's (or the previous
  card's) info and review history. The header shows the flag and ★ mark.
- Messages as in Anki: a leech dialog after answering; on the finished
  screen, when the next learning card is due (studying resumes then), and
  whether daily limits held cards back or some were buried.
- Card text on the Switch: Korean, Chinese-only characters and symbols now
  show in cards (the system font used there had no fallbacks).
- Card buttons without touch: the D-pad (Left/Right, then Up/Down) highlights
  what a tap could activate (template buttons and options, links such as
  audio replay, form controls); A taps it, B removes the highlight. With
  nothing highlighted, A and Up/Down work as before.
- Card fonts from the media folder (`@font-face` in the note type's CSS, as
  shared decks use for Japanese, Arabic or Persian fonts): TrueType and
  OpenType, with their bold and italic faces; missing glyphs come from the
  system font.
- Right-to-left text: Persian and Arabic letters are joined as they should
  be (they showed as separate letters, or not at all: the Switch's fonts
  have no Arabic, so DejaVu Sans is bundled as a fallback), and words and
  lines read right to left, also with `dir="rtl"`. Hebrew is reordered too.
- Furigana (`{{furigana:...}}` and `<ruby>` in cards) shows the reading
  above the text, as in Anki, instead of after it.
- Inline `<svg>` in cards, and `data:` image URIs (such as SVG icons in CSS
  backgrounds), are drawn.
- Fix: script-edited cards no longer garble characters whose UTF-8 encoding
  contains byte A0 (such as "à" or Persian letters).

## 0.2.0 (2026-10-17)

First version verified on hardware for sync and study.

- AnkiWeb login screen (email and password rows that each open the system
  keyboard), replacing two keyboards in a row, which lost the input box on
  hardware.
- Sync fixes found on hardware:
  - Full download works: collections from AnkiWeb and Anki desktop are saved
    in WAL mode, which this SQLite build rejected (error 26); the SQLite VFS
    now accepts them.
  - Horizon can't reopen a file that is open for writing: full download and
    backups no longer do that.
  - Rust thread-locals no longer run out of libnx TLS slots ("out of TLS
    keys" abort when loading a collection or syncing).
  - Clean exit: the backend shuts down before the network does (no crash
    dialog after +).
- Automatic sync when the app opens, after studying and before + exits; on
  by default, switchable in the account menu (-). Verified on hardware (open,
  after studying); exit with + only syncs when something is still unsynced.
- Type-in-the-answer cards (`{{type:...}}`): answer box with the system
  keyboard, Anki's comparison on the answer side.
- Image Occlusion cards: masks drawn natively; X toggles them on the answer
  side. Verified on hardware.
- "Missing media file" notice instead of silent audio.
- Backups (.colpkg) written after closing, restorable in Anki.
- New app icon.
- `crash.log` in the data folder records Rust panics.

## 0.1.0

First builds: deck list, review with Anki's scheduler, hints, audio, images
(PNG/JPEG/WebP/SVG), manual sync.
