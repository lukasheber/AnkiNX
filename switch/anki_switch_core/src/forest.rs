//! The forest Memory Forest grows from a collection (app/forest): one tree a
//! day of new cards, grown by how well they are remembered, with its ponds,
//! animals and anniversaries, and the scene it is drawn in. A port of the
//! add-on's study_log.py, forest_data.py, memory.py and milestones.py, the
//! Peaceful parts of events.py and events_state.py, and scene.py's mood, at
//! the commit pinned in app/library/PINS; names follow theirs. Days are
//! counted as `ago` (0 today, 1 yesterday, ...), as there.
//!
//! The add-on's defaults throughout: every deck, suspended cards keep their
//! trees, no start date. Nothing is remembered between calls (the add-on
//! remembers when each animal came; here they are those the numbers bring).

use std::collections::BTreeMap;
use std::collections::HashMap;
use std::collections::HashSet;

use anki::collection::Collection;
use anki::error::Result;
use chrono::Datelike;
use chrono::NaiveDate;
use chrono::TimeZone;
use regex::Regex;
use serde_json::json;
use serde_json::Map;
use serde_json::Value;
use sha1::Digest;
use sha1::Sha1;

const DAY_SECS: i64 = 86400;
// Revlog types 0-3 are real study (learn, review, relearn, filtered); 4+ are
// manual reschedules and resets.
const STUDY_TYPES: &str = "(0, 1, 2, 3)";
const LAPSE_WINDOW_DAYS: i64 = 7;
// Only cards studied within this many days count as a leech.
const ACTIVE_DAYS: i64 = 60;
// A leech whose interval has grown this far is cured.
const CURED_IVL: i64 = 21;

const MATURE_DAYS: f64 = 21.0;
const OLD_DAYS: f64 = 180.0;
const ANCIENT_DAYS: f64 = 365.0;
const SEEDLING: i64 = 0;
const SAPLING: i64 = 1;
const YOUNG: i64 = 2;
const MATURE: i64 = 3;
const OLD: i64 = 4;
const ANCIENT: i64 = 5;
const SAPLING_LEARNING_SHARE: f64 = 0.5;
const CROWN_SIZE_CARDS: (i64, i64) = (15, 40);
const PINE_PERCENT: u32 = 34;
const HEALTH_THRESHOLDS: [f64; 3] = [0.12, 0.25, 0.45];
const HEALTH_MIN_STAGE: i64 = MATURE;
const BREAK_DAYS: i64 = 7;
const MAX_INDIVIDUAL_TREES: usize = 730;

// FSRS forgetting curve (memory.py)
const DEFAULT_DECAY: f64 = 0.5;
const STABILITY_RECALL: f64 = 0.9;

// events.py: big learning days, and cured leeches' songbirds
const BIG_DAY_WINDOW: i64 = 14;
const BIG_DAY_JUMP: f64 = 1.25;
const BIG_DAY_MORE: i64 = 2;
const BIG_DAY_HISTORY: usize = 7;
const CURED_DAYS: i64 = 7;

// milestones.py
const RABBIT_TREES: i64 = 50;
const DEER_STREAK: i64 = 30;
const STAG_STREAK: i64 = 100;
const FOX_REVIEWS: i64 = 10_000;
const CABIN_AGE_DAYS: i64 = 365;
const BEAR_STREAK: i64 = 730;
const SQUIRREL_MATURE: i64 = 10_000;
const EAGLE_MATURE: i64 = 25_000;

// scene.py
const WIND_CHANCE: f64 = 0.3;
const METEOR_SHOWER_STREAKS: [i64; 6] = [100, 200, 365, 500, 730, 1000];
const PERSEIDS: (u32, u32) = (8, 12);
const DAYS_PER_YEAR: i64 = 365;
const SYNODIC_MONTH: f64 = 29.530588853;
// 2000-01-06 18:14 UTC, a new moon
const KNOWN_NEW_MOON: i64 = 947_182_440;

/// When the forest is worked out: the collection's day and the clock.
#[derive(Clone, Copy)]
pub struct Moment {
    /// When Anki's day ends (the scheduler's day_cutoff).
    pub cutoff: i64,
    /// Anki's day number (sched.today).
    pub today: i64,
    /// Now, in seconds.
    pub now: f64,
}

/// The scenery the forest is drawn in, as a preset has it.
pub struct Look {
    pub environment: String,
    pub landscape: String,
    pub landmark: String,
    pub weather: String,
    pub time: String,
}

/// The page's data (web/core.js AF.mount's `data`) for the whole collection.
pub fn page(col: &mut Collection, at: Moment, look: &Look) -> Result<Value> {
    let rows = load_rows(col, at.cutoff)?;
    let forest = build_forest(&rows, at);
    let mut trees = mark_big_days(&forest.trees);
    let cured = load_cured(col, at.cutoff, CURED_DAYS)?;
    for t in trees.iter_mut() {
        if let Some(n) = cured.get(&ago(t)) {
            t.insert("cured".into(), json!(n));
        }
        // Peaceful: no crows (events.calm_trees)
        t.remove("leeches");
    }
    let today = day_date(0, at.cutoff);
    let anniversaries_all = anniversaries(&trees, today);
    let (kept, merged) = merge_old(trees);
    let merged_count = forest.stats_trees as usize - kept.len();
    let anniversaries: Vec<usize> = anniversaries_all
        .iter()
        .filter(|&&i| i >= merged_count)
        .map(|&i| i - merged_count)
        .collect();
    let free = load_free_days(col, at.cutoff)?;
    let (streak, longest) = streaks(&rows.review_days, &free);
    let mut stats = forest.stats.clone();
    stats.insert("streak".into(), json!(streak));
    stats.insert("longest_streak".into(), json!(longest));
    stats.insert("streak_week".into(), json!(7));
    let now_local = local(at.now as i64);
    let page_trees: Vec<Value> = kept
        .into_iter()
        .map(|mut t| {
            // (the page needs neither: payload.PAGE_LEAVES_OUT)
            t.remove("mature");
            t.remove("day");
            Value::Object(t)
        })
        .collect();
    Ok(json!({
        "trees": page_trees,
        "stats": stats,
        "visitors": forest.visitors,
        "anniversaries": anniversaries,
        "mood": mood(look, now_local, at.now),
        "events": scene_events(&forest.stats, today),
        "merged": merged,
        "forestSeed": forest.seed,
        "dayNumber": today.num_days_from_ce(),
        "testForest": false,
        "animations": true,
        "tooltips": true,
        "nature": "peaceful",
        "craters": [],
        "strike": null,
        "doom": null,
        "fire": null,
        "stagnation": 0.0,
    }))
}

/// What the forest is worked out from, in short: the same until the day, the
/// scenery, or the collection's reviews, cards or notes change.
pub fn key(col: &Collection, at: Moment, look: &Look) -> Result<String> {
    let db = col.storage.db();
    let (reviews, newest): (i64, Option<i64>) =
        db.query_row("select count(), max(id) from revlog", [], |r| Ok((r.get(0)?, r.get(1)?)))?;
    let (cards, cards_mod): (i64, Option<i64>) =
        db.query_row("select count(), max(mod) from cards", [], |r| Ok((r.get(0)?, r.get(1)?)))?;
    let notes_mod: Option<i64> = db.query_row("select max(mod) from notes", [], |r| r.get(0))?;
    Ok(format!(
        "{} {} {} {} {} {} | {reviews} {newest:?} {cards} {cards_mod:?} {notes_mod:?}",
        at.cutoff, look.environment, look.landscape, look.landmark, look.weather, look.time
    ))
}

/// The moment the collection is at now.
pub fn now(col: &mut Collection) -> Result<Moment> {
    let timing = col.timing_today()?;
    Ok(Moment {
        cutoff: timing.next_day_at.0,
        today: timing.days_elapsed as i64,
        now: std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs_f64())
            .unwrap_or(0.0),
    })
}

// ---------- the collection (study_log.py) ----------

struct Rows {
    cards: Vec<(i64, i64, i64, i64, String)>, // (cid, type, queue, ivl, data)
    first_last: HashMap<i64, (i64, i64)>,     // cid -> (first review ms, last review ms)
    recent_lapses: HashSet<i64>,
    review_days: HashSet<i64>,
    total_reviews: i64,
    today_reviews: i64,
    leeches: HashSet<i64>,
}

fn load_rows(col: &Collection, cutoff: i64) -> Result<Rows> {
    let db = col.storage.db();
    let mut cards = Vec::new();
    {
        let mut st = db.prepare("select c.id, c.type, c.queue, c.ivl, c.data from cards c where c.type != 0")?;
        let mut rows = st.query([])?;
        while let Some(r) = rows.next()? {
            cards.push((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?, r.get::<_, Option<String>>(4)?.unwrap_or_default()));
        }
    }
    let mut first_last = HashMap::new();
    {
        let mut st = db.prepare(&format!(
            "select cid, min(id), max(id) from revlog not indexed where ease > 0 and type in {STUDY_TYPES} group by cid"
        ))?;
        let mut rows = st.query([])?;
        while let Some(r) = rows.next()? {
            first_last.insert(r.get(0)?, (r.get(1)?, r.get(2)?));
        }
    }
    let mut day_reviews: HashMap<i64, i64> = HashMap::new();
    {
        let mut st = db.prepare(&format!(
            "select (?1 - 1 - id / 1000) / {DAY_SECS}, count() from revlog \
             where ease > 0 and type in {STUDY_TYPES} and id < ?2 group by 1"
        ))?;
        let mut rows = st.query([cutoff, cutoff * 1000])?;
        while let Some(r) = rows.next()? {
            day_reviews.insert(r.get(0)?, r.get(1)?);
        }
    }
    let lapse_since = (cutoff - LAPSE_WINDOW_DAYS * DAY_SECS) * 1000;
    let mut recent_lapses = HashSet::new();
    {
        let mut st = db.prepare("select distinct cid from revlog where type = 1 and ease = 1 and id > ?1")?;
        let mut rows = st.query([lapse_since])?;
        while let Some(r) = rows.next()? {
            recent_lapses.insert(r.get(0)?);
        }
    }
    let active_ms = (cutoff - ACTIVE_DAYS * DAY_SECS) * 1000;
    let mut leeches = HashSet::new();
    {
        let mut st = db.prepare(&format!(
            "select c.id from cards c join notes n on n.id = c.nid where lower(n.tags) like '% leech %' and c.ivl < {CURED_IVL}"
        ))?;
        let mut rows = st.query([])?;
        while let Some(r) = rows.next()? {
            let cid: i64 = r.get(0)?;
            if first_last.get(&cid).is_some_and(|&(_, last)| last >= active_ms) {
                leeches.insert(cid);
            }
        }
    }
    Ok(Rows {
        cards,
        first_last,
        recent_lapses,
        total_reviews: day_reviews.values().sum(),
        today_reviews: day_reviews.get(&0).copied().unwrap_or(0),
        review_days: day_reviews.keys().copied().collect(),
        leeches,
    })
}

/// The leeches cured within the last `days` days and still cured, by their tree's day.
fn load_cured(col: &Collection, cutoff: i64, days: i64) -> Result<HashMap<i64, i64>> {
    let db = col.storage.db();
    let since_ms = (cutoff - days * DAY_SECS) * 1000;
    let mut st = db.prepare(&format!(
        "select c.id, (select min(r.id) from revlog r where r.cid = c.id and r.ease > 0 and r.type in {STUDY_TYPES}) \
         from cards c join notes n on n.id = c.nid \
         where lower(n.tags) like '% leech %' and c.queue != -1 and c.ivl >= {CURED_IVL} \
         and exists (select 1 from revlog r where r.cid = c.id and r.ivl >= {CURED_IVL} \
         and r.lastIvl < {CURED_IVL} and r.id >= ?1)"
    ))?;
    let mut out = HashMap::new();
    let mut rows = st.query([since_ms])?;
    while let Some(r) = rows.next()? {
        let Some(first) = r.get::<_, Option<i64>>(1)? else { continue };
        let d = days_ago(first as f64 / 1000.0, cutoff);
        if d >= 0 {
            *out.entry(d).or_insert(0) += 1;
        }
    }
    Ok(out)
}

/// The days with no review and nothing due, which a streak passes over
/// (study_log.load_free_days, for the whole collection).
fn load_free_days(col: &Collection, cutoff: i64) -> Result<HashSet<i64>> {
    let db = col.storage.db();
    let day = format!("(({cutoff} - 1 - id / 1000) / {DAY_SECS})");
    let spans = format!(
        "select cid, {day} as d, case when ivl > 0 then {day} - ivl \
         when ivl < 0 then ({cutoff} - 1 - id / 1000 + ivl) / {DAY_SECS} end as due_on, \
         lead({day}) over (partition by cid order by id) as next from revlog not indexed where id < {}",
        cutoff * 1000
    );
    let oldest: Option<i64> =
        db.query_row(&format!("select max({day}) from revlog where id < ?1"), [cutoff * 1000], |r| r.get(0))?;
    let Some(oldest) = oldest else { return Ok(HashSet::new()) };
    let mut studied = HashSet::new();
    {
        let mut st = db.prepare(&format!(
            "select distinct {day} from revlog where ease > 0 and type in {STUDY_TYPES} and id < ?1"
        ))?;
        let mut rows = st.query([cutoff * 1000])?;
        while let Some(r) = rows.next()? {
            studied.insert(r.get::<_, i64>(0)?);
        }
    }
    let ranges = format!(
        "select min(due_on, {oldest}) as older, case when next is not null then next + 1 \
         when cid in (select id from cards where queue in (1, 2, 3)) then 0 else -1 end as newer from ({spans}) \
         where due_on is not null"
    );
    let mut due = vec![0i64; oldest as usize + 2];
    let mut after: Vec<i64> = studied.iter().copied().collect();
    after.sort();
    let mut st = db.prepare(&format!(
        "select older, newer, count() from ({ranges}) where older >= max(newer, 0) group by 1, 2"
    ))?;
    let mut rows = st.query([])?;
    while let Some(r) = rows.next()? {
        let older: i64 = r.get(0)?;
        let mut newer: i64 = r.get(1)?;
        let n: i64 = r.get(2)?;
        if newer < 0 {
            // due until the first day studied on or after its due day
            let at = after.partition_point(|&d| d <= older);
            newer = if at > 0 { after[at - 1] + 1 } else { 0 };
            if newer > older {
                continue;
            }
        }
        due[newer as usize] += n;
        due[older as usize + 1] -= n;
    }
    let mut free = HashSet::new();
    let mut level = 0;
    for d in 0..=oldest {
        level += due[d as usize];
        if !studied.contains(&d) && level <= 0 {
            free.insert(d);
        }
    }
    Ok(free)
}

/// 0 for today's Anki day, 1 for yesterday, ... (by the rollover hour).
fn days_ago(ts: f64, cutoff: i64) -> i64 {
    ((cutoff as f64 - 1.0 - ts) / DAY_SECS as f64).floor() as i64
}

fn local(ts: i64) -> chrono::DateTime<chrono::Local> {
    chrono::Local.timestamp_opt(ts, 0).single().unwrap_or_else(chrono::Local::now)
}

/// The calendar date of an Anki day.
fn day_date(days: i64, cutoff: i64) -> NaiveDate {
    local(cutoff - days * DAY_SECS - DAY_SECS / 2).date_naive()
}

// ---------- how well cards are remembered (memory.py) ----------

fn number_after(re: &Regex, data: &str) -> Option<f64> {
    re.captures(data).and_then(|c| c[1].parse::<f64>().ok())
}

fn stability(data: &str) -> Option<f64> {
    static RE: std::sync::OnceLock<Regex> = std::sync::OnceLock::new();
    if data.is_empty() {
        return None;
    }
    let re = RE.get_or_init(|| Regex::new(r#""s"\s*:\s*(-?[0-9.]+(?:[eE][-+]?\d+)?)"#).unwrap());
    if re.is_match(data) {
        return number_after(re, data).filter(|&s| s > 0.0);
    }
    serde_json::from_str::<Value>(data)
        .ok()
        .and_then(|v| v.get("s").and_then(Value::as_f64))
        .filter(|&s| s > 0.0)
}

fn retrievability(stability: f64, elapsed: f64, data: &str) -> f64 {
    static RE: std::sync::OnceLock<Regex> = std::sync::OnceLock::new();
    let re = RE.get_or_init(|| Regex::new(r#""decay"\s*:\s*([0-9.]+(?:[eE][-+]?\d+)?)"#).unwrap());
    let decay = number_after(re, data).filter(|&d| d > 0.0 && d < 1.0).unwrap_or(DEFAULT_DECAY);
    let factor = STABILITY_RECALL.powf(-1.0 / decay) - 1.0;
    (1.0 + factor * elapsed / stability).powf(-decay)
}

fn median(values: &[f64]) -> f64 {
    let mut v = values.to_vec();
    v.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let mid = v.len() / 2;
    if v.len() % 2 == 1 {
        v[mid]
    } else {
        (v[mid - 1] + v[mid]) / 2.0
    }
}

/// Python's round(x, digits): by the decimal value the float really holds,
/// halves to even. (Scaling it first turned 870.549999... into a tie.)
fn round_to(x: f64, digits: i32) -> f64 {
    format!("{:.*}", digits.max(0) as usize, x).parse().unwrap_or(x)
}

// ---------- trees (forest_data.py) ----------

type Tree = Map<String, Value>;

fn ago(t: &Tree) -> i64 {
    t["ago"].as_i64().unwrap_or(0)
}

fn seed(day: i64) -> i64 {
    let digest = Sha1::digest(format!("anki-forest-{day}").as_bytes());
    (u32::from_be_bytes([digest[0], digest[1], digest[2], digest[3]]) & 0x7FFF_FFFF) as i64
}

#[allow(clippy::too_many_arguments)]
fn make_tree(day: i64, ago: i64, date: NaiveDate, n: i64, stage: i64, health: i64, remembered: f64, strength: f64,
             struggling: i64, measured: bool) -> Tree {
    let seed = seed(day);
    let mut t = Map::new();
    t.insert("day".into(), json!(day));
    t.insert("ago".into(), json!(ago));
    t.insert("date".into(), json!(date.format("%Y-%m-%d").to_string()));
    t.insert("n".into(), json!(n));
    t.insert("stage".into(), json!(stage));
    let size = if n < CROWN_SIZE_CARDS.0 { 0 } else if n < CROWN_SIZE_CARDS.1 { 1 } else { 2 };
    t.insert("size".into(), json!(size));
    t.insert("health".into(), json!(health));
    t.insert("kind".into(), json!(if (seed % 100) < PINE_PERCENT as i64 { 1 } else { 0 }));
    t.insert("seed".into(), json!(seed));
    t.insert("remembered".into(), json!(round_to(remembered, 3)));
    t.insert("strength".into(), json!(round_to(strength, 1)));
    t.insert("struggling".into(), json!(struggling));
    t.insert("measured".into(), json!(measured));
    t
}

fn stage_of(strengths: &[f64], learning_share: f64, is_today: bool) -> i64 {
    if is_today {
        return SEEDLING;
    }
    if strengths.is_empty() || learning_share > SAPLING_LEARNING_SHARE {
        return SAPLING;
    }
    let m = median(strengths);
    if m >= ANCIENT_DAYS {
        ANCIENT
    } else if m >= OLD_DAYS {
        OLD
    } else if m >= MATURE_DAYS {
        MATURE
    } else {
        YOUNG
    }
}

fn health_of(struggling_share: f64) -> i64 {
    HEALTH_THRESHOLDS.iter().filter(|&&t| struggling_share >= t).count() as i64
}

/// (current streak, longest streak) in days studied; a streak survives until
/// today ends, and over `free` days, which it passes without growing.
fn streaks(review_days: &HashSet<i64>, free: &HashSet<i64>) -> (i64, i64) {
    if review_days.is_empty() {
        return (0, 0);
    }
    let (mut current, mut d) = (0, 0);
    while review_days.contains(&d) || free.contains(&d) || d == 0 {
        current += review_days.contains(&d) as i64;
        d += 1;
    }
    let (mut longest, mut run) = (0, 0);
    let max = *review_days.iter().max().unwrap();
    for d in (0..=max).rev() {
        run = if review_days.contains(&d) {
            run + 1
        } else if free.contains(&d) {
            run
        } else {
            0
        };
        longest = longest.max(run);
    }
    (current, longest)
}

/// (ago of the day that ended the break, its length) for each stretch of at
/// least BREAK_DAYS days without reviews, oldest first.
fn breaks(review_days: &HashSet<i64>, oldest: i64) -> Vec<(i64, i64)> {
    let mut out = Vec::new();
    let mut gap = 0;
    for d in (0..=oldest).rev() {
        if review_days.contains(&d) {
            if gap >= BREAK_DAYS {
                out.push((d, gap));
            }
            gap = 0;
        } else {
            gap += 1;
        }
    }
    out
}

/// Mark each break on the first tree planted after it (forest_data._ponds).
fn ponds(trees: &mut [Tree], breaks: &[(i64, i64)], cutoff: i64) {
    let mut ponds: Vec<[i64; 4]> = Vec::new(); // tree after it, days away, began, ended
    for &(resumed, length) in breaks {
        let i = trees.iter().position(|t| ago(t) <= resumed).unwrap_or(trees.len()) as i64;
        if let Some(last) = ponds.last_mut().filter(|p| i <= p[0] + 1) {
            last[1] += length;
            last[3] = resumed + 1;
        } else {
            ponds.push([i, length, resumed + length, resumed + 1]);
        }
    }
    let date = |d: i64| day_date(d, cutoff).format("%Y-%m-%d").to_string();
    for [i, days, began, ended] in ponds {
        let n = trees.len();
        let t = &mut trees[(i as usize).min(n - 1)];
        t.insert("gap".into(), json!(days));
        t.insert("gap_from".into(), json!(date(began)));
        t.insert("gap_to".into(), json!(date(ended)));
    }
}

struct Forest {
    trees: Vec<Tree>,
    stats: Map<String, Value>,
    stats_trees: i64,
    visitors: Vec<Value>,
    seed: i64,
}

#[derive(Default)]
struct Cohort {
    n: i64,
    learning: i64,
    strengths: Vec<f64>,
    struggling: i64,
    r: Vec<f64>,
    suspended: i64,
}

fn build_forest(rows: &Rows, at: Moment) -> Forest {
    let mut cohorts: BTreeMap<i64, Cohort> = BTreeMap::new();
    for (cid, ctype, queue, ivl, data) in &rows.cards {
        let first_last = rows.first_last.get(cid);
        let first_ms = first_last.map_or(*cid, |fl| fl.0);
        let d = days_ago(first_ms as f64 / 1000.0, at.cutoff);
        if d < 0 {
            continue;
        }
        let c = cohorts.entry(d).or_default();
        c.n += 1;
        let stab = stability(data);
        if *ctype == 1 {
            c.learning += 1;
        } else {
            c.strengths.push(stab.unwrap_or((*ivl).max(0) as f64));
        }
        if *queue == -1 {
            c.suspended += 1;
            continue;
        }
        if *ctype == 3 || rows.recent_lapses.contains(cid) {
            c.struggling += 1;
        }
        if let (Some(stab), Some(fl)) = (stab, first_last) {
            let elapsed = ((at.now - fl.1 as f64 / 1000.0) / DAY_SECS as f64).max(0.0);
            c.r.push(retrievability(stab, elapsed, data));
        }
    }
    let mut leeches: HashMap<i64, i64> = HashMap::new();
    for cid in &rows.leeches {
        let first = rows.first_last.get(cid).map_or(*cid, |fl| fl.0);
        let d = days_ago(first as f64 / 1000.0, at.cutoff);
        if d >= 0 {
            *leeches.entry(d).or_insert(0) += 1;
        }
    }

    let mut trees = Vec::new();
    for (&d, c) in cohorts.iter().rev() {
        // oldest first
        let n = c.n;
        let active = n - c.suspended;
        let stage = stage_of(&c.strengths, c.learning as f64 / n as f64, d == 0);
        let health = if active > 0 && stage >= HEALTH_MIN_STAGE {
            health_of(c.struggling as f64 / active as f64)
        } else {
            0
        };
        let strength = if c.strengths.is_empty() { 0.0 } else { round_to(median(&c.strengths), 1) };
        let measured = active > 0 && c.r.len() as f64 >= active as f64 / 2.0;
        let remembered = if !c.r.is_empty() {
            c.r.iter().sum::<f64>() / c.r.len() as f64
        } else if active > 0 {
            1.0 - c.struggling as f64 / active as f64
        } else {
            1.0
        };
        let mut t = make_tree(at.today - d, d, day_date(d, at.cutoff), n, stage, health, remembered, strength,
                              c.struggling, measured);
        t.insert("mature".into(), json!(c.strengths.iter().filter(|&&s| s >= MATURE_DAYS).count()));
        if c.suspended > 0 {
            t.insert("suspended".into(), json!(c.suspended));
        }
        if let Some(&l) = leeches.get(&d).filter(|&&l| l > 0) {
            t.insert("leeches".into(), json!(l));
        }
        trees.push(t);
    }
    if let Some(oldest) = trees.first().map(ago) {
        let b = breaks(&rows.review_days, oldest);
        ponds(&mut trees, &b, at.cutoff);
    }
    let (current, longest) = streaks(&rows.review_days, &HashSet::new());
    let stats = make_stats(&trees, current, longest, rows.total_reviews, rows.today_reviews);
    let visitors = visitors(&stats);
    Forest {
        stats_trees: trees.len() as i64,
        seed: trees.first().map_or(0, |t| t["seed"].as_i64().unwrap_or(0)),
        trees,
        stats,
        visitors,
    }
}

fn make_stats(trees: &[Tree], streak: i64, longest: i64, reviews: i64, today_reviews: i64) -> Map<String, Value> {
    let count = |f: &dyn Fn(&Tree) -> bool| trees.iter().filter(|t| f(t)).count();
    let stage = |t: &Tree| t["stage"].as_i64().unwrap_or(0);
    let int = |t: &Tree, k: &str| t.get(k).and_then(Value::as_i64).unwrap_or(0);
    let last = trees.last();
    let planted_today = last.is_some_and(|t| ago(t) == 0);
    let mut s = Map::new();
    s.insert("trees".into(), json!(trees.len()));
    s.insert("cards".into(), json!(trees.iter().map(|t| int(t, "n")).sum::<i64>()));
    s.insert("ancient".into(), json!(count(&|t| stage(t) == ANCIENT)));
    s.insert("old".into(), json!(count(&|t| stage(t) == OLD)));
    s.insert("young".into(), json!(count(&|t| stage(t) == YOUNG)));
    s.insert("yellowing".into(), json!(count(&|t| int(t, "health") > 0)));
    s.insert("streak".into(), json!(streak));
    s.insert("longest_streak".into(), json!(longest));
    s.insert("reviews".into(), json!(reviews));
    s.insert("today_reviews".into(), json!(today_reviews));
    s.insert("forest_age".into(), json!(trees.first().map_or(0, ago)));
    s.insert("oldest_date".into(), trees.first().map_or(Value::Null, |t| t["date"].clone()));
    s.insert("planted_today".into(), json!(planted_today));
    s.insert("today_cards".into(), json!(if planted_today { int(last.unwrap(), "n") } else { 0 }));
    s.insert("ponds".into(), json!(count(&|t| t.get("gap").is_some())));
    s.insert("mature_cards".into(), json!(trees.iter().map(|t| int(t, "mature")).sum::<i64>()));
    s
}

/// Everything older than the newest MAX_INDIVIDUAL_TREES trees, as one summary
/// the page draws as the deep forest.
fn merge_old(trees: Vec<Tree>) -> (Vec<Tree>, Value) {
    if trees.len() <= MAX_INDIVIDUAL_TREES {
        return (trees, Value::Null);
    }
    let mut trees = trees;
    let kept = trees.split_off(trees.len() - MAX_INDIVIDUAL_TREES);
    let old = trees;
    let merged = json!({
        "count": old.len(),
        "cards": old.iter().map(|t| t["n"].as_i64().unwrap_or(0)).sum::<i64>(),
        "ancient": old.iter().filter(|t| t["stage"].as_i64() == Some(ANCIENT)).count(),
        "ponds": old.iter().filter(|t| t.get("gap").is_some()).count(),
        "from_date": old[0]["date"],
        "to_date": old[old.len() - 1]["date"],
        "from_ago": ago(&old[0]),
        "to_ago": ago(&old[old.len() - 1]),
    });
    (kept, merged)
}

// ---------- events (events.py, Peaceful) ----------

/// The trees, each whose day was a big jump in new cards marked `big` (what it beat).
fn mark_big_days(trees: &[Tree]) -> Vec<Tree> {
    let mut out = Vec::with_capacity(trees.len());
    for (i, t) in trees.iter().enumerate() {
        let mut t = t.clone();
        t.remove("big");
        if i >= BIG_DAY_HISTORY {
            let mut best = 0;
            let mut j = i as i64 - 1;
            while j >= 0 && ago(&trees[j as usize]) <= ago(&t) + BIG_DAY_WINDOW {
                best = best.max(trees[j as usize]["n"].as_i64().unwrap_or(0));
                j -= 1;
            }
            let n = t["n"].as_i64().unwrap_or(0);
            if best > 0 && n as f64 >= best as f64 * BIG_DAY_JUMP && n >= best + BIG_DAY_MORE {
                t.insert("big".into(), json!(best));
            }
        }
        out.push(t);
    }
    out
}

// ---------- animals and anniversaries (milestones.py) ----------

fn visitors(s: &Map<String, Value>) -> Vec<Value> {
    let int = |k: &str| s.get(k).and_then(Value::as_i64).unwrap_or(0);
    let planted_today = s.get("planted_today").and_then(Value::as_bool).unwrap_or(false);
    let (streak, longest) = (int("streak"), int("longest_streak"));
    let came = [
        ("rabbit", int("trees") >= RABBIT_TREES, int("trees") == RABBIT_TREES && planted_today),
        ("deer", longest >= DEER_STREAK, streak == DEER_STREAK && longest == DEER_STREAK),
        ("fox", int("reviews") >= FOX_REVIEWS, int("reviews") - int("today_reviews") < FOX_REVIEWS && FOX_REVIEWS <= int("reviews")),
        ("owl", int("ancient") >= 1, false),
        ("heron", int("ponds") >= 1, false),
        ("stag", longest >= STAG_STREAK, streak == STAG_STREAK && longest == STAG_STREAK),
        ("cabin", int("forest_age") >= CABIN_AGE_DAYS, int("forest_age") == CABIN_AGE_DAYS),
        ("bear", longest >= BEAR_STREAK, streak == BEAR_STREAK && longest == BEAR_STREAK),
        ("squirrel", int("mature_cards") >= SQUIRREL_MATURE, false),
        ("eagle", int("mature_cards") >= EAGLE_MATURE, false),
    ];
    came.iter()
        .filter(|(_, here, _)| *here)
        .map(|(key, _, new)| json!({ "key": key, "new": new }))
        .collect()
}

/// Indexes of trees planted on this calendar day in an earlier year (29 Feb
/// on 28 Feb in years without one).
fn anniversaries(trees: &[Tree], on: NaiveDate) -> Vec<usize> {
    let leapless = on.month() == 2 && on.day() == 28 && NaiveDate::from_ymd_opt(on.year(), 2, 29).is_none();
    trees
        .iter()
        .enumerate()
        .filter_map(|(i, t)| {
            let d = NaiveDate::parse_from_str(t["date"].as_str()?, "%Y-%m-%d").ok()?;
            let same = (d.month(), d.day()) == (on.month(), on.day());
            let leap = leapless && (d.month(), d.day()) == (2, 29);
            (d.year() < on.year() && (same || leap)).then_some(i)
        })
        .collect()
}

// ---------- the scene (scene.py) ----------

/// A deterministic 0..1 value for a date, so choices hold all day.
fn roll(date: NaiveDate, salt: &str) -> f64 {
    let digest = Sha1::digest(format!("{}|{salt}", date.format("%Y-%m-%d")).as_bytes());
    u32::from_be_bytes([digest[0], digest[1], digest[2], digest[3]]) as f64 / 4_294_967_296.0
}

fn moon_phase(now: f64) -> f64 {
    let days = (now - KNOWN_NEW_MOON as f64) / DAY_SECS as f64;
    (days / SYNODIC_MONTH).rem_euclid(1.0)
}

fn mood(look: &Look, now: chrono::DateTime<chrono::Local>, now_ts: f64) -> Value {
    let date = now.date_naive();
    let windy = matches!(look.weather.as_str(), "cloudy" | "rain" | "storm") && roll(date, "wind") < WIND_CHANCE;
    json!({
        "time": look.time,
        "home": look.time,
        "clock": false,
        "weather": look.weather,
        "special": look.environment,
        "wind": windy,
        "environment": look.environment,
        "landscape": look.landscape,
        "landmark": look.landmark,
        "moon": round_to(moon_phase(now_ts), 3),
        "source": "manual",
    })
}

/// Night-sky events tied to your study (they only show on clear nights).
fn scene_events(stats: &Map<String, Value>, today: NaiveDate) -> Vec<&'static str> {
    let mut out = Vec::new();
    let streak = stats.get("streak").and_then(Value::as_i64).unwrap_or(0);
    if METEOR_SHOWER_STREAKS.contains(&streak) || (today.month(), today.day()) == PERSEIDS {
        out.push("meteor_shower");
    }
    let age = stats.get("forest_age").and_then(Value::as_i64).unwrap_or(0);
    if age > 0 && age % DAYS_PER_YEAR == 0 {
        out.push("harvest_moon");
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn days(v: &[i64]) -> HashSet<i64> {
        v.iter().copied().collect()
    }

    #[test]
    fn streaks_pass_over_today_and_free_days() {
        // today not studied yet: yesterday's streak still stands
        assert_eq!(streaks(&days(&[1, 2, 3, 7]), &HashSet::new()), (3, 3));
        // a free day (nothing due) neither breaks nor grows it
        assert_eq!(streaks(&days(&[0, 1, 3]), &days(&[2])), (3, 3));
        assert_eq!(streaks(&HashSet::new(), &HashSet::new()), (0, 0));
    }

    #[test]
    fn breaks_and_ponds() {
        // away from day 20 to day 11 (10 days), back on day 10
        let review: HashSet<i64> = (0..=10).chain(21..=30).collect();
        assert_eq!(breaks(&review, 30), vec![(10, 10)]);
    }

    #[test]
    fn stages_and_health() {
        assert_eq!(stage_of(&[400.0, 380.0, 10.0], 0.0, false), ANCIENT);
        assert_eq!(stage_of(&[30.0], 0.0, false), MATURE);
        assert_eq!(stage_of(&[30.0], 0.6, false), SAPLING);
        assert_eq!(stage_of(&[30.0], 0.0, true), SEEDLING);
        assert_eq!(health_of(0.3), 2);
        assert_eq!(median(&[1.0, 3.0, 2.0, 10.0]), 2.5);
    }

    #[test]
    fn rounding_and_seeds_match_python() {
        assert_eq!(round_to(0.8125, 3), 0.812); // Python: round(0.8125, 3) == 0.812
        assert_eq!(round_to(2.25, 1), 2.2);
        assert_eq!(round_to(870.55, 1), 870.5); // 870.549999...
        assert_eq!(round_to(0.125, 2), 0.12);
        // hashlib.sha1(b"anki-forest-100").digest()[:4], big-endian, & 0x7FFFFFFF
        assert_eq!(seed(100), {
            let d = Sha1::digest(b"anki-forest-100");
            (u32::from_be_bytes([d[0], d[1], d[2], d[3]]) & 0x7FFF_FFFF) as i64
        });
    }

    #[test]
    fn memory_curve() {
        // recall is 90% after `stability` days, on the default curve
        assert!((retrievability(10.0, 10.0, "") - 0.9).abs() < 1e-9);
        assert_eq!(stability(r#"{"s":12.5,"d":5.0}"#), Some(12.5));
        assert_eq!(stability(""), None);
    }
}
