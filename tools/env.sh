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
# The Anki source the core is built from, kept unmodified in source_code/$ANKI_DIR
# (tools/fetch_anki.sh puts it there; tools/sync.sh applies ANKI_PATCHES below
# to the work tree's copy). experimental/fsrs7: the unofficial FSRS-7 + RWKV fork of
# Anki 26.09.4. Cargo paths can't use variables: switch/anki_switch_core/Cargo.toml
# names the same directory.
export ANKI_DIR=anki-fsrs7
export ANKI_REPO=https://github.com/JSchoreels/anki.git
export ANKI_COMMIT=4ae68fa860f989bd30a67faec4f98cdd455ad9d4  # 26.09.4+fsrs7, after build 115
# Our changes to it (switch/patches/), applied in this order by tools/sync.sh:
# anki-rwkv.patch is kuma3-anki-core's RWKV layer (its `rwkv` branch,
# a8cf880) merged with this pin, see docs/fsrs7-plan.md; anki.patch is the
# media sync fix.
ANKI_PATCHES=(anki-rwkv.patch anki.patch)
# Project lives on the Windows drive; builds happen on the Linux filesystem (much faster).
export ANKISWITCH_SRC=/mnt/c/ankiswitch
export ANKISWITCH_WORK="${ANKISWITCH_WORK:-$HOME/ankiswitch}"
