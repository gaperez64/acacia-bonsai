#!/bin/bash
# Prepare the pinned solver library before Meson setup, including wheel builds.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
oxidd_dir="$root/subprojects/tlsf-tools/external/oxidd"
if python3 "$root/subprojects/tlsf-tools/scripts/oxidd_stamp.py" check \
    "$oxidd_dir" "$oxidd_dir/target/release/liboxidd_ffi_c.a" \
    "$oxidd_dir/build/oxidd-build-stamp" >/dev/null 2>&1; then
    exit 0
fi
if [[ ! -f "$oxidd_dir/crates/oxidd-ffi-c/Cargo.toml" ]]; then
    echo "Initialize tlsf-tools' nested external/oxidd submodule before building." >&2
    exit 1
fi
export PATH="${CARGO_HOME:-$HOME/.cargo}/bin:$PATH"
if ! command -v rustup >/dev/null 2>&1; then
    curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.91.0
fi
rustup toolchain install 1.91.0 --profile minimal
export RUSTUP_TOOLCHAIN=1.91.0
if ! command -v cbindgen >/dev/null 2>&1 || [[ $(cbindgen --version) != 'cbindgen 0.29.4' ]]; then
    cargo install cbindgen --version 0.29.4 --locked -j 1
fi
exec "$root/subprojects/tlsf-tools/scripts/build_oxidd.sh"
