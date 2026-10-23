# AnkiNX

Anki for the Nintendo Switch (homebrew). A native app, not an Android
wrapper, built on Anki's own Rust backend, so scheduling, card rendering and
AnkiWeb sync behave as in Anki desktop. The interface follows AnkiDroid.

Unofficial: not affiliated with Ankitects, AnkiDroid or Nintendo.

| | |
| --- | --- |
| ![Deck list](docs/screenshots/decks.png) | ![Deck overview, dark theme](docs/screenshots/overview.png) |
| ![Cloze card](docs/screenshots/cloze_question.png) | ![Card with an image](docs/screenshots/image_answer.png) |
| ![Maths typeset from LaTeX](docs/screenshots/maths.png) | ![Persian and Arabic, right to left](docs/screenshots/rtl.png) |
| ![Image Occlusion question](docs/screenshots/occlusion_question.png) | ![Image Occlusion answer](docs/screenshots/occlusion_answer.png) |

## Scope: reviewing on the go

AnkiNX is for **studying**. What it does:

- **Decks:** the deck list with today's counts, a time estimate, what you
  studied today and your study streak; a deck overview with Study now,
  custom study, deck options (limits, steps, FSRS or SM-2) and statistics;
  a read-only card browser.
- **Reviews:** Anki's scheduler and answer buttons, undo and redo, the card
  menu (flag, mark, bury, suspend, set due date, card info), timer and auto
  advance, leech handling.
- **Cards as Anki shows them:** HTML and CSS, images, audio, maths
  (`\(...\)` and `\[...\]`), JavaScript in templates, furigana, right-to-left
  text, fonts from the media folder, type-in-the-answer and Image Occlusion.
- **Controls:** buttons (rebindable, one Joy-Con held sideways works too),
  touch, swipes, rumble, and a whiteboard in handheld mode.
- **Languages:** English, Portuguese (Brazil), Spanish and Japanese, following
  the console. Light and dark themes, following the console too; larger card
  text on a TV.
- **Keeping it safe and current:** backups you can restore on the console,
  Anki's Check Database, and updates from GitHub releases inside the app.

Adding and editing notes and cards is **not implemented**. Create and maintain
your collection in Anki desktop or AnkiDroid, and use AnkiNX to review it.

**With AnkiWeb (recommended).** Log in under Settings (− button) > Account &
sync, and sync with X. A self-hosted sync server can be set there too.
Automatic sync is on by default: when the app opens, after studying and before
exiting with +. Your reviews then reach Anki desktop and AnkiDroid, and new
cards made there reach the Switch.

**Offline.** Without AnkiWeb, copy the collection to the SD card:

1. Close Anki desktop, so all changes are written to the collection file.
2. From your Anki profile folder, copy `collection.anki2` and the
   `collection.media` folder to `/switch/ankinx/` on the SD card, replacing
   what is there. The profile folder is `Anki2/<profile name>` inside
   `%APPDATA%` on Windows, `~/Library/Application Support` on macOS and
   `~/.local/share` on Linux.
3. Start AnkiNX.

To bring your Switch reviews back to the computer, copy `collection.anki2`
back the same way, with Anki closed. This replaces the computer's copy, so
don't study on both sides between copies.

**When something goes wrong.** The app writes a log to
`/switch/ankinx/ankinx.log` on the SD card; the previous run's is
`ankinx.old.log` (after a crash, that is the one to look at). Crash reports
from Atmosphère are in `/atmosphere/crash_reports/`.

## Layout

| Path | What |
| --- | --- |
| `app/` | The Switch app: borealis UI, litehtml card renderer, audio. See [app/README.md](app/README.md) and [app/ROADMAP.md](app/ROADMAP.md) |
| `switch/` | Anki's Rust backend cross-compiled for Horizon: the `anki_switch_core` crate (JSON API, SQLite VFS, POSIX shim), Rust std/libc patches, target spec. See [switch/README.md](switch/README.md) |
| `tools/` | Environment setup, source sync into WSL, dependency fetchers, icon tool |
| `VERSION`, `CHANGELOG.md` | Project version and release notes |
| `test-deck/` | A test deck with Anki's card types and template features, each card saying what to check |

## Building

Linux or WSL2 with devkitPro (devkitA64, libnx, switch-* portlibs), a pinned
Rust nightly and Python 3.13. `tools/provision_phase1.sh` sets this up. Then:

```sh
bash tools/fetch_frontend_deps.sh   # borealis, litehtml, lunasvg at pinned commits
bash tools/fetch_ftl.sh             # Anki translations
bash switch/tools/vendor.sh apply   # patched crates from switch/patches/
bash switch/build_core.sh           # Rust backend -> switch/dist/libanki_switch_core.a
bash app/build.sh                   # -> app/dist/AnkiNX.nro
```

Anki's source (26.09.3) is expected in `source_code/anki-26.09.3`, unmodified.
`bash switch/test_core_host.sh` runs the backend tests on Linux, including
under an emulation of the Switch's filesystem rules.

## Licence

AnkiNX is free software under the GNU Affero General Public License, version
3 or later (AGPL-3.0-or-later), the same licence as Anki, whose backend it
includes. See [LICENSE](LICENSE). If you distribute the `.nro`, you must also
offer its source code.

Third-party code and assets, under their own licences:

| Component | Licence |
| --- | --- |
| Anki (`source_code/`, rslib) | AGPL-3.0-or-later, with BSD-3 portions |
| borealis (UI framework) | Apache-2.0 |
| litehtml (HTML/CSS layout; our fixes in `app/patches/litehtml`) | BSD-3-Clause |
| gumbo (HTML parser, bundled with litehtml) | Apache-2.0 |
| QuickJS-ng (card JavaScript) | MIT |
| Card page runtime (`app/resources/script/runtime.js`, built from `app/js`; licence texts in `runtime-licenses.txt` next to it): linkedom, uhyphen, boolbase | ISC |
| … htmlparser2, dom-serializer, cssom, html-escaper, jQuery | MIT |
| … css-select, css-what, domhandler, domutils, domelementtype, entities, nth-check | BSD-2-Clause |
| lunasvg (SVG) | MIT |
| DejaVu Sans font (`app/resources/card_fonts`; right-to-left text) | Bitstream Vera licence; DejaVu changes public domain |
| Material Icons font (`app/resources/material`) | Apache-2.0 |
| Patched crates (`switch/patches/`): libc, getrandom | MIT OR Apache-2.0 |
| Patched crates: mio, tokio | MIT |
| Patched crate: ring | Apache-2.0 AND ISC |
| Patched crate: stacker | MIT OR Apache-2.0 |
| Typst (maths layout), MiTeX (LaTeX to Typst; definitions in `switch/anki_switch_core/assets/mitex`) | Apache-2.0 |
| New Computer Modern fonts (`switch/anki_switch_core/assets/fonts`) | GUST Font License (maths); GPL-3.0+ with font exception (text) |

The Switch system fonts used by the desktop preview are Nintendo's. They are
not part of this repository: `tools/fetch_frontend_deps.sh` copies them from
borealis. The app logo depicts Joy-Con-style controllers. AnkiNX is not
affiliated with or endorsed by Nintendo.

## Installing

Copy `AnkiNX.nro` to `/switch/` on the SD card and start it from the
Homebrew Menu. For full memory, open the Homebrew Menu by holding R while
starting a game rather than from the Album. Data lives in `/switch/ankinx/`.
