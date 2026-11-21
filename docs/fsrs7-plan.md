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

#### How it is done (2026-11-20)

- **Source**: kuma3's `rwkv` branch (a8cf880) merged with our fork pin, carried
  as `switch/patches/anki-rwkv.patch` (applied before `anki.patch`, see
  `ANKI_PATCHES` in `tools/env.sh`). The merge lives on a local branch of a
  clone of kuma3-anki-core (`source_code/kuma3-anki-core`, branch
  `ankinx-merge`); the patch is `git diff <ANKI_COMMIT> HEAD -- rslib proto
  ftl/core Cargo.toml Cargo.lock`. Conflicts: answering (kuma3's Curve
  intervals and add-on retention rules on top of the fork's
  `next_answer_states`), FSRS repair and preset helpers (both kept). Our
  commits on top of the merge:
  - the FSRS-7 repair made callable (AnkiNX opens collections without the
    backend, which runs it on open and after a full sync);
  - undoing a non-answer op (bury, suspend, set due) shows the restored card
    first again, and an op that changes the shown card ends kuma3's keeping
    it first (it compared whole-second mtimes);
  - the state file closed before it is renamed (Horizon refuses to rename an
    open file: every save failed with EIO), with the failing step in errors;
  - `answer_card_updating_rwkv`: the backend's AnswerCard (answer, then the
    answer into the model state) for direct callers; without it every answer
    in AnkiNX made the next queue build replay the whole history.
- **AnkiNX** (`switch/anki_switch_core/src/rwkv.rs`): after open (and a full
  sync), if presets use RWKV (set on the desktop: AnkiNX never turns it on),
  start kuma3's prepare with the bundled models (`app/resources/rwkv`: Tiny
  sm106/sm101 by default, Small sm92/sm98 with `"rwkv_size": "small"` in
  settings.json), after checking the memory free; `rwkv_status` for the app,
  which polls it while loading, logs it, and refreshes the deck list when
  ready. Leftover temporary saves are removed at start.
- **Logging**: `core.log` next to `ankinx.log` (the previous run's kept as
  `core.old.log`), opened and closed for each line so it can be read while the
  app runs: memory (the malloc heap: libnx reserves it all at startup, so
  Horizon's own counters always show the process full), sizes, timings,
  kuma3's own lines (history read, runtime built, queue scores, each answer).

#### Measured on the console (2026-11-20)

8,921 cards, 75,625 reviews (500 days, `make_forest_history`), presets on
RWKV-Instant + Curve, Tiny model, full-memory mode (heap ~2.9-3.0 GB):

| | |
| --- | --- |
| history read at start | 0.44 s |
| full replay (own thread; FSRS meanwhile) | 123-126 s (PC: ~12 s) |
| heap: start / peak while replaying / after | 150 / 410-509 / ~290 MB |
| state file | 61 MB (~7 KB per card) |
| scoring the review queue (3,600 cards) | 3.6 s |
| an answer into the model state | 1 ms |

RWKV-Instant picks the reviews (200 due per deck, the daily limit, where FSRS
showed 0). Synthetic histories must not have reviews dated in the future: an
answer then isn't the newest review, and RWKV takes the history as changed and
replays it all.

**Re-scoring the queue after answers.** The desktop fork re-scores the
RWKV-Instant queue after every answer ("Update the RWKV queue every 1
answers"), and by default does it in the background: "Precompute next card
before this card answer" (on by default) shows the next card from the scores
as they were and updates them afterwards ("faster, approximate queue updates",
which recheck only the likeliest next cards, are opt-in). kuma3 doesn't read
that option: it always re-scores before showing the next card, the desktop's
behaviour with precompute off: 3.7 s of waiting per card on the console. The
patch implements the desktop's default: an answer leaves a due re-scoring
pending, the next card comes at once (the answered card's own score patched),
and the study screen has the queue re-scored while that card is shown
(`rwkv_refresh`). With precompute off, or when the queue ran out, the next card
waits, as before; the other options apply as synced. An answer that comes
before the re-scoring is done waits for its end. (kuma3 is a port, not the
reference: its choices are checked against the desktop fork's code.)

Both sizes on the console, same collection, precompute (default settings):

| | Tiny (sm106 + sm101) | Small (sm92 + sm98) |
| --- | --- | --- |
| full replay | 123-126 s | 139 s |
| start from the state file | 3.1 s | (to measure) |
| heap after loading | ~240-290 MB | ~340 MB |
| an answer (into the model state, its score patched) | ~20 ms | ~25 ms |
| background re-scoring of 6.5k cards after each answer | 3.8 s | 5.0 s |

At a normal pace (7-9 s per card) the re-scoring is done before the next
answer with either size; answering faster waits for it (with Tiny, cards came
every ~5 s at most). Whether Small predicts better than Tiny is not something
we measured; the sizes' comparisons we have are kuma3's.

## Next round (in progress, 2026-11-21)

- **Display order** (`src/display_order.rs`, the desktop's DisplayOrder.svelte):
  gather and sort orders (sort orders the gather order disables are left
  out, and reset as on the desktop), new/review, interday and intraday
  learning/review order, review sort order (FSRS's own entries with FSRS).
  The intraday order is the fork's: kept in the preset's extra JSON
  (`jschoreels.deckoptions.intraday_learning_mix`, older switch removed).
- **Easy days** (`src/easy_days.rs`, EasyDays.svelte): load balancer and
  review fuzz switches (collection-wide), each weekday as Minimum /
  Reduced / Normal (the desktop's slider steps), fuzz base and factors
  (collection-wide, percent as on the desktop), the fuzz preview (default →
  current), the "no normal day" and "not rescheduled" warnings.
- **Presets** (`presets::action_request`, ConfigSelector + SaveButton):
  choose the deck's preset among all (desktop's list and entries), add,
  clone, rename, remove (default refused; full-sync warning; the deck gets
  the preset before it), save to all subdecks.
- Still to do: check on the console; Tiny and "one model" against the desktop.

## Status (2026-11-21): released as 0.11.0-fsrs7 (pre-release)

Checked on the console against the desktop fork (build 26.9.4), with the
answer side shown on the desktop (it applies RWKV-Curve in
`_answerButtons`), on an 8,921-card / 75.7k-review collection:

- **RWKV (Small, two models)**: same next card and the same buttons as the
  desktop; reviews answered on either side sync with the intervals shown;
  the console's state after its own answers equals a full replay of the
  synced history (checked against a fresh download replayed on a PC). The
  desktop's predictions after a sync stay stale until it restarts
  (reported: JSchoreels/anki#30).
- **FSRS-7**: same cards and buttons (same-day hours included) both ways;
  Hard/Easy/Again from the console arrive with the shown intervals.
- **Optimize**: current parameters score the same; the optimized ones differ
  in the 4th decimal between the console (ARM) and a PC (x86), each
  deterministic.
- **Console**: first replay 142 s (Small) / 128 s (Tiny), later starts
  ~15 s from the state file plus the new reviews; heap under 330 MB; the
  study gate and model change work.
- **Next round**: Display order and Easy days sections, switching presets,
  Tiny and "one model" against the desktop.

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
- **Deck options grouped as the desktop's** (its sections and order, labels
  from the fork's own strings): Daily limits, New cards, Lapses (with the two
  collection-wide same-day switches), Scheduler, the chosen scheduler's
  sections, Burying, Audio, Timer, Auto advance, Advanced (FSRS's minimum
  interval with FSRS). Scheduler: the Review scheduler is the choice (picking
  one turns FSRS on; no separate on/off row; a collection on SM-2 shows
  "SM-2"), the Fallback with RWKV-Instant (RWKV-Curve recommended), Desired
  retention "Preset" / "This deck"; `src/scheduler_choice.rs` ports
  `scheduler-choice.ts` with its tests. RWKV section with RWKV is chosen
  (Instant and Curve parts, Advanced: model Small/Tiny and "one model", kept
  per console like the desktop's per-profile settings, then RWKV-Instant's
  queue updates). FSRS section only when FSRS is the scheduler or the
  Fallback: parameters, Optimize, FSRS-7's options, Dynamic DR (ADR) with its
  clamp, Optimize and Evaluation search filters, Reschedule cards on change
  (per screen, as the desktop's), health check. Dynamic DR is
  refused without a calibration (Anki can't schedule its cards then: the
  desktop only warns); turning it on offers the optimization that calibrates
  it and turns it on when saved.
- **Not ported** (desktop-only extras of the fork's page): "Optimize all
  presets", the custom decay table, "Same-day reviews: Help me decide",
  editing ADR's policy/calibration arrays by hand and its DR plot, the
  workload simulator. Card info `extra_rows` (filled by the desktop's Python).
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
