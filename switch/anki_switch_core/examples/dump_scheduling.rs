//! Everything that decides how a deck's cards are scheduled, from a copy of a
//! collection: the deck's preset (every field, the fork's included), the
//! deck's own overrides, the collection-wide settings deck options saves, the
//! day's timing, and optionally one card's state and review history. For
//! comparing the console with the desktop fork.
//! Usage: dump_scheduling <collection.anki2> <deck name> [card id]

use anki::collection::CollectionBuilder;
use anki::prelude::*;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("usage: dump_scheduling <collection.anki2> <deck name> [card id]");
    let deck_name = args.next().expect("deck name");
    let card_id = args.next().map(|id| CardId(id.parse().expect("card id")));
    let mut col = CollectionBuilder::new(path).build().unwrap();

    let deck = col.get_deck_id(&deck_name).unwrap().expect("no such deck");
    let info = col.get_deck_configs_for_update(deck).unwrap();
    let current = info.current_deck.as_ref().unwrap();
    println!("== deck {deck_name} ({}) preset id {}", deck.0, current.config_id);
    println!("deck limits (the deck's own): {:#?}", current.limits);
    let preset = info.all_config.iter().find(|c| c.config.as_ref().unwrap().id == current.config_id).unwrap();
    println!("== preset (used by {} decks)\n{:#?}", preset.use_count, preset.config);
    println!(
        "== collection-wide\nfsrs={} fsrs_health_check={} load_balancer={} short_term_with_steps={} learning_queues_disabled={} \
         apply_all_parent_limits={} new_cards_ignore_review_limit={} review_fuzz enabled={} base={} short={} mid={} long={}\n\
         rwkv_profile_settings (desktop fills these from its profile)={:?}",
        info.fsrs,
        info.fsrs_health_check,
        info.load_balancer_enabled,
        info.fsrs_short_term_with_steps_enabled,
        info.fsrs_learning_queues_disabled,
        info.apply_all_parent_limits,
        info.new_cards_ignore_review_limit,
        info.review_fuzz_enabled,
        info.review_fuzz_base,
        info.review_fuzz_factor_short,
        info.review_fuzz_factor_mid,
        info.review_fuzz_factor_long,
        info.rwkv_profile_settings,
    );
    println!("custom scheduling: {} bytes", info.card_state_customizer.len());
    let timing = col.timing_today().unwrap();
    println!(
        "== timing: days_elapsed={} next_day_at={} now={} rollover={:?} configured_utc_offset={:?}",
        timing.days_elapsed,
        timing.next_day_at.0,
        timing.now.0,
        col.rollover_for_current_scheduler().ok(),
        col.get_configured_utc_offset(),
    );
    {
        let db = col.storage.db();
        let crt: i64 = db.query_row("select crt from col", [], |r| r.get(0)).unwrap();
        println!("crt={crt}");
        let mut stmt = db
            .prepare("select key, cast(val as text) from config where key in ('collapseTime','creationOffset','localOffset','rollover','schedVer','sched2021','dayLearnFirst','newSpread','estTimes','dueCounts')")
            .unwrap();
        let rows = stmt.query_map([], |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?))).unwrap();
        for row in rows {
            let (key, val) = row.unwrap();
            println!("config {key} = {val}");
        }
    }

    if let Some(cid) = card_id {
        let card = col.storage.get_card(cid).unwrap().expect("no such card");
        println!("== card {}\n{card:#?}", cid.0);
        {
            let db = col.storage.db();
            let mut stmt = db
                .prepare("select id, ease, ivl, lastIvl, type, time, factor from revlog where cid = ?1 order by id")
                .unwrap();
            let rows: Vec<String> = stmt
                .query_map([cid.0], |r| {
                    Ok(format!(
                        "{} ease={} ivl={} last_ivl={} type={} taken_ms={} factor={}",
                        r.get::<_, i64>(0)?, r.get::<_, i64>(1)?, r.get::<_, i64>(2)?, r.get::<_, i64>(3)?,
                        r.get::<_, i64>(4)?, r.get::<_, i64>(5)?, r.get::<_, i64>(6)?
                    ))
                })
                .unwrap()
                .map(Result::unwrap)
                .collect();
            println!("== {} reviews (last 8)", rows.len());
            for row in rows.iter().rev().take(8).rev() {
                println!("{row}");
            }
        }
        let states = col.get_scheduling_states(cid).unwrap();
        println!("== scheduling states now (this build's)\n{states:#?}");
        println!("labels: {:?}", col.describe_next_states(&states).unwrap());
    }
}
