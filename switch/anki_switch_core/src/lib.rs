//! C ABI over Anki's Rust backend for the Nintendo Switch frontend.
//!
//! Same protobuf-RPC boundary as Anki's Python bridge (`pylib/rsbridge`) and
//! AnkiDroid: the frontend sends serialized `anki_proto` messages, the backend
//! replies with serialized messages. See `include/anki_switch_core.h`.

mod api;
mod math;
mod occlusion;
mod update;

use std::ffi::c_char;
use std::ffi::CStr;
use std::ffi::CString;
use std::ptr;
use std::slice;

use anki::backend::init_backend;
use anki::backend::Backend;

/// Byte buffer owned by Rust; release with `anki_buffer_free`.
#[repr(C)]
pub struct AnkiBuffer {
    pub data: *mut u8,
    pub len: usize,
}

impl AnkiBuffer {
    fn from_vec(bytes: Vec<u8>) -> AnkiBuffer {
        let boxed = bytes.into_boxed_slice();
        let len = boxed.len();
        AnkiBuffer {
            data: Box::into_raw(boxed) as *mut u8,
            len,
        }
    }
}

pub const ANKI_OK: i32 = 0;
/// `out` holds a serialized `anki_proto::backend::BackendError`.
pub const ANKI_BACKEND_ERROR: i32 = 1;
/// Invalid arguments (null handle or output pointer); `out` is untouched.
pub const ANKI_INVALID_ARGUMENT: i32 = 2;

unsafe fn input<'a>(data: *const u8, len: usize) -> &'a [u8] {
    if data.is_null() || len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    }
}

unsafe fn write_result(out: *mut AnkiBuffer, result: Result<Vec<u8>, Vec<u8>>) -> i32 {
    match result {
        Ok(bytes) => {
            *out = AnkiBuffer::from_vec(bytes);
            ANKI_OK
        }
        Err(bytes) => {
            *out = AnkiBuffer::from_vec(bytes);
            ANKI_BACKEND_ERROR
        }
    }
}

/// Make SQLite's lock-free "unix-none" VFS the default. Horizon's libnx does not
/// provide POSIX advisory locks on files, and the collection is only ever opened
/// by this one process (Anki also uses `locking_mode=exclusive`). This VFS also
/// doesn't track inodes, which Horizon's stat() can't always provide (the
/// open-file fallback in shim/horizon_fixes.h reports inode 0; the default VFS
/// would then report the database as moved: SQLITE_READONLY_DBMOVED).
/// The VFS (shim/sqlite_vfs.c) wraps "unix-none" so that collections saved in
/// WAL mode (AnkiWeb downloads, copies from Anki desktop) can be opened.
fn use_lockless_sqlite_vfs() {
    extern "C" {
        fn ankinx_register_sqlite_vfs() -> i32;
    }
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| unsafe {
        // On failure SQLite keeps its default VFS; opening then reports the
        // problem as an error instead of the app aborting here.
        let rc = ankinx_register_sqlite_vfs();
        if rc != 0 {
            eprintln!("registering the SQLite VFS failed: {rc}");
        }
    });
}

/// Create a backend from a serialized `anki_proto::backend::BackendInit`.
/// Returns null on failure, with a UTF-8 message in `*err` if `err` is non-null.
#[no_mangle]
pub unsafe extern "C" fn anki_backend_open(
    init: *const u8,
    init_len: usize,
    err: *mut AnkiBuffer,
) -> *mut Backend {
    if cfg!(target_os = "horizon") {
        use_lockless_sqlite_vfs();
    }
    match init_backend(input(init, init_len)) {
        Ok(backend) => Box::into_raw(Box::new(backend)),
        Err(msg) => {
            if !err.is_null() {
                *err = AnkiBuffer::from_vec(msg.into_bytes());
            }
            ptr::null_mut()
        }
    }
}

/// Run backend service `service`, method `method` on a serialized request.
#[no_mangle]
pub unsafe extern "C" fn anki_backend_command(
    backend: *const Backend,
    service: u32,
    method: u32,
    data: *const u8,
    len: usize,
    out: *mut AnkiBuffer,
) -> i32 {
    let (Some(backend), false) = (backend.as_ref(), out.is_null()) else {
        return ANKI_INVALID_ARGUMENT;
    };
    write_result(
        out,
        backend.run_service_method(service, method, input(data, len)),
    )
}

/// Run a JSON-encoded database command (Anki's `db_command` protocol).
#[no_mangle]
pub unsafe extern "C" fn anki_backend_db_command(
    backend: *const Backend,
    data: *const u8,
    len: usize,
    out: *mut AnkiBuffer,
) -> i32 {
    let (Some(backend), false) = (backend.as_ref(), out.is_null()) else {
        return ANKI_INVALID_ARGUMENT;
    };
    write_result(out, backend.run_db_command_bytes(input(data, len)))
}

/// Destroy a backend created by `anki_backend_open`. Null is ignored.
#[no_mangle]
pub unsafe extern "C" fn anki_backend_close(backend: *mut Backend) {
    if !backend.is_null() {
        drop(Box::from_raw(backend));
    }
}

/// Free a buffer returned by this library. Null/empty buffers are ignored.
#[no_mangle]
pub unsafe extern "C" fn anki_buffer_free(buf: AnkiBuffer) {
    if !buf.data.is_null() {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(buf.data, buf.len)));
    }
}

/// Run a JSON API command (see src/api.rs). `args` may be null or a JSON
/// object. Returns a NUL-terminated JSON string, `{"ok": ...}` or
/// `{"error": "...", "kind": "..."}`; release it with `ankinx_free`.
#[no_mangle]
pub unsafe extern "C" fn ankinx_call(cmd: *const c_char, args: *const c_char) -> *mut c_char {
    let text = |p: *const c_char| {
        if p.is_null() {
            String::new()
        } else {
            CStr::from_ptr(p).to_string_lossy().into_owned()
        }
    };
    // On every platform, so host tests and the desktop preview use the same
    // SQLite I/O path as the Switch.
    use_lockless_sqlite_vfs();
    let response = match api::call(&text(cmd), &text(args)) {
        Ok(value) => serde_json::json!({ "ok": value }),
        Err(err) => err.to_json(),
    };
    // serde_json escapes control characters, so the output never contains NUL.
    CString::new(response.to_string())
        .unwrap_or_default()
        .into_raw()
}


/// Free a string returned by `ankinx_call`. Null is ignored.
#[no_mangle]
pub unsafe extern "C" fn ankinx_free(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

/// NUL-terminated build hash of the bundled Anki backend.
#[no_mangle]
pub extern "C" fn anki_buildhash() -> *const c_char {
    static HASH: std::sync::OnceLock<std::ffi::CString> = std::sync::OnceLock::new();
    HASH.get_or_init(|| std::ffi::CString::new(anki::version::buildhash()).unwrap_or_default())
        .as_ptr()
}
