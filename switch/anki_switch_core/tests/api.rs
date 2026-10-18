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
    let overview = ok("deck_overview", json!({ "deck_id": verbs }));
    assert_eq!(overview["name"], "Spanish::Verbs");
    assert_eq!((overview["new"].clone(), overview["review"].clone()), (json!(3), json!(0)));
    assert_eq!((overview["description"].clone(), overview["filtered"].clone()), (json!(""), json!(false)));
    assert_eq!(error_kind("deck_overview", json!({ "deck_id": 12345 })), "no_deck");

    // Deck options: Anki's defaults, then a change read back and undone.
    // (Selecting a deck is undoable too: do it first.)
    ok("select_deck", json!({ "deck_id": verbs }));
    let options = ok("deck_options", json!({ "deck_id": verbs }));
    assert_eq!(options["preset"], "Default");
    assert_eq!(options["fsrs"], false);
    let values = &options["values"];
    assert_eq!(values["new_per_day"], 20);
    assert_eq!(values["learn_steps"], "1m 10m");
    assert_eq!(values["leech_action"], "tag");
    ok(
        "set_deck_options",
        json!({ "deck_id": verbs, "values": { "new_per_day": 5, "learn_steps": "30s 2h 1d", "bury_new": true } }),
    );
    let values = &ok("deck_options", json!({ "deck_id": verbs }))["values"];
    assert_eq!(values["new_per_day"], 5);
    assert_eq!(values["learn_steps"], "30s 2h 1d");
    assert_eq!(values["bury_new"], true);
    assert_eq!(values["answer_action"], "bury");
    ok(
        "set_deck_options",
        json!({ "deck_id": verbs, "values": { "answer_action": "good", "seconds_to_show_answer": 4.5, "show_timer": true } }),
    );
    let values = &ok("deck_options", json!({ "deck_id": verbs }))["values"];
    assert_eq!((values["answer_action"].clone(), values["seconds_to_show_answer"].clone()), (json!("good"), json!(4.5)));
    let timing = &ok("next_card", json!({}))["timing"];
    assert_eq!((timing["show_timer"].clone(), timing["answer_secs"].clone()), (json!(true), json!(4.5)));
    assert_eq!(ok("undo", json!({}))["undone"], true);
    for bad in [json!({ "learn_steps": "1x" }), json!({ "desired_retention": 1.5 }), json!({ "bogus": 1 })] {
        assert_eq!(error_kind("set_deck_options", json!({ "deck_id": verbs, "values": bad })), "args");
    }
    assert_eq!(ok("undo", json!({}))["undone"], true);
    assert_eq!(ok("deck_options", json!({ "deck_id": verbs }))["values"]["new_per_day"], 20);

    // Custom study: two new cards in a "Custom Study Session" deck, undone.
    let defaults = ok("custom_study_defaults", json!({ "deck_id": verbs }));
    assert_eq!(defaults["available_new"], 3, "{defaults}"); // the deck's new cards
    let session = ok("custom_study", json!({ "deck_id": verbs, "kind": "cram_new", "value": 2 }));
    assert_eq!(session["name"], "Custom Study Session");
    // (Building it makes it the current deck, as in Anki.)
    assert_eq!(ok("next_card", json!({}))["counts"]["new"], 2);
    assert_eq!(ok("deck_overview", json!({ "deck_id": session["deck_id"] }))["filtered"], true);
    assert_eq!(error_kind("custom_study", json!({ "deck_id": verbs, "kind": "bogus", "value": 1 })), "args");
    assert_eq!(ok("undo", json!({}))["undone"], true);
    assert_eq!(error_kind("deck_overview", json!({ "deck_id": session["deck_id"] })), "no_deck");
    ok("select_deck", json!({ "deck_id": verbs }));

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
    // Redo answers it again; undo once more for the checks below.
    assert_eq!(ok("redo", json!({}))["redone"], true);
    assert_eq!(ok("next_card", json!({}))["counts"]["new"], 2);
    assert_eq!(ok("undo", json!({}))["undone"], true);
    let card = ok("next_card", json!({}));
    assert_eq!(card["card_id"].as_i64().unwrap(), first_card);
    assert_eq!(error_kind("answer", json!({ "ease": 7 })), "args");

    // Card menu: flag and mark show on the card; card info.
    let cid = card["card_id"].as_i64().unwrap();
    assert_eq!((card["flag"].clone(), card["marked"].clone()), (json!(0), json!(false)));
    ok("card_action", json!({ "card_id": cid, "action": "flag", "flag": 3 }));
    let marked = ok("card_action", json!({ "card_id": cid, "action": "toggle_mark" }));
    assert_eq!(marked["marked"], true);
    let card = ok("next_card", json!({}));
    assert_eq!(card["card_id"], cid);
    assert_eq!((card["flag"].clone(), card["marked"].clone()), (json!(3), json!(true)));
    let info = ok("card_info", json!({ "card_id": cid }));
    assert_eq!(info["deck"], "Spanish::Verbs");
    assert_eq!(info["notetype"], "Basic");
    assert_eq!(info["reviews"], 0);
    assert!(info["revlog"].as_array().unwrap().is_empty(), "{info}");
    assert!(info["added"].as_i64().unwrap() > 0);
    // Actions that take the card out of today's queue; each one is undoable.
    for action in ["bury_card", "bury_note", "suspend_card", "suspend_note", "set_due_date"] {
        ok("card_action", json!({ "card_id": cid, "action": action, "days": "3" }));
        let next = ok("next_card", json!({}));
        assert_ne!(next["card_id"], cid, "{action} left the card in the queue");
        assert_eq!(next["counts"]["new"], 2, "{action}");
        assert_eq!(ok("undo", json!({}))["undone"], true);
        assert_eq!(ok("next_card", json!({}))["card_id"], cid, "undo {action}");
    }
    ok("card_action", json!({ "card_id": cid, "action": "forget" }));
    assert_eq!(ok("undo", json!({}))["undone"], true);
    assert_eq!(error_kind("card_action", json!({ "card_id": cid, "action": "bogus" })), "args");
    assert_eq!(error_kind("card_action", json!({ "card_id": 1, "action": "flag", "flag": 1 })), "no_card");
    // Undo the mark, then the flag.
    ok("undo", json!({}));
    ok("undo", json!({}));
    let card = ok("next_card", json!({}));
    assert_eq!((card["flag"].clone(), card["marked"].clone()), (json!(0), json!(false)));
    let answered = ok("answer", json!({ "ease": 3 }));
    assert_eq!((answered["leech"].clone(), answered["suspended"].clone()), (json!(false), json!(false)));
    assert_eq!(ok("undo", json!({}))["undone"], true);

    // Finish the deck: Easy graduates new cards out of today's queue.
    let mut answered = 0;
    loop {
        let card = ok("next_card", json!({}));
        if card["done"] == true {
            let congrats = &card["congrats"];
            assert_eq!(congrats["learn_remaining"], 0, "{card}");
            assert_eq!(congrats["secs_until_next_learn"], Value::Null, "{card}");
            assert_eq!(congrats["new_limit_reached"], false, "{card}");
            assert_eq!(congrats["buried"], false, "{card}");
            break;
        }
        ok("answer", json!({ "ease": 4 }));
        answered += 1;
        assert!(answered < 20, "deck never finished");
    }
    assert_eq!(answered, 3);

    // Statistics: the three answers show today, in the heatmap's last day and
    // as cards due later (Easy graduated them).
    for args in [json!({ "deck_id": verbs }), json!({})] {
        let stats = ok("stats", args);
        assert_eq!(stats["today"]["answers"], 3, "{stats}");
        assert_eq!(stats["heatmap"].as_array().unwrap().last(), Some(&json!(3)));
        assert_eq!(stats["forecast"].as_array().unwrap().len(), 31);
        let due_later: u64 = stats["forecast"].as_array().unwrap().iter().map(|n| n.as_u64().unwrap()).sum();
        assert_eq!(due_later, 3, "{stats}");
        assert_eq!(stats["cards"]["new"], 0);
        let studied = stats["studied_today"].as_str().unwrap();
        assert!(studied.contains('3') && !studied.contains(['\u{2068}', '\u{2069}']), "{stats}");
    }
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
        let mut col = CollectionBuilder::new(typing.join("collection.anki2"))
            .set_media_paths(typing.join("collection.media"), typing.join("collection.mdb"))
            .build()
            .unwrap();
        let deck = col.get_or_create_normal_deck("Typing").unwrap();
        let notetype = col.get_notetype_by_name("Basic (type in the answer)").unwrap().unwrap();
        let mut note = notetype.new_note();
        note.set_field(0, "to speak").unwrap();
        note.set_field(1, "hablar").unwrap();
        col.add_note(&mut note, deck.id).unwrap();
        // Maths, as MathJax delimiters in a field.
        let basic = col.get_notetype_by_name("Basic").unwrap().unwrap();
        let deck = col.get_or_create_normal_deck("Maths").unwrap();
        let mut note = basic.new_note();
        note.set_field(0, r"Roots of \(ax^2+bx+c\)?").unwrap();
        note.set_field(1, r"\[x = \frac{-b \pm \sqrt{b^2-4ac}}{2a}\]").unwrap();
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

    // Maths: rendered to cached SVGs, referenced by absolute path.
    ok("select_deck", json!({ "deck_id": deck_id(&decks, "Maths") }));
    let card = ok("next_card", json!({}));
    let question = card["question"].as_str().unwrap();
    assert!(question.contains("class=\"ankinx-math\"") && !question.contains("\\("), "{question}");
    let answer = card["answer"].as_str().unwrap();
    assert!(answer.contains("ankinx-math-display"), "{answer}");
    let svgs = std::fs::read_dir(typing.join("cache/math")).unwrap().count();
    assert!(svgs >= 2, "expected cached maths, found {svgs} files");
    ok("answer", json!({ "ease": 3 }));

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
