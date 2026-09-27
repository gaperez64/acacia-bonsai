#!/bin/sh
# Build the oracle frontend from this checkout's pinned tlsf-tools submodule.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
source_dir="$root/subprojects/tlsf-tools"
build_dir="$root/build_oracle_tlsf"
expected=$(git -C "$root" rev-parse HEAD:subprojects/tlsf-tools)
actual=$(git -C "$source_dir" rev-parse HEAD)
if [ "$actual" != "$expected" ]; then
  echo "oracle toolchain: tlsf-tools is at $actual, expected $expected" >&2
  exit 1
fi
"$source_dir/scripts/build_oxidd.sh" --verify
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
python3 -s - "$build_dir" "$actual" "$source_dir" <<'PY'
import hashlib
import json
import pathlib
import sys

build, commit, source = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
names = ("tlsf2tlsf", "tlsf2ltl", "tlsfinfo", "tlsfsolve", "tlsfcertcheck")
data = {
    "source_commit": commit,
    "oxidd_patch_record": (source / "external/oxidd/build/oxidd-gc-thread-retirement-info.txt").read_text().strip(),
    "sha256": {name: hashlib.sha256((build / name).read_bytes()).hexdigest() for name in names},
}
(build / "acacia-toolchain.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
PY
echo "oracle toolchain built in $build_dir"
