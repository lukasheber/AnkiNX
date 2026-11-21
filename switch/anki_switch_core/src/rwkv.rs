//! RWKV on the console (phase 3 of docs/fsrs7-plan.md), through kuma3's
//! offline layer (switch/patches/anki-rwkv.patch): once the collection is
//! open, `start` asks it to load the model state (from the state file next to
//! the collection, replaying only newer reviews, or the whole history on its
//! own thread); until it is ready, and whenever it can't run, RWKV presets are
//! scheduled with their FSRS version.
//!
//! AnkiNX never turns RWKV on: it runs it for presets set to RWKV in the
//! FSRS-7 fork's desktop. Collections vary a lot in size and the console has
//! little memory to spare, so a start is checked against the memory free, and
//! everything is logged (core.log, next to ankinx.log) to learn what RWKV
//! costs on the console: memory, timings, history size, the state file.

use std::path::Path;
use std::path::PathBuf;
use std::time::Instant;

use anki::collection::Collection;
use anki::error::Result;
use anki::prelude::DeckId;
use anki::services::SchedulerService;
use anki_proto::scheduler::RwkvOfflineInstantPassStepRequest;
use anki_proto::scheduler::RwkvPrepareOfflineRequest;
use serde_json::json;
use serde_json::Value;

use crate::sysinfo;

/// Model sizes, as the FSRS-7 fork's desktop defines them
/// (`RWKV_SIZE_MODELS` in qt/aqt/rwkv_model.py), each a pair: the two-head
/// model (RWKV-Curve: intervals, S90; Small's has an answer-time head) and the
/// Instant-only model (RWKV-Instant). Small is the desktop's default, and
/// AnkiNX's; Tiny costs less time, memory and SD space. Big isn't bundled.
/// The choice is the device's, as it is the desktop profile's (not synced).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ModelSize {
    Tiny,
    Small,
}

impl ModelSize {
    /// "tiny", else Small (the default).
    pub fn parse(name: &str) -> Self {
        if name.eq_ignore_ascii_case("tiny") {
            ModelSize::Tiny
        } else {
            ModelSize::Small
        }
    }

    fn models(self) -> (&'static str, &'static str) {
        match self {
            ModelSize::Tiny => ("sm106", "sm101"),
            ModelSize::Small => ("sm92", "sm98"),
        }
    }
}

/// The desktop loads the f32 weights (RWKV_MODEL_FILES): the same here, so
/// the predictions are the desktop's.
const WEIGHTS: &str = "weights.f32.bin";
/// The desktop's default state precision, "cards_int4_notes_int8"
/// (qt/aqt/rwkv_precision.py): kuma3's "int4".
const STATE_PRECISION: &str = "int4";

/// First guess at the memory a start needs, checked against what is free:
/// a fixed part (models, history read) and a part per card (card and note
/// states, scores). From host runs of the Tiny pair (12.4k cards, 118k
/// reviews: about 85 MB of state); to be corrected from console logs, which
/// show the estimate next to what was used.
const BASE_ESTIMATE: u64 = 96 << 20;
const PER_CARD_ESTIMATE: u64 = 16 << 10;

/// Which kinds of RWKV the collection's presets use.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Presets {
    pub curve: bool,
    pub instant: bool,
}

impl Presets {
    pub fn any(&self) -> bool {
        self.curve || self.instant
    }
}

pub fn presets(col: &mut Collection) -> Result<Presets> {
    let info = col.get_deck_configs_for_update(DeckId(1))?;
    let mut presets = Presets::default();
    for config in info.all_config.iter().filter_map(|c| c.config.as_ref()) {
        let inner = config.config.as_ref();
        presets.curve |= inner.is_some_and(|c| c.rwkv_review_enabled);
        presets.instant |= inner.is_some_and(|c| c.rwkv_review_instant_order_enabled);
    }
    Ok(presets)
}

/// Whether studying the current deck can involve RWKV: its preset or a
/// subdeck's (each card is scheduled with its home deck's preset), or, for a
/// filtered deck, any preset.
pub fn current_deck_uses_rwkv(col: &mut Collection) -> Result<bool> {
    use anki::decks::DeckKind;
    let deck = col.get_current_deck()?;
    let uses = |config: &anki::deckconfig::DeckConfig| {
        config.inner.rwkv_review_enabled || config.inner.rwkv_review_instant_order_enabled
    };
    if matches!(deck.kind, DeckKind::Filtered(_)) {
        return Ok(presets(col)?.any());
    }
    let name = deck.human_name();
    let prefix = format!("{name}::");
    let mut ids = vec![deck.id];
    for (id, other) in col.get_all_normal_deck_names(false)? {
        if other.starts_with(&prefix) {
            ids.push(id);
        }
    }
    for id in ids {
        let Some(config_id) = col.get_deck(id)?.and_then(|d| d.config_id()) else {
            continue;
        };
        if col.get_deck_config(config_id, true)?.is_some_and(|c| uses(&c)) {
            return Ok(true);
        }
    }
    Ok(false)
}

/// The size of what RWKV works on.
#[derive(Clone, Copy, Debug, Default)]
pub struct Size {
    pub cards: u64,
    pub notes: u64,
    pub reviews: u64,
}

fn size(col: &Collection) -> Size {
    let count = |table: &str| -> u64 {
        col.storage
            .db()
            .query_row(&format!("select count() from {table}"), [], |r| r.get::<_, i64>(0))
            .map_or(0, |n| n as u64)
    };
    Size { cards: count("cards"), notes: count("notes"), reviews: count("revlog") }
}

pub fn estimate(size: Size) -> u64 {
    BASE_ESTIMATE + size.cards * PER_CARD_ESTIMATE
}

/// kuma3's state file, next to the collection.
fn state_file(collection: &Path) -> PathBuf {
    collection.with_extension("rwkv-offline")
}

/// Delete what failed saves of the state file left next to the collection
/// (kuma3 writes a temporary file, then renames it; on the console a failed
/// save couldn't delete it, being still open): tens of MB each. Only when
/// RWKV starts, when no save is under way (closing waits for them).
fn remove_leftover_saves(collection: &Path) {
    let Some(dir) = collection.parent() else {
        return;
    };
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.starts_with(".tmp") && (name.ends_with(".rwkv-offline.tmp") || name.ends_with(".rwkv-calibration.tmp")) {
            let size = entry.metadata().map_or(0, |m| m.len());
            match std::fs::remove_file(entry.path()) {
                Ok(()) => tracing::info!(file = %name, mb = sysinfo::mb(size), "RWKV: removed a leftover save"),
                Err(e) => tracing::warn!(file = %name, error = %e, "RWKV: leftover save not removed"),
            }
        }
    }
}

fn file_mb(path: &Path) -> Option<u64> {
    std::fs::metadata(path).ok().map(|m| sysinfo::mb(m.len()))
}

/// Why RWKV isn't running, for the app to say.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Off {
    /// No preset uses it (the usual case).
    NotUsed,
    /// Not enough memory free (applet mode, or a very large collection).
    Memory { need: u64, free: u64 },
    /// The models aren't where they should be, or kuma3's layer refused.
    Failed(String),
}

/// RWKV while the collection is open: started, then ready (or not).
pub struct Run {
    started: Instant,
    collection: PathBuf,
    size: Size,
    /// When it was ready (logged once).
    ready_after: Option<std::time::Duration>,
    /// Last progress logged: (replayed, when).
    logged: (u64, Instant),
}

/// Start RWKV for the open collection at `collection`, with the models in
/// `models` (romfs:/rwkv on the console). Ok(Err(why)) when it doesn't run.
pub fn start(
    col: &mut Collection,
    collection: &Path,
    models: &Path,
    model_size: ModelSize,
    single_model: bool,
) -> std::result::Result<Run, Off> {
    let presets = presets(col).map_err(|e| Off::Failed(format!("{e:?}")))?;
    if !presets.any() {
        tracing::info!("RWKV: no preset uses RWKV");
        return Err(Off::NotUsed);
    }
    remove_leftover_saves(collection);
    let size = size(col);
    let memory = sysinfo::memory();
    let need = estimate(size);
    tracing::info!(
        model_size = ?model_size,
        single_model,
        curve = presets.curve,
        instant = presets.instant,
        cards = size.cards,
        notes = size.notes,
        reviews = size.reviews,
        state_file_mb = ?file_mb(&state_file(collection)),
        need_mb = sysinfo::mb(need),
        memory = %sysinfo::memory_text(),
        process = %sysinfo::process_memory().map_or("unknown".into(), |m| m.to_string()),
        "RWKV: starting"
    );
    if let Some(memory) = memory {
        if memory.free() < need {
            tracing::warn!(need_mb = sysinfo::mb(need), free_mb = sysinfo::mb(memory.free()), "RWKV: not enough memory free; FSRS schedules RWKV presets");
            return Err(Off::Memory { need, free: memory.free() });
        }
    }
    let (two_head, instant) = model_size.models();
    let two_head = models.join(two_head).join(WEIGHTS);
    let instant = models.join(instant).join(WEIGHTS);
    for model in [&two_head, &instant] {
        if !model.is_file() {
            let why = format!("missing model {}", model.display());
            tracing::warn!("RWKV: {why}");
            return Err(Off::Failed(why));
        }
    }
    let started = Instant::now();
    let prepared = SchedulerService::rwkv_prepare_offline(
        col,
        RwkvPrepareOfflineRequest {
            model_path: two_head.to_string_lossy().into(),
            // (the desktop's: int4 card states, int8 note states;
            // ANKINX_RWKV_STATE_PRECISION, e.g. "f32", for comparisons on a PC)
            state_precision: std::env::var("ANKINX_RWKV_STATE_PRECISION").unwrap_or_else(|_| STATE_PRECISION.into()),
            // the leak-free input rules of the fork's current models
            model_kind: "sm72".into(),
            instant_model_path: instant.to_string_lossy().into(),
            // the desktop's "one model for both schedulers" (off by default)
            single_model,
        },
    );
    match prepared {
        Ok(response) => {
            tracing::info!(
                reviews_to_replay = response.reviews_replayed,
                ms = started.elapsed().as_millis() as u64,
                memory = %sysinfo::memory_text(),
                "RWKV: prepare called (the model state loads on its own thread)"
            );
            Ok(Run { started, collection: collection.into(), size, ready_after: None, logged: (0, Instant::now()) })
        }
        Err(e) => {
            tracing::warn!(error = ?e, "RWKV: prepare failed; FSRS schedules RWKV presets");
            Err(Off::Failed(format!("{e:?}")))
        }
    }
}

impl Run {
    /// Where loading the model state is (cheap: it doesn't score), logged
    /// when it changes: every 10% of a replay, and once when ready, with the
    /// time it took, the memory and the state file.
    pub fn status(&mut self, col: &mut Collection) -> Value {
        let request = RwkvOfflineInstantPassStepRequest { restart: false, status_only: true };
        let progress = match SchedulerService::rwkv_offline_instant_pass_step(col, request) {
            Ok(progress) => progress,
            Err(e) => {
                // kuma3 reports a history that changed under the model state
                // (an undo, a sync, a full download) once, having started a
                // rebuild: asked again, it reports that rebuild.
                tracing::info!(error = ?e, "RWKV: the model state is rebuilt");
                match SchedulerService::rwkv_offline_instant_pass_step(col, request) {
                    Ok(progress) => progress,
                    Err(e) => {
                        tracing::warn!(error = ?e, "RWKV: status failed");
                        return json!({ "state": "failed", "error": format!("{e:?}") });
                    }
                }
            }
        };
        if progress.available {
            if self.ready_after.is_none() {
                let took = self.started.elapsed();
                self.ready_after = Some(took);
                tracing::info!(
                    secs = took.as_secs_f32(),
                    reviews_absorbed = progress.reviews_absorbed,
                    cards = self.size.cards,
                    state_file_mb = ?file_mb(&state_file(&self.collection)),
                    estimate_mb = sysinfo::mb(estimate(self.size)),
                    memory = %sysinfo::memory_text(),
                    "RWKV: ready"
                );
            }
            return json!({
                "state": "ready",
                "reviews": progress.reviews_absorbed,
                "secs": self.ready_after.map(|d| d.as_secs_f32()),
            });
        }
        let (done, total) = (progress.replay_done, progress.replay_total);
        let step = (total / 10).max(1);
        if total > 0 && (done / step != self.logged.0 / step || self.logged.1.elapsed().as_secs() >= 60) {
            self.logged = (done, Instant::now());
            tracing::info!(
                done,
                total,
                secs = self.started.elapsed().as_secs_f32(),
                memory = %sysinfo::memory_text(),
                "RWKV: replaying the review history"
            );
        }
        json!({ "state": "loading", "done": done, "total": total })
    }
}
