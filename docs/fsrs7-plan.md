# FSRS-7 and RWKV

Developed on the `experimental/fsrs7` branch.

## Why

FSRS-7 (fsrs-rs 7.0.0) is out, but official Anki won't ship it for a while.
[JSchoreels/anki](https://github.com/JSchoreels/anki) is an unofficial fork of
Anki 26.09.4 that already schedules with FSRS-7 and adds RWKV, a neural memory
model with two schedulers: **RWKV-Curve** (due dates from the predicted
forgetting curve) and **RWKV-Instant** (picks the review cards with the lowest
predicted recall right now). Desktop users of the fork sync with AnkiWeb, so
AnkiNX has to read and keep their presets and FSRS-7 memory states correctly,
and ideally schedule the same way.

[kuma3-anki-app](https://github.com/Pedrubik2000/kuma3-anki-app) (AnkiDroid on
the fork) shows how this works on a handheld; its RWKV notes (`RWKV.md`) are the
reference for the parts below that it already solved.

## Source

`tools/env.sh` pins the fork: `ANKI_DIR=anki-fsrs7`, `ANKI_REPO`,
`ANKI_COMMIT` (`main` after build 115: fsrs-rs 7.0.0 and the FSRS-7 review
fixes). `tools/fetch_anki.sh` puts it, unmodified, in `source_code/anki-fsrs7`
and fetches its translations (the fork's own `anki-core-i18n`). Moving to a
newer fork commit is a change of `ANKI_COMMIT` plus a rebuild.

`switch/patches/anki.patch` (media sync) applies unchanged: the fork didn't
touch `rslib/src/sync/media/syncer.rs`. Our vendored libc / mio / tokio are
already at or above the fork's versions.

## What runs where

| Part | Desktop fork | AnkiNX |
| --- | --- | --- |
| FSRS-7 scheduling, memory states, state repair after sync | Rust (rslib, fsrs-rs) | the same Rust, unchanged |
| FSRS optimizer (FSRS-6 and FSRS-7 parameters) | Rust | the same Rust, on the console's CPU |
| RWKV engine (inference, model state, NEON kernels) | Rust (`rslib/src/rwkv`) | the same Rust |
| RWKV orchestration: replay history, score queues, Curve intervals | **Python** (`qt/aqt/rwkv_scheduler.py`, ~30k lines) | needs a Rust driver (as kuma3's `offline.rs`) |
| Deck options | Svelte web page | native screen (no browser engine) |

## Phases

### 1. Build on the fork

- Core crate (`switch/anki_switch_core`) builds against the fork for the host
  and the Switch; dependency bumps: rusqlite 0.41, libsqlite3-sys 0.39,
  zip 9, zstd 0.14; `Cargo.lock` seeded from the fork's.
- Adapt our API to rslib changes (new `UpdateDeckConfigsRequest` fields, new
  `FsrsMemoryState` fields in custom scheduling, etc.); host tests pass.
- Check on the console: open, review, sync with a collection that the desktop
  fork has used (FSRS-7 presets, `s_int`/`s_fast` repaired after sync).

### 2. FSRS-7 in the app

- **Deck options › Scheduler**: FSRS-6 / FSRS-7 per preset (the fork's model:
  `fsrs_version`; RWKV choices shown once phase 3 works), desired retention,
  parameters (shown, reset to defaults), **Optimize** this preset on the
  console, and the FSRS-7 options: include same-day reviews when optimizing,
  same-day (re)learning steps, skip the learning queues, minimum interval.
- **Card info**: FSRS-7 rows the backend provides (`extra_rows`).
- **Custom scheduling** (`scheduling.rs`): carry the new memory-state fields.
- Presets set to RWKV on the desktop: keep the setting, schedule with their
  FSRS due-date scheduler, and say so in Deck options until phase 3.

### 3. RWKV (secondary)

- Start from kuma3's offline layer: `rslib/src/scheduler/rwkv/offline*.rs`
  (~235 KB of Rust: history replay, scoring, Curve intervals, the state file,
  workload simulation) in
  [kuma3-anki-core](https://github.com/Pedrubik2000/kuma3-anki-core) `rwkv`.
  As of 2026-11-20 that branch is the fork at 2026-11-14 (build 115) plus 124
  commits in 87 files, 22 commits behind our pin, and already handles Big,
  Small and Tiny. Either build from it (pin it in `tools/env.sh` once it has
  merged the fork's newer commits) or carry its rslib changes as our patch;
  decide by how much of it is Android-only.
- Ship the **Tiny** model by default (smallest state per card, NEON path), Small
  as an option; models go in romfs.
- State file next to the collection, so a start only replays newer reviews.
- Measure on the console: full replay time, memory per 10k cards, the time
  added to building a queue and to each answer. Decide defaults from that.

## Status (2026-11-20)

- **Phase 1 done** except the console check: the core builds for the Switch
  from the fork (0 missing symbols) and the app links; host tests pass on
  Linux and under the Horizon emulator. Lockfile pins that keep the Switch
  build working: socket2 0.6.5 (0.6.3 lacks Horizon), tempfile 3.21.0 +
  rustix 1.0.8 (rustix 1.1 needs `dirfd`). The fork's new `tar` dependency
  (desktop updater) needed `futimes`/`lutimes` in our libc port and shim.
- **Phase 2 mostly done**: deck options show and change the FSRS version, reset
  parameters, the same-day minimum interval and the two collection-wide
  FSRS-7 options; saves keep the desktop's collection-wide settings
  (`presets.rs`); custom scheduling carries the fork's new fields both ways
  (`scheduling.rs`, `app/js/src/scheduling.js`). RWKV presets show a note and
  are scheduled with their FSRS version (tested: their reviews still show).
- **Optimize on the console** (`src/optimize.rs`; API `optimize`,
  `optimize_status`, `optimize_cancel`, `set_deck_options {apply_optimized}`):
  the fork's `ComputeFsrsParams`, started as its deck options page starts it
  (the preset's search or its own, FSRS version, relearning steps within a
  day, ignore-before date, health check, the FSRS-7 options "same-day
  reviews" and "scheduling penalties" kept in the preset's extra JSON as the
  fork keeps them, dynamic desired retention when on). It runs on its own
  thread, which has the collection meanwhile (Anki opens it in exclusive
  mode: there can't be a second connection); every other call gets "busy".
  Cancel sets Anki's abort flag, again on each poll since the optimizer
  clears it when it starts. Not while the media sync runs: both report
  through the collection's one progress. When the parameters changed, the
  current and optimized ones are evaluated (`EvaluateParamsLegacy`: log loss,
  RMSE) and the user chooses, as on the desktop; applying saves through
  Anki's update (memory states updated). A new dynamic-retention calibration
  with unchanged parameters is saved at once, as the desktop keeps it.
  Tested on the host with a year of made-up reviews (changed, applied,
  cancelled, stopped by closing).
- **Not ported** (desktop-only extras of the fork's page): "Optimize all
  presets", the custom decay table, "Same-day reviews: Help me decide",
  the evaluation search box (its saved value is used), the workload
  simulator. Card info `extra_rows` (filled by the desktop's Python).
- **Next**: on-console check with a collection from the desktop fork (also:
  how long Optimize takes on the console's CPU, and its memory), then
  phase 3.

## Risks

- **Memory**: RWKV keeps a state per card and note (Tiny ~5 KB, Small ~7 KB at
  the default precision). Fine for tens of thousands of cards in full-memory
  mode; applet mode has far less.
- **CPU**: the optimizer and a full RWKV replay are heavy on four 1 GHz A57
  cores; both need progress, cancel, and to run off the UI thread.
- **Moving target**: the fork releases often; pin, and update deliberately.
