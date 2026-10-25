//! A collection with two years of made-up study history, for the forest
//! (src/forest.rs): new cards most days, a break of a fortnight, reviews at
//! growing intervals with some lapses, FSRS memory on half the cards, a few
//! suspended cards and leeches.
//! Usage: cargo run --example make_forest_history -- <data dir> [days [away_from away_to]]
//! (away_from to away_to: days ago with no study at all, besides the
//! fortnight long ago: 2 4, say, for Wild's fire and Merciless's asteroid)
//! Writes <dir>/collection.anki2.

use anki::collection::CollectionBuilder;

const DAY_MS: i64 = 86_400_000;
// A fortnight away, as days ago: nothing is studied then.
const AWAY: std::ops::Range<i64> = 300..314;

/// A small deterministic generator (the same history every run).
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> f64 {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        (self.0 >> 11) as f64 / (1u64 << 53) as f64
    }
    fn range(&mut self, lo: i64, hi: i64) -> i64 {
        lo + (self.next() * (hi - lo + 1) as f64) as i64
    }
}

fn main() {
    let mut args = std::env::args().skip(1);
    let dir = std::path::PathBuf::from(args.next().expect("usage: make_forest_history <dir> [days]"));
    let days: i64 = args.next().map_or(800, |d| d.parse().expect("days"));
    let recent = match (args.next(), args.next()) {
        (Some(a), Some(b)) => a.parse::<i64>().expect("away_from")..b.parse::<i64>().expect("away_to") + 1,
        _ => 0..0,
    };
    let away = |ago: i64| AWAY.contains(&ago) || recent.contains(&ago);
    // the day studying resumed after the break that holds `ago`
    let back = |ago: i64| if AWAY.contains(&ago) { AWAY.start - 1 } else { recent.start - 1 };
    std::fs::create_dir_all(dir.join("collection.media")).unwrap();
    let mut col = CollectionBuilder::new(dir.join("collection.anki2"))
        .set_media_paths(dir.join("collection.media"), dir.join("collection.mdb"))
        .build()
        .unwrap();
    let cutoff_ms = col.timing_today().unwrap().next_day_at.0 * 1000;
    let deck = col.get_or_create_normal_deck("Forest").unwrap();
    let basic = col.get_notetype_by_name("Basic").unwrap().unwrap();
    let mut rng = Rng(20261024);

    // (card id, note id, first study day as days ago, cards learned that day)
    let mut cards = Vec::new();
    for ago in (0..days).rev() {
        if away(ago) || rng.next() < 0.15 {
            continue;
        }
        let n = if rng.next() < 0.05 { rng.range(60, 90) } else { rng.range(4, 30) };
        for i in 0..n {
            let mut note = basic.new_note();
            note.set_field(0, format!("day {ago} card {i}")).unwrap();
            note.set_field(1, "back").unwrap();
            col.add_note(&mut note, deck.id).unwrap();
            let cid = col.storage.all_card_ids_of_note_in_template_order(note.id).unwrap()[0];
            cards.push((cid.0, note.id.0, ago));
        }
    }

    let db = col.storage.db();
    db.execute_batch("begin").unwrap();
    let mut rid = 0i64;
    for &(cid, nid, first) in &cards {
        // reviews at noon of their day, a few ms apart so ids stay unique
        let mut at = |ago: i64, rid: &mut i64| {
            *rid += 1;
            cutoff_ms - ago * DAY_MS - DAY_MS / 2 + *rid
        };
        let mut log = |id: i64, ease: i64, ivl: i64, last: i64, kind: i64| {
            db.execute(
                "insert into revlog (id, cid, usn, ease, ivl, lastIvl, factor, time, type) values (?1, ?2, -1, ?3, ?4, ?5, 2500, ?6, ?7)",
                [id, cid, ease, ivl, last, 4000 + (id % 9000), kind],
            )
            .unwrap();
        };
        let leech = rng.next() < 0.01;
        let lapse_rate = if leech { 0.6 } else { 0.08 };
        log(at(first, &mut rid), 3, 1, 0, 0);
        let (mut ago, mut ivl, mut lapsed) = (first, 1i64, false);
        while ago - ivl >= 0 {
            ago -= ivl;
            if away(ago) {
                ago = back(ago); // (reviews wait until the break is over)
            }
            let last = ivl;
            if rng.next() < lapse_rate {
                log(at(ago, &mut rid), 1, -600, last, 1);
                ivl = 1;
                lapsed = true;
            } else {
                ivl = ((ivl as f64) * (2.0 + rng.next())).ceil() as i64;
                log(at(ago, &mut rid), 3, ivl, last, 1);
                lapsed = false;
            }
        }
        // what the card is now: learning if first studied lately, relearning
        // after a lapse just now, otherwise in review; FSRS memory on half
        let (ctype, queue) = if first <= 1 {
            (1, 1)
        } else if lapsed && ago <= 1 {
            (3, 1)
        } else {
            (2, 2)
        };
        let queue = if rng.next() < 0.02 { -1 } else { queue };
        let data = if cid % 2 == 0 {
            format!(r#"{{"s":{:.2},"d":{:.2},"dr":0.9,"decay":0.1542}}"#, ivl as f64 * (0.8 + rng.next()), 3.0 + 4.0 * rng.next())
        } else {
            String::new()
        };
        db.execute(
            "update cards set type = ?1, queue = ?2, ivl = ?3, due = ?4, data = ?5 where id = ?6",
            (ctype, queue, ivl, ivl - ago, data.as_str(), cid),
        )
        .unwrap();
        if leech {
            db.execute("update notes set tags = ' leech ' where id = ?1", [nid]).unwrap();
        }
    }
    db.execute_batch("commit").unwrap();
    println!("{} cards over {days} days", cards.len());
}

