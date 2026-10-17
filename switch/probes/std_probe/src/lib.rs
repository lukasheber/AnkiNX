//! Probe: which parts of std are real on a given Switch target spec.

#[no_mangle]
pub extern "C" fn probe_spawn() -> i32 {
    std::thread::spawn(|| 1).join().unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn probe_is_unix() -> bool {
    cfg!(unix)
}

#[cfg(feature = "tokio")]
#[no_mangle]
pub extern "C" fn probe_tokio() -> bool {
    tokio::runtime::Builder::new_multi_thread().build().is_ok()
}
