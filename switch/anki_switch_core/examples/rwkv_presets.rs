//! Set every preset of a collection to RWKV (Instant and Curve, as the FSRS-7
//! fork's desktop stores them) with FSRS on: a collection to try RWKV with.
//! Usage: cargo run --example rwkv_presets -- <collection.anki2> [instant|curve]

use anki::collection::CollectionBuilder;
use anki::prelude::*;
use anki_proto::deck_config::UpdateDeckConfigsMode;
use anki_switch_core::presets::update_request;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("usage: rwkv_presets <collection.anki2> [instant|curve]");
    let instant = args.next().as_deref() != Some("curve");
    let mut col = CollectionBuilder::new(path).build().unwrap();
    let decks = col.get_all_normal_deck_names(false).unwrap();
    let mut done = std::collections::HashSet::new();
    for (deck_id, name) in decks {
        let info = col.get_deck_configs_for_update(deck_id).unwrap();
        let config_id = DeckConfigId(info.current_deck.as_ref().unwrap().config_id);
        if !done.insert(config_id) {
            continue;
        }
        let mut config = col.get_deck_config(config_id, false).unwrap().unwrap();
        config.inner.rwkv_review_enabled = true;
        config.inner.rwkv_review_instant_order_enabled = instant;
        let mut request = update_request(&info, deck_id, vec![config], UpdateDeckConfigsMode::Normal);
        request.fsrs = true;
        col.update_deck_configs(request).unwrap();
        println!("preset {} (via {name}): RWKV {}", config_id.0, if instant { "Instant + Curve" } else { "Curve" });
    }
    col.close(None).unwrap();
}
