#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 [--baseline-bin BIN] BUILD-DIR" >&2
  exit 2
}

baseline_bin="${REGRESSION_BASELINE_BIN:-}"
if [[ ${1:-} == --baseline-bin ]]; then
  [[ $# -ge 3 ]] || usage
  baseline_bin=$(realpath "$2")
  shift 2
fi
[[ $# -eq 1 ]] || usage

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(realpath "$1")
baseline_bin=${baseline_bin:-$repo_root/build_best_decomp_mona/src/acacia-bonsai}
expected="$repo_root/tests/suites/benchmarks/regress-expected.tsv"
testlog="$build_dir/meson-logs/testlog.json"
scratch=$(mktemp -d /tmp/acacia-regression-gate.XXXXXX)
trap 'rm -rf "$scratch"' EXIT

outer_cgroup=${REGRESSION_OUTER_CGROUP:-0}
if [[ $outer_cgroup == 1 || $outer_cgroup == true || $outer_cgroup == yes || $outer_cgroup == on ]]; then
  benchmark_cgroup=0
  test_cgroup=0
  export ACACIA_OUTER_CGROUP=1
else
  benchmark_cgroup=strict
  test_cgroup=1
fi

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
  BENCHMARK_CGROUP="$benchmark_cgroup" \
  BENCHMARK_CGROUP_SCOPE=solver \
  BENCHMARK_CGROUP_MEMORY_MAX=8G \
  BENCHMARK_CGROUP_SWAP_MAX=0 \
  ACACIA_TEST_CGROUP="$test_cgroup" \
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
python3 - "$expected" "$testlog" "$meson_status" \
  "$scratch/baseline.csv" "$scratch/candidate.csv" "$repo_root" <<'PY'
from collections import defaultdict
import csv
import json
import pathlib
import re
import sys


expected_path = pathlib.Path(sys.argv[1])
testlog_path = pathlib.Path(sys.argv[2])
meson_status = int(sys.argv[3])
baseline_csv = pathlib.Path(sys.argv[4])
candidate_csv = pathlib.Path(sys.argv[5])
repo_root = pathlib.Path(sys.argv[6])
verdict_re = re.compile(r"(?:^|\]\s)(UNREALIZABLE|REALIZABLE)\s*$", re.MULTILINE)


def verdict(stdout):
    matches = verdict_re.findall(stdout or "")
    return matches[-1] if matches and len(set(matches)) == 1 else None


expected = {}
expected_sources = {}
source_maps = {}
with expected_path.open(newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        key = (row["suite"], row["instance"])
        if key in expected:
            raise SystemExit(f"GATE FAIL: duplicate expected row {key[0]}/{key[1]}")
        try:
            baseline_seconds = float(row["baseline_seconds"])
        except (KeyError, ValueError) as exc:
            raise SystemExit(
                f"GATE FAIL: invalid baseline_seconds for {key[0]}/{key[1]}"
            ) from exc
        expected[key] = (row["verdict"], baseline_seconds)

        source_map_path = expected_path.parent / key[0] / "sources.tsv"
        if source_map_path.is_file():
            if source_map_path not in source_maps:
                with source_map_path.open(newline="") as source_handle:
                    source_maps[source_map_path] = {
                        source_row["instance"]: (
                            repo_root / "tests" / "ltl" / source_row["source"]
                        ).resolve()
                        for source_row in csv.DictReader(source_handle, delimiter="\t")
                    }
            try:
                expected_sources[key] = source_maps[source_map_path][key[1]]
            except KeyError as exc:
                raise SystemExit(
                    f"GATE FAIL: {source_map_path}: no source for {key[1]!r}"
                ) from exc
        else:
            expected_sources[key] = (
                repo_root / "tests" / "ltl" / key[0] / key[1]
            ).resolve()

source_to_expected = defaultdict(list)
for key, source in expected_sources.items():
    source_to_expected[source].append(key)

observed = {}
problems = []
with testlog_path.open() as handle:
    for line_no, line in enumerate(handle, start=1):
        row = json.loads(line)
        command = row.get("command") or []
        if "-F" not in command:
            problems.append(f"testlog line {line_no}: command has no -F input")
            continue
        instance_path = pathlib.Path(command[command.index("-F") + 1]).resolve()
        candidates = source_to_expected.get(instance_path, [])
        if not candidates:
            problems.append(
                f"testlog line {line_no}: unexpected input {instance_path}"
            )
            continue
        if len(candidates) == 1:
            key = candidates[0]
        else:
            # Content-addressed corpus entries may be shared across suites.  The
            # Meson test name preserves the logical filename and suite labels,
            # so use those to disambiguate without collapsing duplicate hashes.
            test_name = row.get("name") or ""
            marker = " - Acacia_Bonsai:ab/"
            logical_instance = (
                test_name.rsplit(marker, 1)[1] if marker in test_name else ""
            )
            suite_labels = set(test_name.split(" - ", 1)[0].split("+"))
            named_candidates = [
                candidate
                for candidate in candidates
                if candidate[1] == logical_instance
                and f"ab/{candidate[0]}" in suite_labels
            ]
            if len(named_candidates) != 1:
                rendered = ", ".join(
                    f"{suite}/{instance}" for suite, instance in candidates
                )
                problems.append(
                    f"testlog line {line_no}: cannot disambiguate {instance_path} "
                    f"among {rendered} from test name {test_name!r}"
                )
                continue
            key = named_candidates[0]
        answer = verdict(row.get("stdout"))
        result = row.get("result")
        if key in observed:
            problems.append(f"duplicate result {key[0]}/{key[1]}")
        duration = float(row.get("duration") or 0)
        stdout = row.get("stdout") or ""
        if answer is not None:
            outcome = answer
        elif result == "TIMEOUT":
            outcome = "TIMEOUT"
        elif "RESOURCE LIMIT" in stdout or "NO VERDICT" in stdout:
            outcome = "UNKNOWN"
        else:
            outcome = "ERROR"
        observed[key] = (outcome, duration, row.get("returncode", ""))

for key in sorted(expected.keys() - observed.keys()):
    problems.append(f"missing {key[0]}/{key[1]}")
for key in sorted(observed.keys() - expected.keys()):
    problems.append(f"unexpected {key[0]}/{key[1]}")
if meson_status not in (0, 1):
    problems.append(f"Meson runner exited {meson_status}")

if problems:
    for problem in problems:
        print(f"- {problem}")
    print(f"GATE FAIL: {len(problems)} regression failure(s)")
    raise SystemExit(1)

fields = ["suite", "instance", "result", "seconds", "exit"]
with baseline_csv.open("w", newline="") as handle:
    writer = csv.DictWriter(handle, fields)
    writer.writeheader()
    for (suite, instance), (answer, seconds) in sorted(expected.items()):
        writer.writerow(
            {"suite": suite, "instance": instance, "result": answer,
             "seconds": seconds, "exit": 0 if answer == "REALIZABLE" else 1}
        )
with candidate_csv.open("w", newline="") as handle:
    writer = csv.DictWriter(handle, fields)
    writer.writeheader()
    for (suite, instance), (answer, seconds, exit_code) in sorted(observed.items()):
        writer.writerow(
            {"suite": suite, "instance": instance, "result": answer,
             "seconds": seconds, "exit": exit_code}
        )

print(f"verified {len(expected)} frozen regression rows")
PY
parse_status=$?
set -e

if (( parse_status != 0 )); then
  echo "--- Meson tail ---"
  tail -80 "$scratch/meson.log"
  exit "$parse_status"
fi

python3 "$repo_root/benchmarking/landing-bar.py" \
  "$scratch/baseline.csv" "$scratch/candidate.csv" \
  --timeout 17 \
  --baseline-bin "$baseline_bin" \
  --candidate-bin "$build_dir/src/acacia-bonsai" \
  --source-map "syntcomp24=$repo_root/tests/suites/benchmarks/syntcomp24/sources.tsv" \
  --source-map "syntcomp25=$repo_root/tests/suites/benchmarks/syntcomp25/sources.tsv" \
  --memory-max 8G \
  --memory-swap-max 0
