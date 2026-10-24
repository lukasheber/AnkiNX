# AnkiNX: Anki for Nintendo Switch (frontend)

Native homebrew app: a [borealis](https://github.com/dragonflylee/borealis) UI
(the framework behind Switchfin and wiliwili) over Anki's own Rust backend
(`../switch`). Card HTML/CSS is laid out by [litehtml](https://github.com/litehtml/litehtml)
and drawn with NanoVG. The UI follows AnkiDroid: deck list with
new/learning/due counts, reviewer with four answer buttons, AnkiWeb sync.

## Install and use

1. Copy `dist/AnkiNX.nro` to `sdmc:/switch/` and launch it from the Homebrew
   Menu, preferably via **title override** (hold R while starting a game). Applet
   mode (from the Album) works but has much less memory, and the app warns about it.
2. Get your cards onto the Switch, either way:
   - press **X** and log in to AnkiWeb, then choose *Download from AnkiWeb*; or
   - copy `collection.anki2` and the `collection.media` folder from Anki
     desktop's profile folder to `sdmc:/switch/ankinx/` (with Anki desktop closed).

Data lives in `sdmc:/switch/ankinx/`: `collection.anki2`, `collection.media/`,
`backups/` (rolling backups made on exit) and `sync.json` (AnkiWeb key, not the
password).

| Screen | Controls |
| --- | --- |
| Decks | **A** study · **Y** expand/collapse · **X** sync · **−** AnkiWeb account · **+** quit |
| Review | **A** show answer, then *Good* · **ZL / L / R / ZR** Again / Hard / Good / Easy · **X** show hints · **−** undo · **Y** replay audio · **D-pad / touch drag** scroll · **L3 / R3** zoom · **B** back |

Touch works too: tap the card to show the answer, tap the buttons, tap ▶ to
replay a sound, tap a hint link to reveal it.

Supported media: images PNG, JPEG, GIF (animated), BMP, **WebP**, **SVG**
(including Anki's LaTeX SVGs); audio MP3, OGG/Vorbis, Opus, WAV (SDL2_mixer) and,
through FFmpeg, **M4A/AAC, ALAC, FLAC** and the audio of WebM/MP4.

## Build

Inside WSL (see `../switch/README.md` for the toolchain):

```sh
bash /mnt/c/ankiswitch/tools/fetch_frontend_deps.sh   # once: borealis + litehtml at pinned commits
bash /mnt/c/ankiswitch/switch/build_core.sh           # Rust backend -> switch/dist
bash /mnt/c/ankiswitch/app/build.sh                   # -> app/dist/AnkiNX.nro
```

Desktop preview (same UI code, runs under WSLg; no root needed):

```sh
bash /mnt/c/ankiswitch/switch/build_core_host.sh      # backend for Linux -> switch/dist-host
bash /mnt/c/ankiswitch/app/build_desktop.sh
bash /mnt/c/ankiswitch/app/preview.sh                 # sample collection + screenshots -> app/dist/preview
```

## Layout

| Path | What |
| --- | --- |
| `src/main.cpp` | Startup, font fallbacks for cards, applet-mode warning, clean shutdown |
| `src/anki/api.cpp` | `anki::call(cmd, json)` over the backend's `ankinx_call` |
| `src/activity/deck_picker.cpp` | Deck list (`resources/xml/activity/deck_picker.xml`, `cells/deck_cell.xml`) |
| `src/activity/reviewer.cpp` | Study screen (`resources/xml/activity/reviewer.xml`) |
| `src/view/card_view.cpp` | litehtml `document_container` on NanoVG: text, images, borders, links, scrolling |
| `src/activity/login.cpp` | AnkiWeb login screen (`resources/xml/activity/login.xml`) |
| `src/util/keyboard.cpp` | System keyboard (libnx swkbd presets; borealis input dialog on desktop) |
| `src/util/sync_flow.cpp` | Sync, full-sync conflict dialogs, media sync status |
| `src/util/audio.cpp` | Card audio via SDL2_mixer (MP3, OGG, Opus, WAV) |
| `resources/i18n/en-US/ankinx.json` | UI strings |
| `assets/logo.png` | App logo (source, not bundled). The hbmenu icon `resources/icon/icon.jpg` (256x256 JPEG) is made from it with `bash tools/py_tools.sh tools/logo_to_icon.py app/assets/logo.png app/resources/icon/icon.jpg` |

## Design notes

- **Exit**: `main` calls the backend's `shutdown` before returning. It closes the
  collection and stops the tokio runtime. Without it, borealis' `userAppExit`
  tears down sockets while runtime threads are still polling, and the Switch
  shows "The software was closed because an error occurred".
- **Login**: each field gets its own keyboard, opened from a row on the login
  screen. Opening two keyboards back to back, at a length of more than 32
  characters (which forces swkbd's box layout and hides its header), left the
  second keyboard with no input field on hardware.

- **Why not the Web applet + localhost server** (the original guide's plan): the
  applet covers the app and suspends its main thread, only exists in title
  override, runs a 2016-era NetFront engine, and can't give a controller-first
  UI. Native borealis plus an embedded HTML renderer avoids all of that.
- **Card rendering limits**: no JavaScript, except that `{{hint:...}}` fields are
  handled natively. MathJax is not rendered, gradients are simplified, and
  bold/italic are synthetic (the Switch system fonts have one weight). See
  [ROADMAP.md](ROADMAP.md) for the JavaScript evaluation.
- **Fonts**: card text uses the Switch Standard font (Latin + Japanese) first,
  then Chinese, Korean and symbol fonts, so Japanese kanji keep Japanese forms.
- **Safety**: the collection is closed (with a backup) on every exit, including
  closing the app from HOME. SQLite runs with a rollback journal.
