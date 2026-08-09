#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 BUILD-DIR" >&2
  exit 2
}

[[ $# -eq 1 ]] || usage

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(realpath "$1")
expected="$repo_root/tests/suites/benchmarks/regress-expected.tsv"
testlog="$build_dir/meson-logs/testlog.json"
scratch=$(mktemp -d /tmp/acacia-regression-gate.XXXXXX)
trap 'rm -rf "$scratch"' EXIT

if [[ ! -x "$build_dir/src/acacia-bonsai" ]]; then
  echo "GATE FAIL: $build_dir/src/acacia-bonsai is not executable"
  exit 1
fi
if [[ ! -f "$expected" ]]; then
  echo "GATE FAIL: missing $expected"
  exit 1
fi

rm -f "$testlog"
set +e
env \
  MESON_TESTTHREADS=1 \
  BENCHMARK_TEST_JOBS=1 \
  BENCHMARK_CGROUP=strict \
  BENCHMARK_CGROUP_SCOPE=solver \
  BENCHMARK_CGROUP_MEMORY_MAX=8G \
  BENCHMARK_CGROUP_SWAP_MAX=0 \
  ACACIA_TEST_CGROUP=1 \
  ACACIA_TEST_CGROUP_MEMORY_MAX=8G \
  ACACIA_TEST_CGROUP_SWAP_MAX=0 \
  ACACIA_TEST_RESOURCE_UNKNOWN=1 \
  meson test -C "$build_dir" --no-rebuild --benchmark --num-processes 1 -t 1.7 \
    --suite=ab/syntcomp24/regress --suite=ab/syntcomp25/regress \
    >"$scratch/meson.log" 2>&1
meson_status=$?
set -e

if [[ ! -s "$testlog" ]]; then
  tail -80 "$scratch/meson.log"
  echo "GATE FAIL: Meson did not produce $testlog"
  exit 1
fi

set +e
python3 - "$expected" "$testlog" "$meson_status" <<'PY'
import csv
import json
import pathlib
import re
import sys


expected_path = pathlib.Path(sys.argv[1])
testlog_path = pathlib.Path(sys.argv[2])
meson_status = int(sys.argv[3])
verdict_re = re.compile(r"(?:^|\]\s)(UNREALIZABLE|REALIZABLE)\s*$", re.MULTILINE)


def verdict(stdout):
    matches = verdict_re.findall(stdout or "")
    return matches[-1] if matches and len(set(matches)) == 1 else None


expected = {}
with expected_path.open(newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        key = (row["suite"], row["instance"])
        if key in expected:
            raise SystemExit(f"GATE FAIL: duplicate expected row {key[0]}/{key[1]}")
        expected[key] = row["verdict"]

observed = {}
problems = []
with testlog_path.open() as handle:
    for line_no, line in enumerate(handle, start=1):
        row = json.loads(line)
        command = row.get("command") or []
        if "-F" not in command:
            problems.append(f"testlog line {line_no}: command has no -F input")
            continue
        instance_path = pathlib.Path(command[command.index("-F") + 1])
        key = (instance_path.parent.name, instance_path.name)
        answer = verdict(row.get("stdout"))
        result = row.get("result")
        if key in observed:
            problems.append(f"duplicate result {key[0]}/{key[1]}")
        observed[key] = (result, answer)

for key in sorted(expected.keys() - observed.keys()):
    problems.append(f"missing {key[0]}/{key[1]}")
for key in sorted(observed.keys() - expected.keys()):
    problems.append(f"unexpected {key[0]}/{key[1]}")
for key in sorted(expected.keys() & observed.keys()):
    result, answer = observed[key]
    want = expected[key]
    if result != "OK":
        problems.append(f"{key[0]}/{key[1]}: Meson result {result}, expected {want}")
    elif answer != want:
        problems.append(f"{key[0]}/{key[1]}: verdict {answer or 'MISSING'}, expected {want}")

if meson_status not in (0, 1):
    problems.append(f"Meson runner exited {meson_status}")
elif meson_status != 0 and not problems:
    problems.append("Meson exited 1 despite all parsed instances passing")

if problems:
    for problem in problems:
        print(f"- {problem}")
    print(f"GATE FAIL: {len(problems)} regression failure(s)")
    raise SystemExit(1)

print(f"verified {len(expected)} frozen verdicts")
print("GATE PASS")
PY
gate_status=$?
set -e

if (( gate_status != 0 )); then
  echo "--- Meson tail ---"
  tail -80 "$scratch/meson.log"
fi
exit "$gate_status"
