//! Run the app's AnkiWeb flow (login, sync, full download) against a real
//! account, through the same C entry points the Switch frontend uses.
//! Usage: ANKIWEB_USER=... ANKIWEB_PASS=... sync_probe <data dir> [upload]
//! Run it under shim/horizon_emu.c (LD_PRELOAD) to emulate the Switch's
//! filesystem. Never prints the password or the saved host key.
//! Delete the collection first (or pass a new dir) to get a full download.

use std::ffi::CStr;
use std::ffi::CString;
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
    println!("{cmd} ({:.1}s): {value}", started.elapsed().as_secs_f32());
    value
}

fn main() {
    // Call from a thread with the stack the Switch frontend's calls get:
    // borealis' task thread, created with libnx's default of 128 KiB.
    std::thread::Builder::new()
        .stack_size(128 << 10)
        .spawn(probe)
        .unwrap()
        .join()
        .unwrap();
}

fn probe() {
    let dir = std::env::args().nth(1).expect("usage: sync_probe <data dir> [upload]");
    let upload = std::env::args().nth(2).as_deref() == Some("upload");
    let user = std::env::var("ANKIWEB_USER").expect("ANKIWEB_USER");
    let pass = std::env::var("ANKIWEB_PASS").expect("ANKIWEB_PASS");

    call("init", json!({ "base_dir": dir, "langs": ["en"] }));
    call("open", json!({}));
    if call("status", json!({}))["ok"]["username"].is_null() {
        let login = call("login", json!({ "username": user, "password": pass }));
        assert!(login.get("ok").is_some(), "login failed");
    }
    let sync = call("sync", json!({}));
    let required = sync["ok"]["required"].as_str().unwrap_or("error");
    if required != "none" {
        call("full_sync", json!({ "upload": upload }));
    }
    call("deck_tree", json!({}));
    // Let media sync run a little, then shut down as the app does on exit.
    for _ in 0..30 {
        std::thread::sleep(std::time::Duration::from_secs(1));
        if !call("media_sync_status", json!({}))["ok"]["active"]
            .as_bool()
            .unwrap_or(false)
        {
            break;
        }
    }
    call("shutdown", json!({}));
}
