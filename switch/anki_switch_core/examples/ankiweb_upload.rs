//! Upload a collection to an AnkiWeb account (replacing what is there),
//! through AnkiNX's own API: a test collection for the console to download.
//! Usage: ANKIWEB_USER=... ANKIWEB_PASS=... cargo run --example ankiweb_upload -- <dir>
//! (<dir>/collection.anki2; the login is saved as <dir>/sync.json, as AnkiNX saves it)

use std::ffi::CStr;
use std::ffi::CString;

use anki_switch_core::ankinx_call;
use anki_switch_core::ankinx_free;
use serde_json::json;
use serde_json::Value;

fn call(cmd: &str, args: Value) -> Value {
    let cmd = CString::new(cmd).unwrap();
    let args = CString::new(args.to_string()).unwrap();
    let value: Value = unsafe {
        let out = ankinx_call(cmd.as_ptr(), args.as_ptr());
        let value = serde_json::from_str(CStr::from_ptr(out).to_str().unwrap()).unwrap();
        ankinx_free(out);
        value
    };
    match value.get("ok") {
        Some(ok) => ok.clone(),
        None => panic!("{}: {}", cmd.to_string_lossy(), value),
    }
}

fn main() {
    let dir = std::env::args().nth(1).expect("usage: ankiweb_upload <dir>");
    let username = std::env::var("ANKIWEB_USER").expect("ANKIWEB_USER");
    let password = std::env::var("ANKIWEB_PASS").expect("ANKIWEB_PASS");
    call("init", json!({ "base_dir": dir, "langs": ["en"] }));
    call("open", json!({}));
    call("login", json!({ "username": username, "password": password }));
    let started = std::time::Instant::now();
    // A normal sync first, as the app does: it finds the account's server
    // (AnkiWeb answers a full upload sent elsewhere without a sync reply).
    let sync = call("sync", json!({}));
    println!("sync: {sync}");
    call("full_sync", json!({ "upload": true }));
    println!("uploaded in {:.1} s", started.elapsed().as_secs_f32());
    let status = call("status", json!({}));
    println!("logged in as {}, changes left: {}", status["username"], status["sync_needed"]);
    call("shutdown", json!({}));
}
