//! Optimize a deck's preset as the console's Deck options does (`optimize`,
//! then `optimize_status` until it ends), on a PC, and print the result
//! (log loss and RMSE of the current and optimized parameters): for comparing
//! the console's optimizer with a PC's on the same collection. Nothing is
//! applied.
//! Usage: optimize_probe <data dir> <deck name>

use std::ffi::CStr;
use std::ffi::CString;
use std::time::Duration;
use std::time::Instant;

use anki_switch_core::ankinx_call;
use anki_switch_core::ankinx_free;
use serde_json::json;
use serde_json::Value;

fn call(cmd: &str, args: Value) -> Value {
    let c_cmd = CString::new(cmd).unwrap();
    let c_args = CString::new(args.to_string()).unwrap();
    unsafe {
        let out = ankinx_call(c_cmd.as_ptr(), c_args.as_ptr());
        let value = serde_json::from_str(CStr::from_ptr(out).to_str().unwrap()).unwrap();
        ankinx_free(out);
        value
    }
}

fn main() {
    let mut args = std::env::args().skip(1);
    let dir = args.next().expect("usage: optimize_probe <data dir> <deck name>");
    let name = args.next().expect("deck name");
    call("init", json!({ "base_dir": dir, "langs": ["en"] }));
    println!("open: {}", call("open", json!({})));
    let tree = call("deck_tree", json!({}));
    let deck = tree["ok"]["decks"]
        .as_array()
        .unwrap()
        .iter()
        .find(|d| d["name"] == name.as_str())
        .expect("no such deck")["id"]
        .clone();
    let options = call("deck_options", json!({ "deck_id": deck }));
    println!(
        "preset: fsrs_version={} params={} scheduler={} due_dates={}",
        options["ok"]["values"]["fsrs_version"],
        options["ok"]["fsrs_params"],
        options["ok"]["values"]["scheduler"],
        options["ok"]["values"]["due_dates"]
    );
    let started = Instant::now();
    println!("optimize: {}", call("optimize", json!({ "deck_id": deck })));
    loop {
        std::thread::sleep(Duration::from_millis(500));
        let status = call("optimize_status", json!({}));
        if status["ok"]["active"] == false {
            println!("result after {:.1}s: {}", started.elapsed().as_secs_f32(), status["ok"]["result"]);
            break;
        }
    }
    call("close", json!({}));
}
