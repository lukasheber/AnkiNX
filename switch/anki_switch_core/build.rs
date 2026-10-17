// Compile the C shims into this static library so the frontend only has to
// link libanki_switch_core.a (+ libnx on the Switch):
// - shim/sqlite_vfs.c, everywhere (SQLite VFS, see the file);
// - shim/nx_posix.c, on the Switch (POSIX gaps and fixes in newlib/libnx).
fn main() {
    println!("cargo:rerun-if-changed=shim/sqlite_vfs.c");
    println!("cargo:rerun-if-changed=shim/nx_posix.c");
    println!("cargo:rerun-if-changed=shim/horizon_fixes.h");

    // Set by libsqlite3-sys (links = "sqlite3") for its bundled sqlite3.h.
    let sqlite_include = std::env::var("DEP_SQLITE3_INCLUDE").expect("DEP_SQLITE3_INCLUDE");
    cc::Build::new()
        .file("shim/sqlite_vfs.c")
        .include(sqlite_include)
        .warnings(true)
        // VFS callbacks have fixed signatures; most ignore some arguments.
        .flag("-Wno-unused-parameter")
        .compile("ankinx_sqlite_vfs");

    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("horizon") {
        return;
    }
    let devkitpro = std::env::var("DEVKITPRO").unwrap_or_else(|_| "/opt/devkitpro".into());
    cc::Build::new()
        .file("shim/nx_posix.c")
        .include(format!("{devkitpro}/libnx/include"))
        .define("__SWITCH__", None)
        .warnings(true)
        // The ENOSYS stubs ignore their arguments by design.
        .flag("-Wno-unused-parameter")
        .compile("nx_posix");
}
