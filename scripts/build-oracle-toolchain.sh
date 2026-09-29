#!/bin/sh
# Build the oracle frontend from this checkout's tlsf-tools submodule.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
source_dir="$root/subprojects/tlsf-tools"
build_dir="$root/build_oracle_tlsf"
actual=$(git -C "$source_dir" rev-parse HEAD)
oxidd_commit=$(git -C "$source_dir/external/oxidd" rev-parse HEAD)
expected_oxidd=$(git -C "$source_dir" rev-parse HEAD:external/oxidd)
if [ "$oxidd_commit" != "$expected_oxidd" ]; then
  echo "oracle toolchain: OxiDD is at $oxidd_commit, expected $expected_oxidd" >&2
  exit 1
fi
CARGO_NET_OFFLINE=true "$source_dir/scripts/build_oxidd.sh"
mkdir -p "$build_dir"
mkdir -p "$build_dir/tmp"
export TMPDIR="$build_dir/tmp"
if [ ! -f "$build_dir/build.ninja" ]; then
  meson setup "$build_dir" "$source_dir" --buildtype=release \
    -Doxidd=enabled -Dresearch_tools=true -Dcpu=baseline --wrap-mode=nodownload
else
  meson setup "$build_dir" "$source_dir" --reconfigure --wrap-mode=nodownload
fi
ninja -j1 -C "$build_dir" tlsf2tlsf tlsf2ltl tlsfinfo tlsfsolve tlsfcertcheck
python3 -s - "$build_dir" "$actual" "$oxidd_commit" <<'PY'
import hashlib
import json
import pathlib
import sys

build, commit, oxidd_commit = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
names = ("tlsf2tlsf", "tlsf2ltl", "tlsfinfo", "tlsfsolve", "tlsfcertcheck")
data = {
    "source_commit": commit,
    "oxidd_commit": oxidd_commit,
    "sha256": {name: hashlib.sha256((build / name).read_bytes()).hexdigest() for name in names},
}
(build / "acacia-toolchain.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
PY
echo "oracle toolchain built in $build_dir"
