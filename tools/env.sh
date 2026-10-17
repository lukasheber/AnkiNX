# Source this from build scripts: shared toolchain environment for the Switch port.
# shellcheck shell=bash
[[ -f "$HOME/.cargo/env" ]] && source "$HOME/.cargo/env"
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH="$HOME/.local/bin:$HOME/.cargo/bin:$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"
export RUST_NIGHTLY="${RUST_NIGHTLY:-nightly-2026-09-15}"
# Bundled SQLite as configured for Horizon (see switch/README.md): no mmap, so no
# WAL (Anki's journal_mode=wal falls back to a rollback journal), no /tmp, no
# dlopen. Shared by the Switch and host builds so host tests exercise the same
# SQLite I/O patterns as the console.
export LIBSQLITE3_FLAGS="-DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_TEMP_STORE=3 \
-USQLITE_ENABLE_LOAD_EXTENSION -DSQLITE_OMIT_LOAD_EXTENSION"
# Project lives on the Windows drive; builds happen on the Linux filesystem (much faster).
export ANKISWITCH_SRC=/mnt/c/ankiswitch
export ANKISWITCH_WORK="${ANKISWITCH_WORK:-$HOME/ankiswitch}"
