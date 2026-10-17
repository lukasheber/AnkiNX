# Changelog

AnkiNX follows [semantic versioning](https://semver.org/) (0.x while
features are still arriving). The version lives in `VERSION`: the `.nro`
(hbmenu shows it) and `switch/anki_switch_core/Cargo.toml` must match, and
`switch/build_core.sh` checks this.

How to release: update `VERSION` and the crate version, add a section here,
build, then commit and tag (`git tag v0.2.0`).

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
  by default, switchable in the account menu (-). Not yet tested on hardware.
- Type-in-the-answer cards (`{{type:...}}`): answer box with the system
  keyboard, Anki's comparison on the answer side.
- Image Occlusion cards: masks drawn natively; X toggles them on the answer
  side. Not yet tested on hardware.
- "Missing media file" notice instead of silent audio.
- Backups (.colpkg) written after closing, restorable in Anki.
- New app icon.
- `crash.log` in the data folder records Rust panics.

## 0.1.0

First builds: deck list, review with Anki's scheduler, hints, audio, images
(PNG/JPEG/WebP/SVG), manual sync.
