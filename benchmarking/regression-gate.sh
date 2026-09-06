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
scope_guard_outer=0
scope_snapshot="$scratch/scope-snapshot"
on_exit() {
  local rc=$?
  if (( scope_guard_outer == 1 )); then
    python3 "$repo_root/benchmarking/sweep-acacia-scopes.py" \
      --stop --snapshot "$scope_snapshot" || true
  fi
  rm -rf "$scratch"
  return "$rc"
}
trap on_exit EXIT

if [[ -z ${ACACIA_CAMPAIGN_SCOPE_GUARD:-} ]]; then
  if ! python3 "$repo_root/benchmarking/sweep-acacia-scopes.py" \
       --check --snapshot "$scope_snapshot"; then
    [[ ${ACACIA_ALLOW_STRAY_SCOPES:-0} == 1 ]] || exit 1
    echo "regression-gate: continuing with ACACIA_ALLOW_STRAY_SCOPES=1; measurements may be under contention" >&2
  fi
  export ACACIA_CAMPAIGN_SCOPE_GUARD="regression-gate:$$"
  scope_guard_outer=1
fi

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

# Resolve the corpus before Meson runs, not after.  Meson bakes
# acacia_tlsf_corpus_dir into each test's argv at configure time, so a build
# whose corpus has since moved cannot resolve its own -T inputs; exporting the
# resolved directory lets check-real-correct.sh find the same file under a live
# one.  See issue #134.
tlsf_corpus_dir=$(python3 -c '
import pathlib
import sys
sys.path.insert(0, sys.argv[1])
from benchlib import tlsf_corpus_dir
print(tlsf_corpus_dir(build_dir=pathlib.Path(sys.argv[2])) or "")
' "$repo_root/benchmarking" "$build_dir")

rm -f "$testlog"
set +e
env \
  ACACIA_TLSF_CORPUS="$tlsf_corpus_dir" \
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
  "$scratch/baseline.csv" "$scratch/candidate.csv" "$repo_root" \
  "$build_dir" "$scratch/tlsf-corpus-dir" "$tlsf_corpus_dir" <<'PY'
from collections import defaultdict
import csv
import json
import pathlib
import sys


expected_path = pathlib.Path(sys.argv[1])
testlog_path = pathlib.Path(sys.argv[2])
meson_status = int(sys.argv[3])
baseline_csv = pathlib.Path(sys.argv[4])
candidate_csv = pathlib.Path(sys.argv[5])
repo_root = pathlib.Path(sys.argv[6])
sys.path.insert(0, str(repo_root / "benchmarking"))
import benchlib
from benchlib import verdict_from_output

build_dir = pathlib.Path(sys.argv[7])
tlsf_corpus_out = pathlib.Path(sys.argv[8])


def verdict(stdout):
    return verdict_from_output(stdout, on_conflict="last")


# Resolved in the shell above, so the suites and this parse agree on one
# directory rather than each answering the question separately.
tlsf_corpus = pathlib.Path(sys.argv[9]).resolve() if sys.argv[9] else None


def load_map(path, value_field):
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        expected_header = ["instance", value_field]
        if reader.fieldnames != expected_header:
            rendered_header = "\t".join(expected_header)
            raise SystemExit(
                f"GATE FAIL: {path}: expected header "
                f"{rendered_header!r}"
            )
        entries = {}
        for line_no, row in enumerate(reader, start=2):
            instance = row["instance"]
            source = row[value_field]
            source_path = pathlib.PurePosixPath(source)
            expected_suffix = ".ltl" if value_field == "source" else ".tlsf"
            if (
                pathlib.PurePosixPath(instance).name != instance
                or not instance.endswith(".ltl")
            ):
                raise SystemExit(
                    f"GATE FAIL: {path}:{line_no}: invalid instance {instance!r}"
                )
            if (
                source_path.is_absolute()
                or ".." in source_path.parts
                or not source.endswith(expected_suffix)
            ):
                raise SystemExit(
                    f"GATE FAIL: {path}:{line_no}: invalid {value_field} "
                    f"{source!r}"
                )
            if instance in entries:
                raise SystemExit(
                    f"GATE FAIL: {path}:{line_no}: duplicate instance {instance!r}"
                )
            entries[instance] = source
    return entries


expected = {}
expected_sources = {}
source_maps = {}
test_sources = defaultdict(set)
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

        suite_dir = expected_path.parent / key[0]
        source_map_path = suite_dir / "sources.tsv"
        tlsf_source_map_path = suite_dir / "tlsf-sources.tsv"
        ltl_source = None
        tlsf_source = None
        if source_map_path.is_file():
            if source_map_path not in source_maps:
                source_maps[source_map_path] = load_map(source_map_path, "source")
            raw_ltl_source = source_maps[source_map_path].get(key[1])
            if raw_ltl_source is not None:
                ltl_source = (
                    repo_root / "tests" / "ltl" / raw_ltl_source
                ).resolve()
        if tlsf_source_map_path.is_file():
            if tlsf_source_map_path not in source_maps:
                source_maps[tlsf_source_map_path] = load_map(
                    tlsf_source_map_path, "tlsf"
                )
            raw_tlsf_source = source_maps[tlsf_source_map_path].get(key[1])
            if raw_tlsf_source is not None:
                if tlsf_corpus is None:
                    raise SystemExit(benchlib.tlsf_failure(
                        key,
                        benchlib.tlsf_corpus_diagnosis(build_dir=build_dir),
                    ))
                tlsf_source = (tlsf_corpus / raw_tlsf_source).resolve()
                if not tlsf_source.is_file():
                    raise SystemExit(benchlib.tlsf_failure(key, f"{tlsf_source} is absent"))

        # regress-expected.tsv froze baseline_seconds on the vendored LTL basis,
        # so landing-bar remeasures there even though Meson now uses -T.  The
        # syntcomp25 panel bases matched across four arms: 111 solved, 0
        # differing instances.
        if ltl_source is not None:
            if not ltl_source.is_file():
                raise SystemExit(
                    f"GATE FAIL: cannot locate expected source for "
                    f"{key[0]}/{key[1]}: {ltl_source} is absent"
                )
            expected_sources[key] = ltl_source
        elif tlsf_source is not None:
            expected_sources[key] = tlsf_source
        elif source_map_path.is_file() or tlsf_source_map_path.is_file():
            raise SystemExit(
                f"GATE FAIL: {suite_dir}: no source for {key[1]!r}"
            )
        else:
            expected_sources[key] = (
                repo_root / "tests" / "ltl" / key[0] / key[1]
            ).resolve()

        test_sources[key].add(expected_sources[key])
        if tlsf_source is not None:
            # TLSF-driven Meson suites use -T even when landing-bar can retain
            # the existing vendored LTL as its remeasurement basis.
            test_sources[key].add(tlsf_source)

if tlsf_corpus is not None:
    tlsf_corpus_out.write_text(str(tlsf_corpus), encoding="utf-8")

source_to_expected = defaultdict(list)
tlsf_name_to_expected = defaultdict(list)
for key, sources in test_sources.items():
    for source in sources:
        source_to_expected[source].append(key)
        if source.suffix == ".tlsf":
            tlsf_name_to_expected[source.name].append(key)

observed = {}
problems = []
with testlog_path.open() as handle:
    for line_no, line in enumerate(handle, start=1):
        row = json.loads(line)
        command = row.get("command") or []
        input_flags = [flag for flag in ("-F", "-T") if flag in command]
        if not input_flags:
            problems.append(
                f"testlog line {line_no}: command has no -F or -T input"
            )
            continue
        if len(input_flags) != 1:
            problems.append(
                f"testlog line {line_no}: command has both -F and -T inputs"
            )
            continue
        input_flag = input_flags[0]
        input_index = command.index(input_flag)
        if input_index + 1 >= len(command):
            problems.append(
                f"testlog line {line_no}: command has no value after {input_flag}"
            )
            continue
        instance_path = pathlib.Path(command[input_index + 1]).resolve()
        candidates = source_to_expected.get(instance_path, [])
        if not candidates and input_flag == "-T":
            # Meson recorded the configure-time corpus directory, which need
            # not be the one the run actually used -- check-real-correct.sh
            # relocates a missing -T file under a live ACACIA_TLSF_CORPUS.  The
            # corpus is flat, so the file name identifies the entry.
            candidates = tlsf_name_to_expected.get(instance_path.name, [])
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

landing_tlsf_args=()
if [[ -s "$scratch/tlsf-corpus-dir" ]]; then
  tlsf_corpus_dir=$(<"$scratch/tlsf-corpus-dir")
  landing_tlsf_args=(
    --tlsf-source-map
    "syntcomp25=$repo_root/tests/suites/benchmarks/syntcomp25/tlsf-sources.tsv"
    --tlsf-corpus "$tlsf_corpus_dir"
  )
fi

python3 "$repo_root/benchmarking/landing-bar.py" \
  "$scratch/baseline.csv" "$scratch/candidate.csv" \
  --timeout 17 \
  --baseline-bin "$baseline_bin" \
  --candidate-bin "$build_dir/src/acacia-bonsai" \
  --source-map "syntcomp24=$repo_root/tests/suites/benchmarks/syntcomp24/sources.tsv" \
  --source-map "syntcomp25=$repo_root/tests/suites/benchmarks/syntcomp25/sources.tsv" \
  "${landing_tlsf_args[@]}" \
  --memory-max 8G \
  --memory-swap-max 0
