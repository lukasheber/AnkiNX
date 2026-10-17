//! Host-side test of the JSON API, driven through the same C entry points the
//! Switch frontend uses. Run with switch/test_core_host.sh.
//! The API keeps global state, so everything runs in a single test.

use std::ffi::CStr;
use std::ffi::CString;

use anki::collection::CollectionBuilder;
use anki_switch_core::ankinx_call;
use anki_switch_core::ankinx_free;
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

fn error_kind(cmd: &str, args: Value) -> String {
    let v = call(cmd, args);
    v["kind"].as_str().unwrap_or_else(|| panic!("{cmd} unexpectedly succeeded: {v}")).into()
}

fn deck_id(decks: &Value, name: &str) -> i64 {
    decks["decks"]
        .as_array()
        .unwrap()
        .iter()
        .find(|d| d["name"] == name)
        .unwrap_or_else(|| panic!("deck {name} not in {decks}"))["id"]
        .as_i64()
        .unwrap()
}

#[test]
fn study_flow() {
    let dir = tempfile::tempdir().unwrap();
    let base = dir.path();

    // A collection like one copied from desktop: a nested deck with 3 notes.
    {
        let mut col = CollectionBuilder::new(base.join("collection.anki2"))
            .set_media_paths(base.join("collection.media"), base.join("collection.mdb"))
            .build()
            .unwrap();
        let deck = col.get_or_create_normal_deck("Spanish::Verbs").unwrap();
        let basic = col.get_notetype_by_name("Basic").unwrap().unwrap();
        for (front, back) in [
            ("hablar [sound:hablar.mp3]", "to speak"),
            ("comer", "to eat <img src=\"comer.png\">"),
            ("vivir", "to live"),
        ] {
            let mut note = basic.new_note();
            note.set_field(0, front).unwrap();
            note.set_field(1, back).unwrap();
            col.add_note(&mut note, deck.id).unwrap();
        }
        col.close(None).unwrap();
    }

    assert_eq!(error_kind("deck_tree", json!({})), "not_initialized");
    let status = ok("init", json!({ "base_dir": base, "langs": ["en"] }));
    assert_eq!(status["collection_exists"], true);
    assert_eq!(status["open"], false);
    assert_eq!(error_kind("deck_tree", json!({})), "not_open");

    ok("open", json!({}));
    // Anki creates the implicit parent deck collapsed; expanding shows children.
    let decks = ok("deck_tree", json!({}));
    let spanish = deck_id(&decks, "Spanish");
    if decks["decks"][0]["collapsed"] == true {
        assert_eq!(decks["decks"].as_array().unwrap().len(), 1);
        ok("set_collapsed", json!({ "deck_id": spanish, "collapsed": false }));
    }
    let decks = ok("deck_tree", json!({}));
    let verbs = deck_id(&decks, "Verbs");
    let verbs_entry = &decks["decks"].as_array().unwrap()[1];
    assert_eq!(verbs_entry["level"], 2);
    assert_eq!(verbs_entry["new"], 3);
    assert_eq!(decks["decks"][0]["has_children"], true);

    // Study.
    ok("select_deck", json!({ "deck_id": verbs }));
    let card = ok("next_card", json!({ "night_mode": true }));
    assert_eq!(card["done"], false);
    assert_eq!(card["counts"]["new"], 3);
    assert_eq!(card["queue"], "New");
    assert_eq!(card["buttons"].as_array().unwrap().len(), 4);
    for label in card["buttons"].as_array().unwrap() {
        let label = label.as_str().unwrap();
        assert!(!label.contains(['\u{2068}', '\u{2069}']), "bidi marks in {label:?}");
        assert!(label.chars().any(|c| c.is_ascii_digit()), "{label:?}");
    }
    assert!(card["body_class"].as_str().unwrap().contains("nightMode"));
    assert!(card["css"].as_str().unwrap().contains(".card"));
    let question = card["question"].as_str().unwrap();
    assert!(question.contains("hablar"), "{question}");
    assert!(question.contains(r#"href="playsound:q:0""#), "{question}");
    assert!(!question.contains("[sound:"), "{question}");
    assert_eq!(card["question_av"][0], json!({ "kind": "sound", "file": "hablar.mp3" }));
    assert!(card["answer"].as_str().unwrap().contains("to speak"));
    let first_card = card["card_id"].as_i64().unwrap();

    ok("answer", json!({ "ease": 3 }));
    assert_eq!(error_kind("answer", json!({ "ease": 3 })), "no_card");
    // Never synced: Anki's offline check can't tell, and reports no changes.
    assert_eq!(ok("status", json!({}))["sync_needed"], "none");
    let card = ok("next_card", json!({}));
    assert_ne!(card["card_id"].as_i64().unwrap(), first_card);
    assert_eq!(card["counts"]["new"], 2);
    assert!(!card["body_class"].as_str().unwrap().contains("nightMode"));

    // Undo puts the first card back.
    assert_eq!(ok("undo", json!({}))["undone"], true);
    let card = ok("next_card", json!({}));
    assert_eq!(card["card_id"].as_i64().unwrap(), first_card);
    assert_eq!(card["counts"]["new"], 3);
    assert_eq!(error_kind("answer", json!({ "ease": 7 })), "args");

    // Finish the deck: Easy graduates new cards out of today's queue.
    let mut answered = 0;
    loop {
        let card = ok("next_card", json!({}));
        if card["done"] == true {
            break;
        }
        ok("answer", json!({ "ease": 4 }));
        answered += 1;
        assert!(answered < 20, "deck never finished");
    }
    assert_eq!(answered, 3);
    let decks = ok("deck_tree", json!({}));
    assert_eq!(decks["decks"][1]["new"], 0);

    // Collapsing a deck hides its children.
    ok("set_collapsed", json!({ "deck_id": spanish, "collapsed": true }));
    let decks = ok("deck_tree", json!({}));
    assert_eq!(decks["decks"].as_array().unwrap().len(), 1);
    assert_eq!(decks["decks"][0]["collapsed"], true);

    // Sync needs a login; nothing touches the network here.
    assert_eq!(error_kind("sync", json!({})), "auth");
    assert_eq!(ok("media_sync_status", json!({}))["active"], false);
    assert_eq!(error_kind("bogus", json!({})), "unknown_command");

    // Closing backs up the changed collection, then reopening keeps the reviews.
    ok("close", json!({}));
    let backups: Vec<_> = std::fs::read_dir(base.join("backups"))
        .unwrap()
        .map(|e| e.unwrap().path())
        .collect();
    assert_eq!(backups.len(), 1, "expected a backup on close: {backups:?}");
    // Anki's own importer restores it. (Not under the Horizon emulator: the
    // importer itself reopens a temp file it holds open, which Horizon refuses.)
    let emulated = std::env::var("LD_PRELOAD").is_ok_and(|v| v.contains("horizon_emu"));
    if !emulated {
        let restored = dir.path().join("restored");
        std::fs::create_dir_all(restored.join("media")).unwrap();
        let progress = CollectionBuilder::new(restored.join("scratch.anki2"))
            .build()
            .unwrap()
            .new_progress_handler();
        anki::import_export::package::import_colpkg(
            backups[0].to_str().unwrap(),
            restored.join("collection.anki2").to_str().unwrap(),
            &restored.join("media"),
            &restored.join("media.db"),
            progress,
        )
        .expect("backup restores");
        let mut col = CollectionBuilder::new(restored.join("collection.anki2")).build().unwrap();
        assert!(!col.search_cards("", anki::search::SortMode::NoOrder).unwrap().is_empty());
    }
    ok("open", json!({}));
    let decks = ok("deck_tree", json!({}));
    assert_eq!(decks["decks"][0]["collapsed"], true);

    // App exit: closes the collection and stops the runtime; init starts over.
    ok("shutdown", json!({}));
    assert_eq!(error_kind("status", json!({})), "not_initialized");
    ok("shutdown", json!({})); // idempotent
    let status = ok("init", json!({ "base_dir": base, "langs": ["en"] }));
    assert_eq!(status["open"], false);
    ok("shutdown", json!({}));

    // A collection saved in WAL mode (as AnkiWeb sends it on a full download,
    // or as Anki desktop leaves it) opens, although SQLite is built without WAL.
    let col_path = base.join("collection.anki2");
    let mut data = std::fs::read(&col_path).unwrap();
    data[18] = 2;
    data[19] = 2;
    std::fs::write(&col_path, data).unwrap();
    ok("init", json!({ "base_dir": base, "langs": ["en"] }));
    ok("open", json!({}));
    let decks = ok("deck_tree", json!({}));
    assert_eq!(decks["decks"].as_array().unwrap().len(), 1);
    ok("shutdown", json!({}));

    // {{type:Back}} cards: an answer box instead of "[[type:Back]]", and Anki's
    // comparison of the typed answer on the answer side.
    let typing = dir.path().join("typing");
    std::fs::create_dir_all(&typing).unwrap();
    {
        let mut col = CollectionBuilder::new(typing.join("collection.anki2")).build().unwrap();
        let deck = col.get_or_create_normal_deck("Typing").unwrap();
        let notetype = col.get_notetype_by_name("Basic (type in the answer)").unwrap().unwrap();
        let mut note = notetype.new_note();
        note.set_field(0, "to speak").unwrap();
        note.set_field(1, "hablar").unwrap();
        col.add_note(&mut note, deck.id).unwrap();
        // Image Occlusion: two masks, the other one hidden too ("oi=1").
        let image = typing.join("picture.png");
        std::fs::write(&image, b"not decoded by the backend").unwrap();
        let deck = col.get_or_create_normal_deck("Occlusion").unwrap();
        col.set_current_deck(deck.id).unwrap();
        col.add_image_occlusion_note(anki_proto::image_occlusion::AddImageOcclusionNoteRequest {
            image_path: image.to_string_lossy().into(),
            occlusions: "{{c1::image-occlusion:rect:left=.1:top=.2:width=.3:height=.4:oi=1}}\
                         {{c2::image-occlusion:ellipse:left=.5:top=.5:rx=.1:ry=.2:oi=1}}"
                .into(),
            header: String::new(),
            back_extra: String::new(),
            tags: vec![],
            notetype_id: 0,
        })
        .unwrap();
        col.close(None).unwrap();
    }
    ok("init", json!({ "base_dir": typing, "langs": ["en"] }));
    ok("open", json!({}));
    let decks = ok("deck_tree", json!({}));
    ok("select_deck", json!({ "deck_id": deck_id(&decks, "Typing") }));
    let card = ok("next_card", json!({}));
    assert_eq!(card["type_answer"], true);
    let question = card["question"].as_str().unwrap();
    assert!(!question.contains("[[type:"), "{question}");
    assert!(question.contains(card["typed_marker"].as_str().unwrap()), "{question}");
    assert!(question.contains(r#"href="anki-typeans:""#), "{question}");
    let answer = card["answer"].as_str().unwrap();
    assert!(!answer.contains("[[type:") && answer.contains("typeans"), "{answer}");
    let right = ok("type_answer", json!({ "typed": "hablar" }));
    assert!(right["answer"].as_str().unwrap().contains("typeGood"), "{right}");
    let wrong = ok("type_answer", json!({ "typed": "hablo" }));
    assert!(wrong["answer"].as_str().unwrap().contains("typeBad"), "{wrong}");
    ok("answer", json!({ "ease": 3 }));
    assert!(card["occlusions"].is_null());

    // Image Occlusion: masks for the app to draw, image marked for the view.
    ok("select_deck", json!({ "deck_id": deck_id(&decks, "Occlusion") }));
    let card = ok("next_card", json!({}));
    let io = &card["occlusions"];
    let question: Vec<_> = io["question"].as_array().unwrap().iter().collect();
    assert_eq!(question.len(), 2, "{io}");
    let active: Vec<_> = question.iter().filter(|s| s["state"] == "active").collect();
    assert_eq!(active.len(), 1, "{io}");
    assert!(question.iter().all(|s| s["occlude_inactive"] == true));
    assert!(io["answer"].as_array().unwrap().iter().any(|s| s["state"] == "highlight"), "{io}");
    assert_eq!(io["can_toggle"], true);
    let html = card["question"].as_str().unwrap();
    assert!(html.contains("#ankinx-io\""), "{html}");
    assert!(!html.contains("<canvas"), "{html}");
    ok("shutdown", json!({}));
}
