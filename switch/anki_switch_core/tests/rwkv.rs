//! RWKV through the JSON API (kuma3's offline layer, src/rwkv.rs), as the
//! console runs it: a collection whose presets the desktop set to RWKV loads
//! the model state in the background, studies with it, and reopens from the
//! state file. Its own test binary: the API keeps global state.
//! Run with switch/test_core_host.sh.

use std::ffi::CStr;
use std::ffi::CString;
use std::path::PathBuf;
use std::time::Duration;
use std::time::Instant;

use anki::collection::CollectionBuilder;
use anki::prelude::*;
use anki_proto::deck_config::UpdateDeckConfigsMode;
use anki_switch_core::ankinx_call;
use anki_switch_core::ankinx_free;
use anki_switch_core::presets::update_request;
use serde_json::json;
use serde_json::Value;

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

/// The app's bundled models (app/resources/rwkv).
fn models() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../app/resources/rwkv")
}

/// 200 cards with half a year of reviews ending over a month ago, presets set to RWKV-Instant and
/// Curve as the fork's desktop saves them.
fn make_collection(base: &std::path::Path) {
    let mut col = CollectionBuilder::new(base.join("collection.anki2"))
        .set_media_paths(base.join("collection.media"), base.join("collection.mdb"))
        .build()
        .unwrap();
    let basic = col.get_notetype_by_name("Basic").unwrap().unwrap();
    let mut cards = vec![];
    for i in 0..200 {
        let mut note = basic.new_note();
        note.set_field(0, format!("rwkv {i}")).unwrap();
        note.set_field(1, "back").unwrap();
        col.add_note(&mut note, DeckId(1)).unwrap();
        cards.push(col.storage.all_card_ids_of_note_in_template_order(note.id).unwrap()[0]);
    }
    let next_day_ms = col.timing_today().unwrap().next_day_at.0 * 1000;
    let db = col.storage.db();
    db.execute_batch("begin").unwrap();
    for (i, cid) in cards.iter().enumerate() {
        let (mut ago, mut ivl, mut kind) = (180 - (i as i64 % 150), 0i64, 0);
        // (studied until 40+ days ago: overdue now, so RWKV-Instant shows them)
        while ago > 40 {
            let lapse = kind != 0 && (i as i64 + ago) % 9 == 0;
            let last = ivl;
            ivl = if kind == 0 || lapse { 1 } else { (ivl as f64 * 2.4).ceil() as i64 };
            let id = next_day_ms - (ago + 1) * 86_400_000 + 3_600_000 + i as i64 * 10;
            db.execute(
                "insert into revlog (id, cid, usn, ease, ivl, lastIvl, factor, time, type) values (?1, ?2, -1, ?3, ?4, ?5, 2500, 5000, ?6)",
                rusqlite::params![id, cid.0, if lapse { 1 } else { 3 }, ivl, last, if lapse { 2 } else { kind }],
            )
            .unwrap();
            kind = 1;
            ago -= ivl;
        }
        // (due today, as reviews: what RWKV-Instant orders)
        db.execute("update cards set type = 2, queue = 2, ivl = ?1, due = ?2 where id = ?3", rusqlite::params![ivl.max(1), 0, cid.0])
            .unwrap();
    }
    db.execute_batch("commit").unwrap();
    let info = col.get_deck_configs_for_update(DeckId(1)).unwrap();
    let mut config = col.get_deck_config(DeckConfigId(1), false).unwrap().unwrap();
    config.inner.rwkv_review_enabled = true;
    config.inner.rwkv_review_instant_order_enabled = true;
    // (RWKV-Instant shows cards below it: nearly all, whatever the model predicts)
    config.inner.desired_retention = 0.99;
    let mut request = update_request(&info, DeckId(1), vec![config], UpdateDeckConfigsMode::Normal);
    request.fsrs = true;
    col.update_deck_configs(request).unwrap();
    col.close(None).unwrap();
}

fn wait_ready() -> Value {
    let start = Instant::now();
    loop {
        let status = ok("rwkv_status", json!({}));
        match status["state"].as_str() {
            Some("ready") => return status,
            Some("loading") => {}
            _ => panic!("RWKV not loading: {status}"),
        }
        assert!(start.elapsed() < Duration::from_secs(300), "still loading: {status}");
        std::thread::sleep(Duration::from_millis(100));
    }
}

#[test]
fn rwkv_presets_from_the_desktop_run_on_the_console() {
    let dir = tempfile::tempdir().unwrap();
    let base = dir.path();
    make_collection(base);
    ok("init", json!({ "base_dir": base, "langs": ["en"], "rwkv_models": models() }));
    ok("open", json!({}));

    // The whole history, replayed in the background.
    let status = wait_ready();
    assert!(status["reviews"].as_u64().unwrap() > 500, "{status}");
    // (unknown under the Horizon emulator, which keeps /proc from being read)
    if !status["memory"].is_null() {
        assert!(status["memory"]["used_mb"].as_u64().unwrap() > 0, "{status}");
    }

    // Studying: the reviews come in RWKV-Instant's order (its own daily rules
    // decide how many), and each answer goes into the model state at once, as
    // through Anki's backend: no "history changed" replay of everything (2
    // minutes for 75k reviews on the console).
    let deck = json!({ "deck_id": 1 });
    ok("select_deck", deck.clone());
    // As Anki desktop's "Precompute next card" (on by default): an answer
    // leaves the queue's re-scoring for later, so the next card comes at once,
    // and the study screen has it done while that card is shown.
    let mut answered = 0;
    let mut refreshed = 0;
    for _ in 0..3 {
        let card = ok("next_card", json!({}));
        if !card["card_id"].is_i64() {
            break;
        }
        if ok("rwkv_refresh", json!({}))["refreshed"] == true {
            refreshed += 1;
        }
        ok("answer", json!({ "ease": 3 }));
        answered += 1;
    }
    let log = std::fs::read_to_string(base.join("core.log")).unwrap_or_default();
    assert!(answered > 0, "RWKV-Instant showed no review\ncore.log:\n{log}");
    assert_eq!(log.matches("RWKV answer absorbed=true").count(), answered, "core.log:\n{log}");
    assert!(!log.contains("history changed"), "an answer made RWKV replay everything\ncore.log:\n{log}");
    if answered > 1 {
        assert!(log.contains("deferred=true"), "the re-scoring waits for the next card\ncore.log:\n{log}");
        assert!(refreshed > 0, "rwkv_refresh re-scored the queue\ncore.log:\n{log}");
    }
    assert_eq!(ok("rwkv_status", json!({}))["state"], "ready");
    ok("close", json!({}));
    assert_eq!(ok("rwkv_status", json!({}))["state"], "closed");
    let state_file = base.join("collection.rwkv-offline");
    assert!(state_file.is_file(), "the state file is kept next to the collection");

    // Opening again starts from the state file, and removes what failed saves
    // left behind (on the console, a failed save couldn't delete its file).
    let leftover = base.join(".tmpAbC123.rwkv-offline.tmp");
    std::fs::write(&leftover, b"half a state").unwrap();
    ok("open", json!({}));
    assert!(!leftover.exists(), "leftover save removed");
    let status = wait_ready();
    assert!(status["reviews"].as_u64().unwrap() > 500, "{status}");
    let log = std::fs::read_to_string(base.join("core.log")).unwrap_or_default();
    assert!(!log.contains("history changed"), "the state file was used\ncore.log:\n{log}");
    ok("close", json!({}));

    // A build without the models: RWKV is off, the collection still opens.
    ok("shutdown", json!({}));
    ok("init", json!({ "base_dir": base, "langs": ["en"] }));
    ok("open", json!({}));
    let status = ok("rwkv_status", json!({}));
    assert_eq!(status["state"], "off", "{status}");
    assert_eq!(status["reason"], "failed", "{status}");
    ok("select_deck", deck);
    assert!(ok("next_card", json!({}))["card_id"].is_i64(), "FSRS schedules the RWKV preset");
    ok("close", json!({}));
}
