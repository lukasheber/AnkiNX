//! The forest Memory Forest grows from a collection (app/forest): one tree a
//! day of new cards, grown by how well they are remembered, with its ponds,
//! animals and anniversaries, what the way you study brings to it (Nature:
//! fire, the asteroid, crows, tall grass, tumbleweeds, flowers, songbirds),
//! and the scene it is drawn in. A port of the add-on's study_log.py,
//! forest_data.py, memory.py, milestones.py, events.py, events_state.py and
//! scene.py's mood, at the commit pinned in app/library/PINS; names follow
//! theirs. Days are counted as `ago` (0 today, 1 yesterday, ...), as there.
//!
//! The add-on's defaults throughout: every deck, suspended cards keep their
//! trees, no start date. What it remembers between days (the animals that
//! came and when, the ancient trees, what was shown) is the caller's
//! `memory`, kept as its state file is.

use std::collections::BTreeMap;
use std::collections::HashMap;
use std::collections::HashSet;

use anki::collection::Collection;
use anki::error::Result;
use chrono::DateTime;
use chrono::Datelike;
use chrono::NaiveDate;
use chrono::TimeZone;
use chrono::Utc;
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

// events.py: big learning days, cured leeches' songbirds, Nature's strikes
// and fire, tall grass, review hell
const BIG_DAY_WINDOW: i64 = 14;
const BIG_DAY_JUMP: f64 = 1.25;
const BIG_DAY_MORE: i64 = 2;
const BIG_DAY_HISTORY: usize = 7;
const CURED_DAYS: i64 = 7;
const LOOKBACK_DAYS: i64 = 3660;
const STRIKE_NEWS_DAYS: i64 = 7;
const CRATER_GONE_DAYS: i64 = 400;
const FIRE_FROM: i64 = 3;
const FIRE_PER_DAY: f64 = 0.05;
const FIRE_MAX: f64 = 0.5;
const FIRE_HEAL_DAYS: i64 = 7;
const STAGNANT_AFTER: i64 = 7;
const STAGNANT_FULL: i64 = 30;
const STAGNANT_REVIEWING: i64 = 3;
const REVIEW_HELL_TIMES: i64 = 2;
const REVIEW_HELL_MIN: i64 = 30;
const REVIEW_HELL_USUAL_DAYS: i64 = 30;
const BACKLOG_CLEARED_WITHIN: i64 = 7;

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
// Without the place's own, the sun rises and sets at these local times; dawn and
// dusk last this long either side, golden hour as long before dusk.
const DEFAULT_SUNRISE: (u32, u32) = (6, 30);
const DEFAULT_SUNSET: (u32, u32) = (19, 0);
const TWILIGHT_MINUTES: i64 = 60;
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
    /// The real sky: the weather is the live weather (`live`, the preset's own
    /// without it) and the time of day follows the clock.
    pub real_sky: bool,
    /// The live weather now (weather::current), and the place it is for.
    pub live: Option<Value>,
    pub city: Option<String>,
}

/// What the forest is grown with, besides the collection.
pub struct Options {
    /// "peaceful", "wild" or "merciless" (events.py's Nature).
    pub nature: String,
    /// The review log is all there: this session's sync has finished, or the
    /// collection doesn't sync. Until then a day studied on another device
    /// looks missed, so no strike, fire, smoke or doom that hasn't shown yet
    /// comes (events_state.settled).
    pub settled: bool,
    /// The heatmap's weeks start on this day (Sunday 0, Monday 1).
    pub week_start: i64,
}

/// The page's data (web/core.js AF.mount's `data`) for the whole collection,
/// as payload.payload makes it. `memory` is what the add-on keeps in its
/// state file (the animals that came, the ancient trees, what was shown):
/// read and updated here, saved by the caller.
pub fn page(col: &mut Collection, at: Moment, look: &Look, opts: &Options, memory: &mut Value) -> Result<Value> {
    if !memory.is_object() {
        *memory = json!({});
    }
    let rows = load_rows(col, at.cutoff)?;
    let forest = build_forest(&rows, at);
    let level = nature_level(&opts.nature);
    let days = &rows.review_days;
    let date = |d: i64| iso(day_date(d, at.cutoff));
    let mut trees = forest.trees;
    let mut stats = forest.stats;

    // Nature (events_state.apply)
    let mut extras = Map::new();
    extras.insert("nature".into(), json!(level));
    extras.insert("craters".into(), json!([]));
    extras.insert("strike".into(), Value::Null);
    extras.insert("doom".into(), Value::Null);
    extras.insert("fire".into(), Value::Null);
    let mut struck = None;
    if level == "merciless" {
        let held = held(days, opts.settled, memory, &date);
        let out = merciless(&trees, days, &held);
        if let (Some(&last), true) = (out.hits.last(), opts.settled) {
            remember_shown(memory, "strike", &date(last));
        }
        if let Some(latest) = &out.latest {
            let craters: Vec<Value> = out
                .craters
                .iter()
                .map(|c| json!({ "ago": c.ago, "lost": c.lost, "streak": c.streak, "date": date(c.ago) }))
                .collect();
            extras.insert("craters".into(), json!(craters));
            // it plays by itself once, while it is news and hasn't been seen (`fresh`: with the
            // forest it took); the journal tells it until it has, and for the rest of that day
            // (events_state.strike_payload)
            let news = latest.ago <= STRIKE_NEWS_DAYS;
            let seen_key = date(latest.ago);
            let fresh = news && !strike_seen(memory, &seen_key);
            let told = news && (fresh || memory["strike_seen_day"].as_i64() == Some(at.today));
            let mut strike = json!({ "date": date(latest.ago), "lost": latest.lost, "seen": seen_key,
                                     "fresh": fresh, "news": news, "told": told });
            if fresh {
                let (before, merged) = merge_old(latest.before.clone());
                strike["before"] = json!(before);
                strike["merged"] = merged;
            }
            extras.insert("strike".into(), strike);
        }
        if let Some(&hit) = out.hits.last() {
            // the forest now: only what grew since, its streak, reviews and animals counted from there
            struck = Some(at.today - hit);
            let reviews = rows.day_reviews.iter().filter(|(d, _)| **d < hit).map(|(_, n)| n).sum();
            let since: HashSet<i64> = days.iter().filter(|d| **d < hit).copied().collect();
            let (streak, longest) = streaks(&since, &HashSet::new());
            trees = out.trees;
            stats = make_stats(&trees, streak, longest, reviews, rows.today_reviews);
        }
        if out.doom && shows(memory, "doom", &date(0), opts.settled) {
            extras.insert("doom".into(), json!({ "missed": 1, "grace": 1, "left": 0 }));
        }
    } else if level == "wild" {
        let (burning, fire) = set_fire(&trees, days, Some(MAX_INDIVIDUAL_TREES));
        let kind = fire.as_ref().and_then(|f| {
            if f["smoke"].as_i64().unwrap_or(0) > 0 {
                Some("smoke")
            } else if f["trees"].as_i64().unwrap_or(0) > 0 {
                Some("fire")
            } else {
                None
            }
        });
        let shown = match (kind, &fire) {
            (Some(kind), Some(f)) => shows(memory, kind, &date(f["began"].as_i64().unwrap_or(0)), opts.settled),
            _ => true,
        };
        if shown {
            trees = burning;
            extras.insert("fire".into(), fire.unwrap_or(Value::Null));
        }
    }
    // (the first tree standing: after an asteroid, the new forest's)
    let seed = trees.first().map_or(0, |t| t["seed"].as_i64().unwrap_or(0));
    let key = struck.map_or_else(|| "all".to_string(), |s| s.to_string());
    let visitors = animals(memory, &key, &stats, at.today);
    let mut trees = mark_big_days(&trees);
    let mut stagnation = stagnation(&trees, days);
    if stagnation > 0.0 && !has_new_cards(col)? {
        stagnation = 0.0; // every card learned: reviewing alone is what is left to do
    }
    let mut backlog = backlog(col, memory, at)?;
    let cured = load_cured(col, at.cutoff, CURED_DAYS)?;
    for t in trees.iter_mut() {
        if let Some(n) = cured.get(&ago(t)) {
            t.insert("cured".into(), json!(n));
        }
    }
    if level == "peaceful" {
        // only the good things: no crows, tall grass or tumbleweeds (events_state.keep_calm)
        for t in trees.iter_mut() {
            t.remove("leeches");
        }
        stagnation = 0.0;
        backlog.insert("hell".into(), json!(0.0));
    }
    let today = day_date(0, at.cutoff);
    let new_ancient = new_ancient_today(memory, &trees, today);

    let anniversaries_all = anniversaries(&trees, today);
    let mut events = scene_events(&stats, today);
    if new_ancient {
        events.push("new_ancient");
    }
    // the journal: today's news, else the forest's own line - which, for the night sky's
    // events, depends on the sky the page draws (a clear night or not): one for each
    let journal_lines = match news_line(&extras, &backlog) {
        Some(news) => json!({ "starry": news, "plain": news }),
        None => json!({
            "starry": journal(&stats, &visitors, &events, &trees, &anniversaries_all, stagnation, today, true),
            "plain": journal(&stats, &visitors, &events, &trees, &anniversaries_all, stagnation, today, false),
        }),
    };
    // the study heatmap's (payload._calendar, for the whole collection, as the deck list has it)
    let free = load_free_days(col, at.cutoff)?;
    let calendar = calendar(col, at, &rows, &trees, &visitors, &free, opts.week_start)?;
    let all = trees.len();
    let (kept, merged) = merge_old(trees);
    let merged_count = all - kept.len();
    let anniversaries: Vec<usize> = anniversaries_all
        .iter()
        .filter(|&&i| i >= merged_count)
        .map(|&i| i - merged_count)
        .collect();
    // the page's streak: every day you studied, passing over days with nothing due
    let (streak, longest) = streaks(days, &free);
    let mut page_stats = stats;
    page_stats.insert("streak".into(), json!(streak));
    page_stats.insert("longest_streak".into(), json!(longest));
    page_stats.insert("streak_week".into(), json!(7));
    let page_trees: Vec<Value> = kept
        .into_iter()
        .map(|mut t| {
            // (the page needs neither: payload.PAGE_LEAVES_OUT)
            t.remove("mature");
            t.remove("day");
            Value::Object(t)
        })
        .collect();
    let mut out = json!({
        "trees": page_trees,
        "stats": page_stats,
        "visitors": visitors,
        "anniversaries": anniversaries,
        "mood": mood(look, at.now),
        "events": events,
        "merged": merged,
        "forestSeed": seed,
        "dayNumber": today.num_days_from_ce(),
        "testForest": false,
        "animations": true,
        "tooltips": true,
        "stagnation": stagnation,
        "backlog": backlog,
        "journalLines": journal_lines,
        "calendar": calendar,
    });
    out.as_object_mut().unwrap().extend(extras);
    Ok(out)
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
    day_reviews: HashMap<i64, i64>, // days ago -> reviews that day
    day_time: HashMap<i64, i64>,    // days ago -> seconds those reviews took
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
    let (mut day_reviews, mut day_time) = (HashMap::new(), HashMap::new());
    {
        let mut st = db.prepare(&format!(
            "select (?1 - 1 - id / 1000) / {DAY_SECS}, count(), sum(time) from revlog \
             where ease > 0 and type in {STUDY_TYPES} and id < ?2 group by 1"
        ))?;
        let mut rows = st.query([cutoff, cutoff * 1000])?;
        while let Some(r) = rows.next()? {
            let day: i64 = r.get(0)?;
            day_reviews.insert(day, r.get::<_, i64>(1)?);
            day_time.insert(day, r.get::<_, i64>(2)? / 1000);
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
        day_reviews,
        day_time,
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
    Forest { trees, stats }
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

fn nature_level(value: &str) -> &'static str {
    match value {
        "wild" => "wild",
        "merciless" => "merciless",
        _ => "peaceful",
    }
}

fn iso(date: NaiveDate) -> String {
    date.format("%Y-%m-%d").to_string()
}

// What has shown, so it shows again after a restart while the review log is
// still on its way (events_state._shows and friends).

/// The day (ISO) of the last `kind` shown: a strike, a fire, its smoke, a doom.
fn last_shown(memory: &Value, kind: &str) -> Option<String> {
    let shown = memory.get("shown").and_then(|s| s.get(kind)).and_then(Value::as_str);
    let seen = if kind == "strike" { memory.get("strike_seen").and_then(Value::as_str) } else { None };
    [shown, seen]
        .into_iter()
        .flatten()
        .filter(|d| d.starts_with(|c: char| c.is_ascii_digit()))
        .max()
        .map(str::to_string)
}

/// Whether the strike of `key` (its ISO date) has played: it, or a later one.
fn strike_seen(memory: &Value, key: &str) -> bool {
    memory["strike_seen"].as_str().is_some_and(|seen| key <= seen)
}

/// The strike of `key` has played (the app's page has shown it): it plays
/// once, and every strike before it with it (events_state.mark_seen).
pub fn mark_strike_seen(memory: &mut Value, key: &str, today: i64) {
    if !memory.is_object() {
        *memory = json!({});
    }
    if !strike_seen(memory, key) {
        memory["strike_seen"] = json!(key);
    }
    memory["strike_seen_day"] = json!(today);
}

fn remember_shown(memory: &mut Value, kind: &str, day: &str) {
    if memory.get("shown").and_then(|s| s.get(kind)).and_then(Value::as_str) == Some(day) {
        return;
    }
    if !memory.get("shown").is_some_and(Value::is_object) {
        memory["shown"] = json!({});
    }
    memory["shown"][kind] = json!(day);
}

/// Whether `kind` of `day` shows: once the review log is all there it does
/// (and is remembered); before that, only if it showed already.
fn shows(memory: &mut Value, kind: &str, day: &str, settled: bool) -> bool {
    if !settled {
        return last_shown(memory, kind).as_deref() == Some(day);
    }
    remember_shown(memory, kind, day);
    true
}

/// The strikes that don't come yet: until the review log is all there, any
/// newer than the last one shown.
fn held(days: &HashSet<i64>, settled: bool, memory: &Value, date: &dyn Fn(i64) -> String) -> HashSet<i64> {
    if settled {
        return HashSet::new();
    }
    let last = last_shown(memory, "strike");
    strikes(days).into_iter().filter(|&d| last.as_ref().is_none_or(|l| date(d) > *l)).collect()
}

/// (oldest missed day, newest missed day) for each run of days without
/// reviews, oldest first; today is left out, not being over.
fn runs(days: &HashSet<i64>, oldest: i64) -> Vec<(i64, i64)> {
    let (mut out, mut start) = (Vec::new(), None);
    for d in (1..=oldest).rev() {
        if days.contains(&d) {
            if let Some(s) = start.take() {
                out.push((s, d + 1));
            }
        } else if start.is_none() {
            start = Some(d);
        }
    }
    if let Some(s) = start {
        out.push((s, 1));
    }
    out
}

/// The days Merciless's asteroid struck, oldest first: the first day of each
/// run of days without reviews since you first studied.
fn strikes(days: &HashSet<i64>) -> Vec<i64> {
    match days.iter().max() {
        None => vec![],
        Some(&max) => runs(days, max.min(LOOKBACK_DAYS)).into_iter().map(|(start, _)| start).collect(),
    }
}

/// The run of review days that ended just before the break containing `ago`.
fn streak_before(days: &HashSet<i64>, ago: i64) -> i64 {
    let mut d = ago;
    while !days.contains(&d) && d < LOOKBACK_DAYS {
        d += 1;
    }
    let mut run = 0;
    while days.contains(&(d + run)) {
        run += 1;
    }
    run
}

struct Crater {
    ago: i64,
    lost: usize,
    streak: i64,
    /// The trees it took (the latest strike's only, to play it).
    before: Vec<Tree>,
}

struct Strikes {
    hits: Vec<i64>,
    trees: Vec<Tree>,
    craters: Vec<Crater>,
    latest: Option<Crater>,
    doom: bool,
}

/// What Merciless does: a strike for every run of missed days (but those
/// `held`), the forest starting again after the latest; and today, with no
/// reviews yet after a day studied, the asteroid on its way (events.merciless).
fn merciless(trees: &[Tree], days: &HashSet<i64>, held: &HashSet<i64>) -> Strikes {
    let hits: Vec<i64> = strikes(days).into_iter().filter(|d| !held.contains(d)).collect();
    // (events._wipe)
    let (mut craters, mut latest, mut prev): (Vec<Crater>, Option<Crater>, Option<i64>) = (vec![], None, None);
    for &hit in &hits {
        let before: Vec<Tree> =
            trees.iter().filter(|t| ago(t) >= hit && prev.is_none_or(|p| ago(t) < p)).cloned().collect();
        let lost = before.len();
        prev = Some(hit);
        if lost > 0 {
            let streak = streak_before(days, hit);
            if hit <= CRATER_GONE_DAYS {
                craters.push(Crater { ago: hit, lost, streak, before: vec![] });
            }
            latest = Some(Crater { ago: hit, lost, streak, before });
        }
    }
    let mut kept: Vec<Tree> = trees.iter().filter(|t| hits.last().is_none_or(|&h| ago(t) < h)).cloned().collect();
    if !hits.is_empty() {
        // the break that struck is marked by its crater: no pond for it in the new forest
        if let Some(first) = kept.first_mut() {
            first.remove("gap");
        }
    }
    Strikes { doom: !days.contains(&0) && days.contains(&1), hits, trees: kept, craters, latest }
}

struct FireState {
    share: f64,
    healed: i64,
    missed: i64,
    began: Option<i64>,
    epoch: i64,
    out: bool,
    news: bool,
}

/// Wild's fire as the days studied leave it (events.fire_state).
fn fire_state(days: &HashSet<i64>) -> Option<FireState> {
    let max = *days.iter().max()?;
    let (mut share, mut healed, mut run, mut missed, mut began, mut epoch, mut out) = (0.0, 0, 0, 0, None, 0, false);
    for d in (0..=max.min(LOOKBACK_DAYS)).rev() {
        if days.contains(&d) {
            run = 0;
            if share != 0.0 {
                healed += 1;
                if healed >= FIRE_HEAL_DAYS {
                    share = 0.0;
                    out = d == 0;
                }
            }
        } else if d != 0 {
            run += 1;
            out = false;
            if run >= FIRE_FROM {
                if share == 0.0 {
                    let b = d + run - 1;
                    began = Some(b);
                    epoch = days.iter().filter(|&&x| x > b).count() as i64;
                    missed = 0;
                }
                share = f64::min(FIRE_MAX, f64::max(share, FIRE_PER_DAY * (run - FIRE_FROM + 1) as f64));
                healed = 0;
                missed = missed.max(run);
            }
        }
    }
    if share == 0.0 && !out {
        return None;
    }
    let news = share != 0.0 && (healed == 0 || (healed == 1 && days.contains(&0)));
    Some(FireState { share, healed, missed, began, epoch, out, news })
}

/// Days in a row without reviews up to now, counting today while it has none.
fn missed_now(days: &HashSet<i64>) -> i64 {
    let mut run = 0;
    while run < LOOKBACK_DAYS && !days.contains(&run) {
        run += 1;
    }
    run
}

/// Python's zlib.crc32.
fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &b in bytes {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0xEDB8_8320 } else { crc >> 1 };
        }
    }
    !crc
}

/// Python's round() to a whole number: halves to even.
fn round_even(x: f64) -> i64 {
    let r = x.round();
    if (x - x.trunc()).abs() == 0.5 && r % 2.0 != 0.0 {
        (r - x.signum()) as i64
    } else {
        r as i64
    }
}

/// The indexes of the trees a fire of `share` takes: among those that stood
/// when the break began, of the newest `limit`, picked by lot - the same ones
/// every day of it (events._fire_lot).
fn fire_lot(trees: &[Tree], share: f64, began: i64, epoch: i64, limit: Option<usize>) -> HashSet<usize> {
    let from = limit.map_or(0, |l| trees.len().saturating_sub(l));
    let mut pool: Vec<usize> = (from..trees.len()).filter(|&i| ago(&trees[i]) > began).collect();
    if pool.is_empty() {
        return HashSet::new();
    }
    let n = (round_even(share * pool.len() as f64).max(1) as usize).min(pool.len());
    let lot = |i: &usize| {
        let t = &trees[*i];
        let seed = t.get("seed").and_then(Value::as_i64).unwrap_or_else(|| ago(t));
        crc32(format!("fire|{seed}|{epoch}").as_bytes())
    };
    pool.sort_by_key(lot);
    pool.into_iter().take(n).collect()
}

/// The trees with Wild's fire on them (`burn`, 1 while it blazes and less
/// each day you study), or the day before one breaks out its `smoke`, and
/// what the page says of it (events.set_fire).
fn set_fire(trees: &[Tree], days: &HashSet<i64>, limit: Option<usize>) -> (Vec<Tree>, Option<Value>) {
    let mark = |picked: &HashSet<usize>, key: &str, value: Value| -> Vec<Tree> {
        trees
            .iter()
            .enumerate()
            .map(|(i, t)| {
                let mut t = t.clone();
                if picked.contains(&i) {
                    t.insert(key.into(), value.clone());
                }
                t
            })
            .collect()
    };
    let Some(st) = fire_state(days) else {
        // smoke: FIRE_FROM - 1 days missed, today none yet (events.smoke_state)
        let run = missed_now(days) - 1;
        if days.is_empty() || run != FIRE_FROM - 1 {
            return (trees.to_vec(), None);
        }
        let epoch = days.iter().filter(|&&x| x > run).count() as i64;
        let smoking = fire_lot(trees, FIRE_PER_DAY, run, epoch, limit);
        let info = json!({ "left": 0, "trees": 0, "missed": FIRE_FROM - 1, "began": run, "out": false,
                           "news": false, "smoke": smoking.len() });
        return (mark(&smoking, "smoke", json!(1)), Some(info));
    };
    let mut info = json!({ "left": 0, "trees": 0, "missed": st.missed, "began": st.began, "out": st.out,
                           "news": st.news, "smoke": 0 });
    if st.share == 0.0 {
        return (trees.to_vec(), Some(info));
    }
    let burning = fire_lot(trees, st.share, st.began.unwrap_or(0), st.epoch, limit);
    let burn = round_to(1.0 - st.healed as f64 / FIRE_HEAL_DAYS as f64, 3);
    info["left"] = json!(FIRE_HEAL_DAYS - st.healed);
    info["trees"] = json!(burning.len());
    (mark(&burning, "burn", json!(burn)), Some(info))
}

/// 0, or how tall the grass has grown (up to 1) after weeks without new
/// cards while still reviewing (events.stagnation).
fn stagnation(trees: &[Tree], days: &HashSet<i64>) -> f64 {
    let Some(newest) = trees.last() else { return 0.0 };
    if !(0..STAGNANT_REVIEWING).any(|d| days.contains(&d)) {
        return 0.0; // not reviewing either: that is a break, not coasting
    }
    let idle = ago(newest);
    if idle < STAGNANT_AFTER {
        return 0.0;
    }
    f64::min(1.0, (idle - STAGNANT_AFTER + 1) as f64 / (STAGNANT_FULL - STAGNANT_AFTER + 1) as f64)
}

/// Whether any new card is left to learn (suspended ones don't count).
fn has_new_cards(col: &Collection) -> Result<bool> {
    Ok(col
        .storage
        .db()
        .query_row("select exists (select 1 from cards c where c.type = 0 and c.queue != -1)", [], |r| r.get(0))?)
}

/// The overdue reviews, how deep in review hell they put you, and whether
/// today is the day you cleared them (events_state._backlog).
fn backlog(col: &Collection, memory: &mut Value, at: Moment) -> Result<Map<String, Value>> {
    let db = col.storage.db();
    let overdue: i64 = db.query_row(
        "select count() from cards c where c.queue in (2, 3) \
         and (case when c.odid != 0 and c.odue != 0 then c.odue else c.due end) < ?1",
        [at.today],
        |r| r.get(0),
    )?;
    let mut counts = Vec::new();
    {
        let mut st = db.prepare(&format!(
            "select (?1 - 1 - id / 1000) / {DAY_SECS}, count() from revlog \
             where ease > 0 and type in {STUDY_TYPES} and id >= ?2 and id < ?3 group by 1"
        ))?;
        let from = (at.cutoff - (REVIEW_HELL_USUAL_DAYS + 1) * DAY_SECS) * 1000;
        let mut rows = st.query([at.cutoff, from, (at.cutoff - DAY_SECS) * 1000])?;
        while let Some(r) = rows.next()? {
            counts.push(r.get::<_, i64>(1)? as f64);
        }
    }
    let studied: Vec<f64> = counts.into_iter().filter(|&n| n > 0.0).collect();
    let usual = if studied.is_empty() { 0.0 } else { median(&studied) };
    let limit = f64::max(REVIEW_HELL_MIN as f64, REVIEW_HELL_TIMES as f64 * usual);
    let hell = if overdue as f64 <= limit { 0.0 } else { f64::min(1.0, overdue as f64 / limit - 1.0) };
    let day = |memory: &Value, key: &str| memory.get(key).and_then(Value::as_i64);
    let (hell_day, cleared_day) = (day(memory, "hell_day"), day(memory, "backlog_cleared"));
    let cleared = cleared_day == Some(at.today)
        || (overdue == 0
            && hell_day.is_some_and(|h| (0..=BACKLOG_CLEARED_WITHIN).contains(&(at.today - h)))
            && cleared_day.is_none_or(|c| c < hell_day.unwrap()));
    for (key, on) in [("hell_day", hell > 0.0), ("backlog_cleared", cleared)] {
        if on && day(memory, key) != Some(at.today) {
            memory[key] = json!(at.today);
        }
    }
    if hell > 0.0 && memory.get("hell_level").and_then(Value::as_f64) != Some(round_to(hell, 3)) {
        memory["hell_level"] = json!(round_to(hell, 3));
    }
    let mut out = Map::new();
    out.insert("overdue".into(), json!(overdue));
    out.insert("usual".into(), json!(round_even(usual)));
    out.insert("hell".into(), json!(round_to(hell, 3)));
    out.insert("cleared".into(), json!(cleared));
    if cleared {
        let level = memory.get("hell_level").and_then(Value::as_f64).filter(|&l| l > 0.0 && l <= 1.0);
        out.insert("was".into(), json!(level.unwrap_or(0.5)));
    }
    Ok(out)
}

// ---------- animals and anniversaries (milestones.py) ----------

/// The milestone animals these numbers bring (milestones.VISITORS), in order.
fn earned(s: &Map<String, Value>) -> Vec<&'static str> {
    let int = |k: &str| s.get(k).and_then(Value::as_i64).unwrap_or(0);
    let longest = int("longest_streak");
    [
        ("rabbit", int("trees") >= RABBIT_TREES),
        ("deer", longest >= DEER_STREAK),
        ("fox", int("reviews") >= FOX_REVIEWS),
        ("owl", int("ancient") >= 1),
        ("heron", int("ponds") >= 1),
        ("stag", longest >= STAG_STREAK),
        ("cabin", int("forest_age") >= CABIN_AGE_DAYS),
        ("bear", longest >= BEAR_STREAK),
        ("squirrel", int("mature_cards") >= SQUIRREL_MATURE),
        ("eagle", int("mature_cards") >= EAGLE_MATURE),
    ]
    .into_iter()
    .filter_map(|(key, here)| here.then_some(key))
    .collect()
}

const ANIMALS: [&str; 10] = ["rabbit", "deer", "fox", "owl", "heron", "stag", "cabin", "bear", "squirrel", "eagle"];

/// Every animal that has come to the forest `key` ("all", or the day an
/// asteroid began it again), remembered in `memory`: once come, an animal
/// stays, and is new on the day it came. The first look at a forest takes
/// in the animals it already has without announcing them
/// (events_state._animals, milestones.arrivals and visitors).
fn animals(memory: &mut Value, key: &str, stats: &Map<String, Value>, today: i64) -> Vec<Value> {
    let every = memory.get("animals").filter(|v| v.is_object()).cloned().unwrap_or_else(|| json!({}));
    let known = every.get(key).filter(|v| v.is_object()).cloned();
    let first = known.is_none();
    let mut arrived: Map<String, Value> = known
        .and_then(|v| v.as_object().cloned())
        .unwrap_or_default()
        .into_iter()
        .filter(|(_, v)| v.is_null() || v.is_i64())
        .collect();
    for animal in earned(stats) {
        if !arrived.contains_key(animal) {
            arrived.insert(animal.into(), if first { Value::Null } else { json!(today) });
        }
    }
    if every.get(key) != Some(&Value::Object(arrived.clone())) {
        let mut every = every;
        every[key] = Value::Object(arrived.clone());
        memory["animals"] = every;
    }
    ANIMALS
        .iter()
        .filter_map(|&a| {
            let day = arrived.get(a)?;
            Some(json!({ "key": a, "new": day.as_i64() == Some(today), "day": day }))
        })
        .collect()
}

// ---------- the study heatmap (payload._calendar, study_log, milestones.animal_days) ----------

// How many days ahead the heatmap counts the cards due (payload.CALENDAR_DUE_DAYS).
const CALENDAR_DUE_DAYS: usize = 800;

/// What the heatmap is drawn from (web/calendar.js `cal`): each day's reviews and the time they
/// took from the first day studied to today, the cards due on each day from today on, the trees'
/// days, the day each animal came, the forest's ponds and the days with nothing due.
#[allow(clippy::too_many_arguments)]
fn calendar(col: &Collection, at: Moment, rows: &Rows, trees: &[Tree], visitors: &[Value], free: &HashSet<i64>,
            week_start: i64) -> Result<Value> {
    let oldest = rows.day_reviews.keys().copied().max().unwrap_or(0);
    let days: Vec<i64> = (0..=oldest).rev().map(|d| rows.day_reviews.get(&d).copied().unwrap_or(0)).collect();
    let time: Vec<i64> = (0..=oldest).rev().map(|d| rows.day_time.get(&d).copied().unwrap_or(0)).collect();
    let free_runs: Vec<[i64; 2]> = runs_of(free).into_iter().filter(|r| r[1] <= oldest).collect();
    Ok(json!({
        "view": "timeline", "shadeBy": "reviews", "weekStart": week_start,
        "start": iso(day_date(oldest, at.cutoff)), "days": days,
        "due": load_due(col, at.today, CALENDAR_DUE_DAYS)?,
        "planted": trees.iter().map(|t| json!([ago(t), t["n"]])).collect::<Vec<_>>(),
        "animals": animal_days(visitors, trees, &rows.day_reviews, at.today),
        "time": time, "answerTime": [],
        "ponds": trees.iter().filter(|t| t.get("gap").is_some() && t.get("gap_from").is_some() && t.get("gap_to").is_some())
            .map(|t| json!([t["gap_from"], t["gap_to"]])).collect::<Vec<_>>(),
        "forestStart": null, "hiddenBefore": null, "countsFrom": null, "streakFromStart": false,
        "marks": [], "gone": [], "free": free_runs, "daysOff": [], "browse": true,
    }))
}

/// How many cards fall due on each of the `days` days from today (today first): overdue cards
/// and cards in learning count as today's; suspended and buried ones nowhere (study_log.load_due).
fn load_due(col: &Collection, today: i64, days: usize) -> Result<Vec<i64>> {
    let mut out = vec![0i64; days];
    let mut st = col.storage.db().prepare(
        "select c.queue, case when c.odid != 0 and c.odue != 0 then c.odue else c.due end, count() \
         from cards c where c.queue in (1, 2, 3) group by 1, 2",
    )?;
    let mut rows = st.query([])?;
    while let Some(r) = rows.next()? {
        let (queue, due, n): (i64, i64, i64) = (r.get(0)?, r.get(1)?, r.get(2)?);
        let ahead = if queue == 1 { 0 } else { (due - today).max(0) };
        if (ahead as usize) < days {
            out[ahead as usize] += n;
        }
    }
    Ok(out)
}

/// Days (as `ago`) in [older, newer] runs, oldest first (study_log.load_free_days' runs).
fn runs_of(days: &HashSet<i64>) -> Vec<[i64; 2]> {
    let mut sorted: Vec<i64> = days.iter().copied().collect();
    sorted.sort();
    let mut runs: Vec<[i64; 2]> = Vec::new();
    for d in sorted {
        match runs.last_mut() {
            Some(r) if r[0] == d - 1 => r[0] = d,
            _ => runs.push([d, d]),
        }
    }
    runs.reverse();
    runs
}

/// When each animal in the forest came, as days ago: from the history where its milestone can
/// be dated (the day a streak reached its length, the 50th tree, the 10,000th review, the first
/// pond, the forest's first birthday), else the day the forest saw it come (milestones.animal_days).
fn animal_days(visitors: &[Value], trees: &[Tree], day_reviews: &HashMap<i64, i64>, today: i64) -> Vec<Value> {
    let mut studied: Vec<i64> = day_reviews.iter().filter(|(_, &n)| n > 0).map(|(&d, _)| d).collect();
    studied.sort_by(|a, b| b.cmp(a)); // oldest first
    let (mut streak_at, mut run, mut last): (HashMap<i64, i64>, i64, Option<i64>) = (HashMap::new(), 0, None);
    for &d in &studied {
        run = if last.is_some_and(|l| l - d == 1) { run + 1 } else { 1 };
        last = Some(d);
        streak_at.entry(run).or_insert(d);
    }
    let mut all: Vec<i64> = day_reviews.keys().copied().collect();
    all.sort_by(|a, b| b.cmp(a));
    let (mut total, mut passed) = (0, None);
    for d in all {
        total += day_reviews[&d];
        if passed.is_none() && total >= FOX_REVIEWS {
            passed = Some(d);
        }
    }
    let first_pond = trees.iter().find(|t| t.get("gap").is_some()).map(ago);
    let birthday = trees.first().map_or(-1, |t| ago(t) - CABIN_AGE_DAYS);
    let found = |key: &str| -> Option<i64> {
        match key {
            "deer" => streak_at.get(&DEER_STREAK).copied(),
            "stag" => streak_at.get(&STAG_STREAK).copied(),
            "bear" => streak_at.get(&BEAR_STREAK).copied(),
            "rabbit" => trees.get(RABBIT_TREES as usize - 1).map(ago),
            "fox" => passed,
            "heron" => first_pond,
            "cabin" => (birthday >= 0).then_some(birthday),
            _ => None,
        }
    };
    visitors
        .iter()
        .filter_map(|v| {
            let key = v["key"].as_str()?;
            let d = found(key).or_else(|| v["day"].as_i64().map(|day| today - day))?;
            (d >= 0).then(|| json!({ "key": key, "ago": d }))
        })
        .collect()
}

/// Whether a tree turned ancient today: remembered in `memory`, so a tree is
/// news once, and the first look takes in the ancient trees already there
/// (payload._new_ancient_today, for the whole forest and no start date).
fn new_ancient_today(memory: &mut Value, trees: &[Tree], today: NaiveDate) -> bool {
    let today_iso = iso(today);
    let mut days: Vec<i64> =
        trees.iter().filter(|t| t["stage"].as_i64() == Some(ANCIENT)).filter_map(|t| t["day"].as_i64()).collect();
    days.sort();
    let ints = |v: Option<&Value>| -> Option<Vec<i64>> {
        v.and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_i64).collect())
    };
    let every: Map<String, Value> = memory.get("ancient").and_then(|v| v.as_object().cloned()).unwrap_or_default();
    let known = ints(every.get("all")).or_else(|| ints(memory.get("ancient_days")));
    let mut event = memory.get("ancient_event").and_then(Value::as_str) == Some(today_iso.as_str());
    let turned: Vec<i64> = match &known {
        None => vec![],
        Some(k) => days.iter().filter(|d| !k.contains(d)).copied().collect(),
    };
    if known.is_none() || !turned.is_empty() {
        let mut turned: HashSet<i64> = turned.into_iter().collect();
        if !turned.is_empty() {
            if event {
                turned.extend(ints(memory.get("ancient_event_days")).unwrap_or_default());
            }
            let mut sorted: Vec<i64> = turned.iter().copied().collect();
            sorted.sort();
            memory["ancient_event"] = json!(today_iso);
            memory["ancient_event_days"] = json!(sorted);
            event = true;
        }
        let mut every_out = Map::new();
        for (k, v) in &every {
            if let Some(list) = ints(Some(v)) {
                let mut all: Vec<i64> = list.into_iter().chain(turned.iter().copied()).collect();
                all.sort();
                all.dedup();
                every_out.insert(k.clone(), json!(all));
            }
        }
        let mut all: Vec<i64> = days.iter().copied().chain(known.unwrap_or_default()).collect();
        all.sort();
        all.dedup();
        every_out.insert("all".into(), json!(all));
        memory["ancient"] = Value::Object(every_out);
        if let Some(m) = memory.as_object_mut() {
            m.remove("ancient_days");
        }
    }
    let shown = ints(memory.get("ancient_event_days")).unwrap_or_default();
    event && shown.iter().any(|d| days.contains(d))
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

/// The time of day at `now`: by the place's sunrise and sunset (instants), or
/// without them by the default ones on the local clock (scene.time_of_day).
fn time_of_day(now: f64, sun: Option<(DateTime<Utc>, DateTime<Utc>)>) -> &'static str {
    let t = DateTime::<Utc>::from_timestamp(now as i64, 0).unwrap_or_default();
    let (rise, set) = sun.unwrap_or_else(|| {
        let local = local(now as i64);
        let at = |h: u32, m: u32| {
            local
                .date_naive()
                .and_hms_opt(h, m, 0)
                .and_then(|d| chrono::Local.from_local_datetime(&d).single())
                .map_or(t, |d| d.with_timezone(&Utc))
        };
        (at(DEFAULT_SUNRISE.0, DEFAULT_SUNRISE.1), at(DEFAULT_SUNSET.0, DEFAULT_SUNSET.1))
    });
    let tw = chrono::Duration::minutes(TWILIGHT_MINUTES);
    if rise - tw <= t && t < rise + tw {
        "dawn"
    } else if set - tw <= t && t < set + tw {
        "dusk"
    } else if set - tw * 2 <= t && t < set - tw {
        "golden_hour"
    } else if rise + tw <= t && t < set - tw {
        "day"
    } else {
        "night"
    }
}

/// The five look settings for this moment (scene.choose_mood): the preset's,
/// or with the real sky on, the live weather (the preset's own without it)
/// and the time of day by the clock.
pub fn mood(look: &Look, now_ts: f64) -> Value {
    let now = local(now_ts as i64);
    let date = now.date_naive();
    let rolled = |weather: &str| matches!(weather, "cloudy" | "rain" | "storm") && roll(date, "wind") < WIND_CHANCE;
    let instant = |v: &Value| DateTime::parse_from_rfc3339(v.as_str()?).ok().map(|t| t.with_timezone(&Utc));
    let real = look.live.as_ref().filter(|_| look.real_sky);
    let (weather, source, windy) = match real {
        Some(r) => (r["weather"].as_str().unwrap_or("cloudy").to_string(), "real", r["windy"].as_bool().unwrap_or(false)),
        None => (look.weather.clone(), if look.real_sky { "preset" } else { "manual" }, rolled(&look.weather)),
    };
    let time = if !look.real_sky {
        look.time.clone()
    } else if let Some(polar) = real.and_then(|r| r["polar"].as_str()) {
        (if polar == "day" { "day" } else { "night" }).to_string()
    } else {
        let sun = real.and_then(|r| Some((instant(&r["sunrise"])?, instant(&r["sunset"])?)));
        time_of_day(now_ts, sun).to_string()
    };
    let mut mood = json!({
        "time": time,
        "home": look.time,
        "clock": look.real_sky,
        "weather": weather,
        "special": look.environment,
        "wind": windy,
        "environment": look.environment,
        "landscape": look.landscape,
        "landmark": look.landmark,
        "moon": round_to(moon_phase(now_ts), 3),
        "source": source,
    });
    if let Some(r) = real {
        mood["temp"] = r["temp"].clone();
        mood["city"] = json!(look.city);
    }
    mood
}

// ---------- the journal (journal.py, events_state.news_line) ----------

// Counts worth remarking on, on the day they are reached.
const TREE_MILESTONES: [i64; 10] = [1, 10, 25, 50, 100, 250, 500, 1000, 2000, 5000];
const STREAK_MILESTONES: [i64; 9] = [7, 30, 50, 100, 200, 365, 500, 730, 1000];
// The journal's line on the day each animal comes (milestones.ARRIVED).
const ARRIVED: [(&str, &str); 10] = [
    ("rabbit", "visitor.a_rabbit_wandered_in_your_forest"),
    ("deer", "visitor.a_deer_wandered_in_you_kept"),
    ("fox", "visitor.a_fox_wandered_in_you_passed"),
    ("owl", "visitor.an_owl_wandered_in_your_first"),
    ("heron", "visitor.a_heron_wandered_in_a_pond"),
    ("stag", "visitor.a_stag_wandered_in_you_kept"),
    ("cabin", "visitor.a_little_cabin_now_stands_at"),
    ("bear", "visitor.a_bear_wandered_in_you_kept"),
    ("squirrel", "visitor.a_family_of_squirrels_wandered_in"),
    ("eagle", "visitor.an_eagle_wandered_in_you_knew"),
];

/// A line of the add-on's, by its id, for the page to write in its
/// language: `n` for a plural, `values` its placeholders (those in `dates`
/// are ISO dates, to write as dates).
fn line(id: &str, n: Option<i64>, values: Value) -> Value {
    json!({ "id": id, "n": n, "values": values })
}

/// What the events brought today, if anything: an asteroid's strike, a fire,
/// or a backlog cleared (events_state.news_line).
fn news_line(extras: &Map<String, Value>, backlog: &Map<String, Value>) -> Option<Value> {
    let strike = &extras["strike"];
    if strike["news"] == true && strike["told"] == true {
        let lost = strike["lost"].as_i64().unwrap_or(0);
        return Some(line("events.an_asteroid_took_your_forest_of", Some(lost), json!({ "trees": lost })));
    }
    let fire = &extras["fire"];
    if fire.is_object() {
        if fire["smoke"].as_i64().unwrap_or(0) > 0 {
            return Some(line("events.smoke_is_rising_from_your_forest", None, json!({})));
        }
        if fire["out"] == true {
            return Some(line("events.the_last_of_the_fire_is", None, json!({})));
        }
        if fire["news"] == true {
            let n = fire["trees"].as_i64().unwrap_or(0);
            return Some(line("events.a_fire_broke_out_while_you", Some(n), json!({ "trees": n, "days": FIRE_HEAL_DAYS })));
        }
    }
    if backlog["cleared"] == true {
        let id = if extras["nature"] == "peaceful" {
            "events.you_cleared_your_overdue_reviews"
        } else {
            "events.you_cleared_your_overdue_reviews_the"
        };
        return Some(line(id, None, json!({})));
    }
    None
}

/// The one line under the forest, on the days there is something to say
/// (journal.journal): an animal arrived, a tree turned ancient, an
/// anniversary came round, a milestone was passed, or the grass stands tall.
/// `starry`: a clear night, when the night sky's events show.
#[allow(clippy::too_many_arguments)]
fn journal(stats: &Map<String, Value>, visitors: &[Value], events: &[&str], trees: &[Tree], anniversaries: &[usize],
           stagnation: f64, today: NaiveDate, starry: bool) -> Option<Value> {
    if trees.is_empty() {
        return Some(line("journal.study_some_new_cards_and_your", None, json!({})));
    }
    for v in visitors {
        if v["new"] == true {
            if let Some((_, id)) = ARRIVED.iter().find(|(k, _)| v["key"] == *k) {
                return Some(line(id, None, json!({})));
            }
        }
    }
    if events.contains(&"harvest_moon") && starry {
        return Some(line("journal.a_harvest_moon_rises_over_your", None, json!({})));
    }
    if events.contains(&"meteor_shower") && starry {
        return Some(line("journal.meteors_are_falling_tonight", None, json!({})));
    }
    if events.contains(&"new_ancient") {
        let id = if starry { "journal.one_of_your_trees_became_ancient_2" } else { "journal.one_of_your_trees_became_ancient" };
        return Some(line(id, None, json!({})));
    }
    if let Some(&i) = anniversaries.first() {
        let date = trees[i]["date"].as_str().unwrap_or("");
        let years = today.year() as i64 - date.get(..4).and_then(|y| y.parse::<i64>().ok()).unwrap_or(today.year() as i64);
        let values = json!({ "date": date, "dates": ["date"] });
        return Some(if years == 1 {
            line("journal.the_tree_you_planted_on_date_2", None, values)
        } else {
            line("journal.the_tree_you_planted_on_date", Some(years), values)
        });
    }
    let int = |k: &str| stats.get(k).and_then(Value::as_i64).unwrap_or(0);
    if stats.get("planted_today") == Some(&json!(true)) {
        let (count, cards, streak) = (int("trees"), int("today_cards"), int("streak"));
        if TREE_MILESTONES.contains(&count) {
            return Some(if count == 1 {
                line("journal.your_first_tree_it_holds_n", Some(cards), json!({ "cards": cards }))
            } else {
                line("journal.your_count_th_tree_planted_today", Some(cards), json!({ "count": count, "cards": cards }))
            });
        }
        if STREAK_MILESTONES.contains(&streak) {
            return Some(line("journal.days_days_in_a_row_the", Some(streak), json!({ "days": streak })));
        }
    }
    if stagnation > 0.0 {
        let weeks = trees.last().map_or(0, ago) / 7;
        return Some(if weeks < 2 {
            line("journal.the_grass_is_growing_tall_no_2", None, json!({}))
        } else {
            line("journal.the_grass_is_growing_tall_no", Some(weeks), json!({}))
        });
    }
    None
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
    fn fire_helpers_match_python() {
        assert_eq!(crc32(b"hello"), 907060870); // zlib.crc32(b"hello")
        assert_eq!(round_even(2.5), 2); // round(2.5)
        assert_eq!(round_even(3.5), 4);
        assert_eq!(round_even(0.4), 0);
    }

    #[test]
    fn strikes_and_fire() {
        // studied until 6 days ago, away 5 to 3 days ago, back since: one strike, at 5
        let review: HashSet<i64> = [0, 1, 2, 6, 7, 8].into_iter().collect();
        assert_eq!(strikes(&review), vec![5]);
        // three days away: a fire, healing for the 3 days studied since
        let fire = fire_state(&review).unwrap();
        assert_eq!((fire.share, fire.healed, fire.missed, fire.began), (FIRE_PER_DAY, 3, 3, Some(5)));
        // two days missed, today not studied yet: smoke, no fire
        let smoke: HashSet<i64> = [3, 4, 5].into_iter().collect();
        assert!(fire_state(&smoke).is_none());
        assert_eq!(missed_now(&smoke), 3);
    }

    #[test]
    fn memory_curve() {
        // recall is 90% after `stability` days, on the default curve
        assert!((retrievability(10.0, 10.0, "") - 0.9).abs() < 1e-9);
        assert_eq!(stability(r#"{"s":12.5,"d":5.0}"#), Some(12.5));
        assert_eq!(stability(""), None);
    }
}
