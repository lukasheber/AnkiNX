#!/usr/bin/env bash
# Phase 1: provision the Linux (WSL2 Ubuntu 24.04) build host for the Anki Switch port.
#
# Installs:
#   - devkitPro pacman + switch-dev, switch-curl, switch-mbedtls, switch-zlib
#   - Rust nightly (pinned) + rust-src, and cargo-nx
#   - Python 3.13 (deadsnakes), exposed as the user's default `python`/`python3`
#   - host tools Anki's build needs (protoc, ninja, git, build-essential)
#
# Usage (inside Ubuntu, as your normal user; sudo will prompt):
#   bash /mnt/c/ankiswitch/tools/provision_phase1.sh
#
# Safe to re-run.

set -euo pipefail

RUST_NIGHTLY="${RUST_NIGHTLY:-nightly-2026-09-15}"
PY_VER="3.13"

log() { printf '\n==> %s\n' "$*"; }

if [[ "$(id -u)" -eq 0 ]]; then
    echo "Run as your normal user, not root (rustup and cargo-nx install per-user)." >&2
    exit 1
fi

# ---------------------------------------------------------------- apt base ---
log "Installing host build dependencies"
sudo apt-get update
sudo apt-get install -y \
    build-essential pkg-config cmake ninja-build git curl wget ca-certificates \
    gnupg software-properties-common protobuf-compiler libssl-dev rsync unzip

# ------------------------------------------------------------- Python 3.13 ---
# Ubuntu 24.04 ships 3.12 as /usr/bin/python3, which apt itself depends on, so
# we don't replace it system-wide. Instead 3.13 goes first on the user's PATH.
log "Installing Python ${PY_VER}"
if ! command -v "python${PY_VER}" >/dev/null; then
    sudo add-apt-repository -y ppa:deadsnakes/ppa
    sudo apt-get update
fi
sudo apt-get install -y "python${PY_VER}" "python${PY_VER}-venv" "python${PY_VER}-dev"

mkdir -p "$HOME/.local/bin"
ln -sf "$(command -v "python${PY_VER}")" "$HOME/.local/bin/python3"
ln -sf "$(command -v "python${PY_VER}")" "$HOME/.local/bin/python"

# --------------------------------------------------------------- devkitPro ---
# Preferred path: devkitPro's own pacman. Fallback: if devkitpro.org refuses us
# (its Cloudflare WAF has blocked some ISP IPs with a 403), copy /opt/devkitpro
# out of devkitPro's official Docker Hub image, which is built with switch-dev
# + switch-portlibs (includes switch-curl, switch-mbedtls, switch-zlib).
DKP_IMAGE="${DKP_IMAGE:-devkitpro/devkita64:20260219}"

install_dkp_via_pacman() {
    # WSL images sometimes lack /etc/mtab, which dkp-pacman needs.
    [[ -e /etc/mtab ]] || sudo ln -s /proc/self/mounts /etc/mtab
    if ! command -v dkp-pacman >/dev/null; then
        local tmp; tmp="$(mktemp -d)"
        wget -O "$tmp/install-devkitpro-pacman" https://apt.devkitpro.org/install-devkitpro-pacman || return 1
        chmod +x "$tmp/install-devkitpro-pacman"
        sudo "$tmp/install-devkitpro-pacman"
        rm -rf "$tmp"
    fi
    sudo dkp-pacman -Syu --noconfirm
    sudo dkp-pacman -S --needed --noconfirm switch-dev switch-curl switch-mbedtls switch-zlib
}

install_dkp_via_docker() {
    if ! command -v docker >/dev/null; then
        sudo apt-get install -y docker.io
    fi
    sudo service docker start >/dev/null 2>&1 || sudo systemctl start docker
    sudo docker pull "$DKP_IMAGE"
    sudo rm -rf /opt/devkitpro
    # Stream via tar rather than `docker cp`: the image contains symlinks that
    # point outside /opt/devkitpro (pacman keyrings), which `docker cp` rejects.
    sudo docker run --rm "$DKP_IMAGE" tar -C /opt -cf - devkitpro | sudo tar -C /opt -xf -
    # Record exactly which package versions this snapshot contains; its presence
    # also marks the copy as complete.
    sudo docker run --rm "$DKP_IMAGE" dkp-pacman -Q | sudo tee /opt/devkitpro/PACKAGES.txt >/dev/null
    sudo chown -R "$(id -u):$(id -g)" /opt/devkitpro
}

dkp_complete() {
    local f
    for f in libnx/lib/libnx.a portlibs/switch/lib/libcurl.a portlibs/switch/lib/libmbedtls.a \
             portlibs/switch/lib/libz.a tools/bin/elf2nro; do
        [[ -e "/opt/devkitpro/$f" ]] || return 1
    done
    command -v dkp-pacman >/dev/null || [[ -f /opt/devkitpro/PACKAGES.txt ]]
}

if dkp_complete; then
    log "devkitPro already present in /opt/devkitpro"
else
    log "Installing devkitPro (pacman)"
    if ! install_dkp_via_pacman; then
        log "devkitpro.org unreachable; falling back to official image ${DKP_IMAGE}"
        install_dkp_via_docker
    fi
fi

# ------------------------------------------------------------------- Rust ---
log "Installing Rust (${RUST_NIGHTLY}) + rust-src"
if ! command -v rustup >/dev/null && [[ ! -x "$HOME/.cargo/bin/rustup" ]]; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
fi
# shellcheck disable=SC1091
source "$HOME/.cargo/env"
rustup toolchain install "$RUST_NIGHTLY" --profile minimal --component rust-src
# Stable stays the default for Anki's own host build; the Switch crates pin
# the nightly via their rust-toolchain.toml in Phase 2.

log "Installing cargo-nx"
cargo +"$RUST_NIGHTLY" install --locked cargo-nx --git https://github.com/aarch64-switch-rs/cargo-nx

# ------------------------------------------------------------ shell env -----
PROFILE_SNIPPET='# >>> ankiswitch env >>>
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH="$HOME/.local/bin:$HOME/.cargo/bin:$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"
# <<< ankiswitch env <<<'
if ! grep -q '>>> ankiswitch env >>>' "$HOME/.bashrc"; then
    printf '\n%s\n' "$PROFILE_SNIPPET" >> "$HOME/.bashrc"
fi
eval "$(printf '%s' "$PROFILE_SNIPPET" | grep '^export')"

# ----------------------------------------------------------------- verify ---
log "Verification"
fail=0
check() {
    local name="$1"; shift
    if out="$("$@" 2>&1 | head -n1)"; then
        printf '  [ok]   %-22s %s\n' "$name" "$out"
    else
        printf '  [FAIL] %-22s %s\n' "$name" "$out"; fail=1
    fi
}
check "python3"            python3 --version
check "python3 is 3.13"    python3 -c "import sys; assert sys.version_info[:2]==(3,13), sys.version; print(sys.version.split()[0])"
check "aarch64-none-elf-gcc" aarch64-none-elf-gcc --version
check "elf2nro"            bash -c 'command -v elf2nro'
check "libnx"              bash -c "ls $DEVKITPRO/libnx/lib/libnx.a"
check "switch-curl"        bash -c "ls $DEVKITPRO/portlibs/switch/lib/libcurl.a"
check "switch-mbedtls"     bash -c "ls $DEVKITPRO/portlibs/switch/lib/libmbedtls.a"
check "switch-zlib"        bash -c "ls $DEVKITPRO/portlibs/switch/lib/libz.a"
check "rustc ($RUST_NIGHTLY)" rustc +"$RUST_NIGHTLY" --version
check "rust-src"           bash -c "rustup component list --toolchain $RUST_NIGHTLY --installed | grep rust-src"
check "cargo-nx"           bash -c 'command -v cargo-nx'
check "protoc"             protoc --version

if [[ $fail -ne 0 ]]; then
    echo; echo "Phase 1 provisioning finished with failures (see above)." >&2
    exit 1
fi
echo; echo "Phase 1 environment ready. Open a new shell (or 'source ~/.bashrc') to pick up the env."
