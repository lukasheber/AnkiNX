//! RWKV as the app runs it, on a PC: open (RWKV starts), wait until it is
//! ready, sync, then the study screen's calls, answering a few cards; prints
//! the backend's log (core.log) and RWKV's status at each step. For finding
//! why something seen on the console happens.
//! Usage: rwkv_probe <data dir> <models dir> [answers]
//! (<data dir> holds collection.anki2 and, to sync, the saved login sync.json)

use std::ffi::CStr;
use std::ffi::CString;
use std::time::Duration;
use std::time::Instant;

use anki_switch_core::ankinx_call;
use anki_switch_core::ankinx_free;
use serde_json::json;
use serde_json::Value;

fn call(cmd: &str, args: Value) -> Value {
    let started = Instant::now();
    let c_cmd = CString::new(cmd).unwrap();
    let c_args = CString::new(args.to_string()).unwrap();
    let value: Value = unsafe {
        let out = ankinx_call(c_cmd.as_ptr(), c_args.as_ptr());
        let value = serde_json::from_str(CStr::from_ptr(out).to_str().unwrap()).unwrap();
        ankinx_free(out);
        value
    };
    let text = value.to_string();
    println!("{cmd} ({:.2}s): {}", started.elapsed().as_secs_f32(), &text[..text.len().min(300)]);
    value
}

fn main() {
    let mut args = std::env::args().skip(1);
    let dir = args.next().expect("usage: rwkv_probe <data dir> <models dir> [answers]");
    let models = args.next().expect("models dir");
    let answers: usize = args.next().map_or(5, |n| n.parse().unwrap());
    call("init", json!({ "base_dir": dir, "langs": ["en"], "rwkv_models": models }));
    call("open", json!({}));
    let started = Instant::now();
    loop {
        let status = call("rwkv_status", json!({}));
        if status["ok"]["state"] != "loading" {
            break;
        }
        assert!(started.elapsed() < Duration::from_secs(900));
        std::thread::sleep(Duration::from_secs(2));
    }
    call("sync", json!({}));
    call("rwkv_status", json!({}));
    let tree = call("deck_tree", json!({}));
    let deck = tree["ok"]["decks"][0]["id"].clone();
    call("status", json!({}));
    call("select_deck", json!({ "deck_id": deck }));
    for _ in 0..answers {
        let card = call("next_card", json!({}));
        if !card["ok"]["card_id"].is_i64() {
            break;
        }
        call("answer", json!({ "ease": 3 }));
    }
    call("rwkv_status", json!({}));
    call("close", json!({}));
    let log = std::fs::read_to_string(std::path::Path::new(&dir).join("core.log")).unwrap_or_default();
    println!("---- core.log\n{log}");
}
