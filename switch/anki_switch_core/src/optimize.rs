//! Optimizing a preset's FSRS parameters on the console (`optimize` in
//! api.rs), as the FSRS-7 fork's deck options do: Anki's optimizer with the
//! preset's FSRS version and options, then, when it found other parameters,
//! the current and the optimized ones evaluated on the review history, for
//! the user to choose. Runs on its own thread with the collection.

use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicU8;
use std::sync::atomic::Ordering;

use anki::collection::Collection;
use anki::deckconfig::DeckConfig;
use anki::deckconfig::DeckConfigInner;
use anki::error::AnkiError;
use anki::error::Result;
use anki::prelude::DeckConfigId;
use anki::prelude::DeckId;
use anki::services::SchedulerService;
use anki_proto::deck_config::deck_config::config::FsrsVersion;
use anki_proto::deck_config::UpdateDeckConfigsMode;
use anki_proto::scheduler::ComputeFsrsParamsRequest;
use anki_proto::scheduler::ComputeFsrsParamsResponse;
use anki_proto::scheduler::EvaluateParamsLegacyRequest;
use serde_json::Map;
use serde_json::Value;

/// The fork's per-preset optimizer options, kept in the preset's extra JSON
/// (as its deck options page writes them).
const SAME_DAY_KEY: &str = "fsrs7IncludeSameDayOptimize";
/// Its older name, still read (and dropped when the option is saved).
const SAME_DAY_LEGACY_KEY: &str = "fsrs7IncludeSameDayEvaluate";
const PENALTIES_KEY: &str = "fsrs7EnableSchedulingPenalties";
/// The search the comparison is evaluated on, when not the optimizer's.
const EVALUATION_SEARCH_KEY: &str = "fsrsEvaluationSearch";

/// Stages of a run, for its progress.
pub const OPTIMIZING: u8 = 0;
/// Evaluating the current and optimized parameters.
pub const CHECKING: u8 = 1;
/// Saving a new calibration of dynamic desired retention.
pub const SAVING: u8 = 2;

pub fn fsrs_version(c: &DeckConfigInner) -> FsrsVersion {
    FsrsVersion::try_from(c.fsrs_version).unwrap_or(FsrsVersion::Seven)
}

/// The parameters of the preset's FSRS version (empty: the defaults).
pub fn params(c: &DeckConfigInner) -> &Vec<f32> {
    match fsrs_version(c) {
        FsrsVersion::Seven => &c.fsrs_params_7,
        FsrsVersion::Six => &c.fsrs_params_6,
        FsrsVersion::Five => &c.fsrs_params_5,
        FsrsVersion::Four => &c.fsrs_params_4,
    }
}

pub fn params_mut(c: &mut DeckConfigInner) -> &mut Vec<f32> {
    match fsrs_version(c) {
        FsrsVersion::Seven => &mut c.fsrs_params_7,
        FsrsVersion::Six => &mut c.fsrs_params_6,
        FsrsVersion::Five => &mut c.fsrs_params_5,
        FsrsVersion::Four => &mut c.fsrs_params_4,
    }
}

fn extra(c: &DeckConfigInner) -> Map<String, Value> {
    match serde_json::from_slice(&c.other) {
        Ok(Value::Object(map)) => map,
        _ => Map::new(),
    }
}

fn extra_bool(c: &DeckConfigInner, key: &str) -> Option<bool> {
    extra(c).get(key).and_then(Value::as_bool)
}

/// FSRS-7: include same-day reviews when optimizing (the fork's default: yes).
pub fn include_same_day(c: &DeckConfigInner) -> bool {
    extra_bool(c, SAME_DAY_KEY)
        .or_else(|| extra_bool(c, SAME_DAY_LEGACY_KEY))
        .unwrap_or(true)
}

/// FSRS-7: use scheduling penalties when optimizing (the fork's default: no).
pub fn scheduling_penalties(c: &DeckConfigInner) -> bool {
    extra_bool(c, PENALTIES_KEY).unwrap_or(false)
}

fn set_extra(c: &mut DeckConfigInner, change: impl FnOnce(&mut Map<String, Value>)) {
    let mut map = extra(c);
    change(&mut map);
    c.other = serde_json::to_vec(&Value::Object(map)).unwrap();
}

pub fn set_include_same_day(c: &mut DeckConfigInner, on: bool) {
    set_extra(c, |map| {
        map.remove(SAME_DAY_LEGACY_KEY);
        map.insert(SAME_DAY_KEY.into(), Value::Bool(on));
    });
}

pub fn set_scheduling_penalties(c: &mut DeckConfigInner, on: bool) {
    set_extra(c, |map| {
        map.insert(PENALTIES_KEY.into(), Value::Bool(on));
    });
}

/// The search the optimizer's comparison is evaluated on (empty: the
/// optimizer's own), the desktop's "Evaluation Search Filter".
pub fn evaluation_search(c: &DeckConfigInner) -> String {
    extra(c).get(EVALUATION_SEARCH_KEY).and_then(Value::as_str).unwrap_or_default().to_string()
}

pub fn set_evaluation_search(c: &mut DeckConfigInner, search: &str) {
    set_extra(c, |map| {
        if search.trim().is_empty() {
            map.remove(EVALUATION_SEARCH_KEY);
        } else {
            map.insert(EVALUATION_SEARCH_KEY.into(), Value::String(search.trim().into()));
        }
    });
}

/// The preset has a usable calibration of dynamic desired retention (what
/// an optimization with it on saves): Anki refuses to schedule the preset's
/// cards with it on otherwise. As the fork's deck options check it
/// (dynamic-desired-retention.ts).
pub fn dynamic_retention_calibrated(c: &DeckConfigInner) -> bool {
    let finite = |v: &[f32]| v.iter().all(|x| x.is_finite());
    let calibration = |weights: &[f32], drs: &[f32], min: usize| {
        weights.len() == drs.len()
            && weights.len() >= min
            && weights.iter().all(|w| w.is_finite() && *w >= 0.0)
            && drs.iter().all(|d| d.is_finite() && (0.0..=1.0).contains(d))
    };
    let optional = |weights: &[f32], drs: &[f32], min: usize| {
        (weights.is_empty() && drs.is_empty()) || calibration(weights, drs, min)
    };
    let (lo, hi) = (c.fsrs_dynamic_desired_retention_min, c.fsrs_dynamic_desired_retention_max);
    c.fsrs_dynamic_desired_retention_params.len() == 15
        && finite(&c.fsrs_dynamic_desired_retention_params)
        && calibration(&c.fsrs_dynamic_desired_retention_weights, &c.fsrs_dynamic_desired_retention_avg_drs, 2)
        && optional(&c.fsrs_dynamic_desired_retention_fsrs_eq_weights, &c.fsrs_dynamic_desired_retention_fsrs_eq_drs, 2)
        && optional(
            &c.fsrs_dynamic_desired_retention_fixed_target_weights,
            &c.fsrs_dynamic_desired_retention_fixed_target_drs,
            1,
        )
        && lo.is_finite()
        && hi.is_finite()
        && lo > 0.0
        && lo < hi
        && hi < 1.0
}

/// What a run needs, worked out from the preset when it starts.
pub struct Job {
    pub deck_id: DeckId,
    pub config_id: DeckConfigId,
    pub request: ComputeFsrsParamsRequest,
    /// Calibrating dynamic desired retention for a preset that has it off:
    /// the calibration turns it on when saved.
    pub enable_dynamic_retention: bool,
    /// The current parameters as evaluated: FSRS-7's defaults when the preset
    /// has none yet (Anki's evaluation would take empty ones as FSRS-6's).
    current_for_evaluation: Vec<f32>,
    evaluation_search: String,
}

/// Anki's search syntax: `"` and `\` escaped (the desktop's escapeSearchText).
fn escape_search(text: &str) -> String {
    text.replace('\\', "\\\\").replace('"', "\\\"")
}

/// Relearning steps that fit in a day, as the desktop's deck options count them.
fn relearning_steps_in_day(steps: &[f32]) -> u32 {
    let mut total = 0.0;
    let mut count = 0;
    for step in steps {
        total += step;
        if total >= 1440.0 {
            break;
        }
        count += 1;
    }
    count
}

/// "2024-05-01" -> its midnight in UTC, in ms (as the desktop's `new Date`).
fn date_ms(date: &str) -> i64 {
    chrono::NaiveDate::parse_from_str(date.trim(), "%Y-%m-%d")
        .ok()
        .and_then(|d| d.and_hms_opt(0, 0, 0))
        .map_or(0, |d| d.and_utc().timestamp_millis())
}

/// The run for `config` (the deck's preset), as the fork's deck options
/// start it: the preset's search (its decks, suspended cards left out) or
/// its own, its FSRS version and same-day options, and the collection's
/// health check setting. `calibrate`: also calibrate dynamic desired
/// retention (FSRS-7), to turn it on.
pub fn job(deck_id: DeckId, config: &DeckConfig, health_check: bool, calibrate: bool) -> Job {
    let c = &config.inner;
    let seven = fsrs_version(c) == FsrsVersion::Seven;
    let enable_dynamic_retention = seven && calibrate && !c.fsrs_dynamic_desired_retention_enabled;
    let search = if c.param_search.trim().is_empty() {
        format!("preset:\"{}\" -is:suspended", escape_search(&config.name))
    } else {
        c.param_search.clone()
    };
    let evaluation_search = extra(c)
        .get(EVALUATION_SEARCH_KEY)
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map_or_else(|| search.clone(), str::to_string);
    let current = params(c).clone();
    let current_for_evaluation = if seven && current.is_empty() {
        // (an FSRS-7 preset with no parameters of its own uses the defaults)
        config.fsrs_params().to_vec()
    } else {
        current.clone()
    };
    Job {
        deck_id,
        config_id: config.id,
        request: ComputeFsrsParamsRequest {
            search,
            current_params: current,
            ignore_revlogs_before_ms: date_ms(&c.ignore_revlogs_before_date),
            num_of_relearning_steps: relearning_steps_in_day(&c.relearn_steps),
            health_check,
            include_same_day_reviews: seven.then(|| include_same_day(c)),
            fsrs_version: Some(c.fsrs_version),
            dynamic_desired_retention_enabled: seven
                && (c.fsrs_dynamic_desired_retention_enabled || enable_dynamic_retention),
            dynamic_desired_retention_review_limit: None,
            dynamic_desired_retention_max_cost_perday_minutes: None,
            enable_scheduling_penalties: Some(seven && scheduling_penalties(c)),
        },
        enable_dynamic_retention,
        current_for_evaluation,
        evaluation_search,
    }
}

/// Log loss and RMSE (bins) of some parameters on the review history.
#[derive(Clone, Copy, Debug)]
pub struct Fit {
    pub log_loss: f32,
    pub rmse_bins: f32,
}

pub struct Outcome {
    pub response: ComputeFsrsParamsResponse,
    /// The optimizer found other parameters (to 4 decimals, as the desktop
    /// compares them); then both are evaluated.
    pub changed: bool,
    pub current: Option<Fit>,
    pub optimized: Option<Fit>,
    /// From the job: a calibration turns dynamic desired retention on.
    pub enable_dynamic_retention: bool,
}

impl Outcome {
    fn has_dynamic_retention(&self) -> bool {
        !self.response.fsrs_dynamic_desired_retention_params.is_empty()
    }
}

/// Same-day reviews in the comparison: only when both sets of parameters can
/// evaluate them (FSRS-7's 34), as the desktop decides.
fn comparison_same_day(requested: Option<bool>, current: &[f32], optimized: &[f32]) -> Option<bool> {
    if requested != Some(true) {
        return requested;
    }
    Some(current.len() == 34 && optimized.len() == 34)
}

fn same_params(current: &[f32], optimized: &[f32]) -> bool {
    optimized.is_empty()
        || (!current.is_empty()
            && current.len() == optimized.len()
            && current.iter().zip(optimized).all(|(a, b)| format!("{a:.4}") == format!("{b:.4}")))
}

/// Optimize, then evaluate when the parameters changed. Stops with
/// [AnkiError::Interrupted] once `cancelled` is set (the caller also sets
/// Anki's abort flag, which stops the optimizer itself). When only the
/// calibration of dynamic desired retention is new, it is saved at once, as
/// the desktop keeps it with unchanged parameters.
pub fn run(col: &mut Collection, job: &Job, stage: &AtomicU8, cancelled: &AtomicBool) -> Result<Outcome> {
    let check = || {
        if cancelled.load(Ordering::Relaxed) {
            Err(AnkiError::Interrupted)
        } else {
            Ok(())
        }
    };
    check()?;
    let response = SchedulerService::compute_fsrs_params(col, job.request.clone())?;
    check()?;
    let changed = !same_params(&job.request.current_params, &response.params);
    let mut outcome = Outcome {
        response,
        changed,
        current: None,
        optimized: None,
        enable_dynamic_retention: job.enable_dynamic_retention,
    };
    if !changed {
        if outcome.has_dynamic_retention() {
            stage.store(SAVING, Ordering::Relaxed);
            save(col, job.deck_id, job.config_id, &outcome, false, false)?;
        }
        return Ok(outcome);
    }
    stage.store(CHECKING, Ordering::Relaxed);
    let include_same_day = comparison_same_day(
        job.request.include_same_day_reviews,
        &job.current_for_evaluation,
        &outcome.response.params,
    );
    let evaluate = |col: &mut Collection, params: &[f32]| -> Result<Fit> {
        let fit = SchedulerService::evaluate_params_legacy(
            col,
            EvaluateParamsLegacyRequest {
                params: params.to_vec(),
                search: job.evaluation_search.clone(),
                ignore_revlogs_before_ms: job.request.ignore_revlogs_before_ms,
                include_same_day_reviews: include_same_day,
            },
        )?;
        check()?;
        Ok(Fit { log_loss: fit.log_loss, rmse_bins: fit.rmse_bins })
    };
    outcome.current = Some(evaluate(col, &job.current_for_evaluation)?);
    outcome.optimized = Some(evaluate(col, &outcome.response.params)?);
    Ok(outcome)
}

fn invalid(message: &str) -> AnkiError {
    AnkiError::InvalidInput {
        source: anki::error::InvalidInputError { message: message.into(), source: None, backtrace: None },
    }
}

/// Save the outcome to the preset `config_id` (still the deck's): the
/// optimized parameters when `params`, and a new calibration of dynamic
/// desired retention if there is one. Through Anki's own update, which
/// updates the cards' memory states (and, with `reschedule`, their due dates,
/// as the desktop's "Reschedule cards on change").
pub fn save(
    col: &mut Collection,
    deck_id: DeckId,
    config_id: DeckConfigId,
    outcome: &Outcome,
    params: bool,
    reschedule: bool,
) -> Result<()> {
    let info = col.get_deck_configs_for_update(deck_id)?;
    let Some(mut config) = col.get_deck_config(config_id, false)? else {
        return Err(invalid("the preset no longer exists"));
    };
    let r = &outcome.response;
    if params {
        *params_mut(&mut config.inner) = r.params.clone();
    }
    if outcome.has_dynamic_retention() {
        let c = &mut config.inner;
        c.fsrs_dynamic_desired_retention_params = r.fsrs_dynamic_desired_retention_params.clone();
        c.fsrs_dynamic_desired_retention_weights = r.fsrs_dynamic_desired_retention_weights.clone();
        c.fsrs_dynamic_desired_retention_avg_drs = r.fsrs_dynamic_desired_retention_avg_drs.clone();
        c.fsrs_dynamic_desired_retention_fsrs_eq_weights = r.fsrs_dynamic_desired_retention_fsrs_eq_weights.clone();
        c.fsrs_dynamic_desired_retention_fsrs_eq_drs = r.fsrs_dynamic_desired_retention_fsrs_eq_drs.clone();
        c.fsrs_dynamic_desired_retention_fixed_target_weights =
            r.fsrs_dynamic_desired_retention_fixed_target_weights.clone();
        c.fsrs_dynamic_desired_retention_fixed_target_drs = r.fsrs_dynamic_desired_retention_fixed_target_drs.clone();
        c.fsrs_dynamic_desired_retention_min = r.fsrs_dynamic_desired_retention_min;
        c.fsrs_dynamic_desired_retention_max = r.fsrs_dynamic_desired_retention_max;
        if outcome.enable_dynamic_retention && dynamic_retention_calibrated(c) {
            c.fsrs_dynamic_desired_retention_enabled = true;
        }
    }
    // (the update gives the deck the preset saved: still the one it had)
    if info.current_deck.as_ref().map(|d| d.config_id) != Some(config_id.0) {
        return Err(invalid("the deck now has another preset"));
    }
    let mut request = crate::presets::update_request(&info, deck_id, vec![config], UpdateDeckConfigsMode::Normal);
    request.fsrs_reschedule = reschedule;
    col.update_deck_configs(request)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_day_and_penalties_as_the_fork_keeps_them() {
        let mut c = DeckConfigInner::default();
        assert!(include_same_day(&c), "on by default");
        assert!(!scheduling_penalties(&c), "off by default");
        c.other = br#"{"fsrs7IncludeSameDayEvaluate": false, "kept": 1}"#.to_vec();
        assert!(!include_same_day(&c), "the older key is read");
        set_include_same_day(&mut c, true);
        set_scheduling_penalties(&mut c, true);
        let map = extra(&c);
        assert_eq!(map.get(SAME_DAY_KEY), Some(&Value::Bool(true)));
        assert!(!map.contains_key(SAME_DAY_LEGACY_KEY), "the older key goes");
        assert_eq!(map.get("kept"), Some(&Value::from(1)), "other keys stay");
        assert!(scheduling_penalties(&c));
    }

    #[test]
    fn job_as_the_desktop_starts_it() {
        let mut config = DeckConfig::default();
        config.name = r#"Say "hi" \o/"#.into();
        config.inner.relearn_steps = vec![10.0, 1000.0, 1440.0];
        config.inner.ignore_revlogs_before_date = "2024-05-01".into();
        let job = job(DeckId(1), &config, true, false);
        assert_eq!(job.request.search, r#"preset:"Say \"hi\" \\o/" -is:suspended"#);
        assert_eq!(job.evaluation_search, job.request.search);
        assert_eq!(job.request.num_of_relearning_steps, 2);
        assert_eq!(job.request.ignore_revlogs_before_ms, 1_714_521_600_000);
        assert_eq!(job.request.include_same_day_reviews, Some(true), "FSRS-7 by default");
        assert!(job.request.current_params.is_empty());
        assert_eq!(job.current_for_evaluation.len(), 34, "evaluated with FSRS-7's defaults");

        config.inner.fsrs_version = FsrsVersion::Six as i32;
        config.inner.param_search = "deck:French".into();
        config.inner.other = br#"{"fsrsEvaluationSearch": " deck:French::Verbs "}"#.to_vec();
        let job = super::job(DeckId(1), &config, false, false);
        assert_eq!(job.request.search, "deck:French");
        assert_eq!(job.evaluation_search, "deck:French::Verbs");
        assert_eq!(job.request.include_same_day_reviews, None, "FSRS-7 only");
        assert!(job.current_for_evaluation.is_empty(), "FSRS-6 evaluates its own defaults");
    }

    #[test]
    fn dynamic_retention_needs_a_calibration() {
        let mut config = DeckConfig::default();
        let c = &mut config.inner;
        assert!(!dynamic_retention_calibrated(c), "none by default");
        c.fsrs_dynamic_desired_retention_params = vec![0.5; 15];
        c.fsrs_dynamic_desired_retention_weights = vec![0.0, 1.0];
        c.fsrs_dynamic_desired_retention_avg_drs = vec![0.9, 0.8];
        c.fsrs_dynamic_desired_retention_min = 0.3;
        c.fsrs_dynamic_desired_retention_max = 0.995;
        assert!(dynamic_retention_calibrated(c));
        c.fsrs_dynamic_desired_retention_params.pop();
        assert!(!dynamic_retention_calibrated(c), "15 policy parameters");
        c.fsrs_dynamic_desired_retention_params.push(f32::NAN);
        assert!(!dynamic_retention_calibrated(c), "finite");
        c.fsrs_dynamic_desired_retention_params = vec![0.5; 15];
        c.fsrs_dynamic_desired_retention_fixed_target_weights = vec![1.0];
        assert!(!dynamic_retention_calibrated(c), "matching fixed target arrays");
        c.fsrs_dynamic_desired_retention_fixed_target_drs = vec![0.9];
        assert!(dynamic_retention_calibrated(c));
        c.fsrs_dynamic_desired_retention_max = 1.0;
        assert!(!dynamic_retention_calibrated(c), "bounds below 1");

        // Calibrating turns it on (FSRS-7 only, and only when it is off).
        let job = super::job(DeckId(1), &config, false, true);
        assert!(job.request.dynamic_desired_retention_enabled && job.enable_dynamic_retention);
        let job = super::job(DeckId(1), &config, false, false);
        assert!(!job.request.dynamic_desired_retention_enabled && !job.enable_dynamic_retention);
        config.inner.fsrs_version = FsrsVersion::Six as i32;
        let job = super::job(DeckId(1), &config, false, true);
        assert!(!job.request.dynamic_desired_retention_enabled && !job.enable_dynamic_retention);
    }

    #[test]
    fn evaluation_search_in_the_presets_extra_json() {
        let mut c = DeckConfigInner::default();
        set_evaluation_search(&mut c, " deck:A ");
        assert_eq!(evaluation_search(&c), "deck:A");
        set_evaluation_search(&mut c, "");
        assert!(!extra(&c).contains_key(EVALUATION_SEARCH_KEY), "empty: the optimizer's search");
    }

    #[test]
    fn parameters_compare_to_four_decimals() {
        assert!(same_params(&[0.12341, 2.0], &[0.12344, 2.0]));
        assert!(!same_params(&[0.1234, 2.0], &[0.1236, 2.0]));
        assert!(!same_params(&[], &[0.1; 21]), "defaults to parameters of its own");
        assert!(same_params(&[0.1; 21], &[]), "nothing optimized");
        assert_eq!(comparison_same_day(Some(true), &[0.0; 34], &[0.0; 21]), Some(false));
        assert_eq!(comparison_same_day(Some(true), &[0.0; 34], &[0.0; 34]), Some(true));
        assert_eq!(comparison_same_day(None, &[0.0; 34], &[0.0; 34]), None);
    }
}
