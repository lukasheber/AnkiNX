//! The FSRS-7 fork of Anki (experimental/fsrs7): what AnkiNX relies on when a
//! collection comes from the desktop fork. Run with switch/test_core_host.sh.
//! The JSON API keeps global state, so its part runs in a single test.

use std::ffi::CStr;
use std::ffi::CString;

use anki::collection::Collection;
use anki::collection::CollectionBuilder;
use anki::config::BoolKey;
use anki::prelude::*;
use anki::scheduler::answering::CardAnswer;
use anki::scheduler::answering::Rating;
use anki_proto::deck_config::deck_config::config::ReviewCardOrder;
use anki_proto::deck_config::UpdateDeckConfigsMode;
use anki_switch_core::ankinx_call;
use anki_switch_core::ankinx_free;
use anki_switch_core::presets::update_request;
use serde_json::json;
use serde_json::Value;

fn collection(base: &std::path::Path) -> Collection {
    CollectionBuilder::new(base.join("collection.anki2"))
        .set_media_paths(base.join("collection.media"), base.join("collection.mdb"))
        .build()
        .unwrap()
}

/// Three Basic notes in the default deck, all due for review today.
fn add_due_reviews(col: &mut Collection) {
    let basic = col.get_notetype_by_name("Basic").unwrap().unwrap();
    for front in ["one", "two", "three"] {
        let mut note = basic.new_note();
        note.set_field(0, front).unwrap();
        note.set_field(1, "back").unwrap();
        col.add_note(&mut note, DeckId(1)).unwrap();
    }
    let cids = col.search_cards("", anki::search::SortMode::NoOrder).unwrap();
    col.set_due_date(&cids, "0", None).unwrap();
}

/// Save the default preset with `change`, FSRS on, through our own update.
fn save_default_preset(col: &mut Collection, change: impl FnOnce(&mut anki::deckconfig::DeckConfig)) {
    let info = col.get_deck_configs_for_update(DeckId(1)).unwrap();
    let mut config = col.get_deck_config(DeckConfigId(1), true).unwrap().unwrap();
    change(&mut config);
    let mut request = update_request(&info, DeckId(1), vec![config], UpdateDeckConfigsMode::Normal);
    request.fsrs = true;
    col.update_deck_configs(request).unwrap();
}

/// A preset set to RWKV in the desktop fork (Curve and Instant) still shows
/// its due reviews here, where nothing supplies RWKV scores: AnkiNX schedules
/// it with its FSRS version, as Deck options tells the user.
#[test]
fn rwkv_presets_without_scores_still_show_their_reviews() {
    for order in [ReviewCardOrder::Day, ReviewCardOrder::RetrievabilityAscending] {
        let dir = tempfile::tempdir().unwrap();
        let mut col = collection(dir.path());
        add_due_reviews(&mut col);
        save_default_preset(&mut col, |config| {
            config.inner.rwkv_review_enabled = true;
            config.inner.rwkv_review_instant_order_enabled = true;
            config.inner.review_order = order as i32;
        });
        let config = col.get_deck_config(DeckConfigId(1), false).unwrap().unwrap();
        assert!(config.inner.rwkv_review_instant_order_enabled, "the RWKV setting is kept");
        let queued = col.get_queued_cards(10, false, false).unwrap();
        assert_eq!(queued.review_count, 3, "{order:?}");
        assert_eq!(queued.cards.len(), 3, "{order:?}");
        // Answering works and leaves an FSRS memory state.
        let card_id = col.get_next_card().unwrap().unwrap().card.id();
        let states = col.get_scheduling_states(card_id).unwrap();
        col.answer_card(&mut CardAnswer {
            card_id,
            current_state: states.current,
            new_state: states.good,
            rating: Rating::Good,
            answered_at: TimestampMillis::now(),
            milliseconds_taken: 4_000,
            custom_data: None,
            desired_retention_override: None,
            desired_retention_override_is_final: false,
            rwkv_s90: None,
            rwkv_retrievability: None,
            rwkv_review_kind: None,
            from_queue: true,
        })
        .unwrap();
        let stats = col.card_stats(card_id).unwrap();
        assert!(stats.memory_state.is_some(), "{order:?}");
    }
}

fn call(cmd: &str, args: Value) -> Value {
    let cmd = CString::new(cmd).unwrap();
    let args = CString::new(args.to_string()).unwrap();
    unsafe {
        let out = ankinx_call(cmd.as_ptr(), args.as_ptr());
        let value = serde_json::from_str(CStr::from_ptr(out).to_str().unwrap()).unwrap();
        ankinx_free(out);
        value
    }
}

fn ok(cmd: &str, args: Value) -> Value {
    let v = call(cmd, args);
    assert!(v.get("ok").is_some(), "{cmd} failed: {v}");
    v["ok"].clone()
}

fn error_kind(cmd: &str, args: Value) -> String {
    let v = call(cmd, args);
    v["kind"].as_str().unwrap_or_else(|| panic!("{cmd} unexpectedly succeeded: {v}")).into()
}

/// Deck options on the console: the FSRS-7 values round-trip, and a save
/// keeps the collection-wide settings made on the desktop.
#[test]
fn deck_options_keep_desktop_settings() {
    let dir = tempfile::tempdir().unwrap();
    let base = dir.path();
    {
        let mut col = collection(base);
        add_due_reviews(&mut col);
        // Desktop choices that differ from the defaults.
        col.set_config_bool(BoolKey::LoadBalancerEnabled, false, false).unwrap();
        col.set_config_bool(BoolKey::FsrsShortTermWithStepsEnabled, false, false).unwrap();
        save_default_preset(&mut col, |config| {
            config.inner.fsrs_params_7 = vec![];
            config.inner.fsrs_params_6 = vec![0.4; 21];
            config.inner.rwkv_review_instant_order_enabled = true;
        });
        col.close(None).unwrap();
    }
    ok("init", json!({ "base_dir": base, "langs": ["en"] }));
    ok("open", json!({}));
    let deck = json!({ "deck_id": 1 });

    let options = ok("deck_options", deck.clone());
    assert_eq!(options["fsrs"], true);
    assert_eq!(options["rwkv"], "instant");
    assert_eq!(options["values"]["fsrs_version"], "7", "FSRS-7 is the fork's default");
    assert_eq!(options["fsrs_params"], 0, "FSRS-7 has no parameters yet");
    assert_eq!(options["values"]["fsrs_minimum_interval"], "1s");
    assert_eq!(options["fsrs_short_term_with_steps"], false);
    assert_eq!(options["fsrs_skip_learning_queues"], false);

    // A preset value: the other settings stay as the desktop left them.
    ok("set_deck_options", json!({ "deck_id": 1, "values": { "new_per_day": 15, "fsrs_version": "6" } }));
    let options = ok("deck_options", deck.clone());
    assert_eq!(options["values"]["new_per_day"], 15);
    assert_eq!(options["values"]["fsrs_version"], "6");
    assert_eq!(options["fsrs_params"], 21);
    assert_eq!(options["rwkv"], "instant");
    assert_eq!(options["fsrs_short_term_with_steps"], false);
    ok("set_deck_options", json!({ "deck_id": 1, "values": { "fsrs_minimum_interval": "10m" } }));
    assert_eq!(ok("deck_options", deck.clone())["values"]["fsrs_minimum_interval"], "10m");
    for bad in [json!({ "fsrs_version": "8" }), json!({ "fsrs_minimum_interval": "10" })] {
        assert_eq!(error_kind("set_deck_options", json!({ "deck_id": 1, "values": bad })), "args");
    }

    // The collection-wide FSRS-7 switches.
    ok("set_deck_options", json!({ "deck_id": 1, "fsrs_skip_learning_queues": true }));
    let options = ok("deck_options", deck.clone());
    assert_eq!(options["fsrs_skip_learning_queues"], true);
    assert_eq!(options["fsrs_short_term_with_steps"], false);
    assert_eq!(error_kind("set_deck_options", json!({ "deck_id": 1, "fsrs_skip_learning_queues": 1 })), "args");

    // Back to the defaults of the preset's version (FSRS-6 here).
    ok("set_deck_options", json!({ "deck_id": 1, "reset_fsrs_params": true }));
    assert_eq!(ok("deck_options", deck.clone())["fsrs_params"], 0);
    ok("close", json!({}));

    let col = collection(base);
    assert!(!col.get_config_bool(BoolKey::LoadBalancerEnabled), "the load balancer stays off");
    assert!(!col.get_config_bool(BoolKey::FsrsShortTermWithStepsEnabled));
    assert!(col.get_config_bool(BoolKey::FsrsLearningQueuesDisabled));
}
