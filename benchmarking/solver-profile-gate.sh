#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<EOF
usage:
  $0 [OPTIONS] BASELINE-BIN CANDIDATE-BIN
  $0 --calibrate [OPTIONS] BASELINE-BIN

Profile the fixed ten-instance solver panel with perf under strict per-run
8 GiB/no-swap cgroups.  Normal comparisons use three repetitions per binary;
--calibrate uses five repetitions of the unmodified baseline and reports the
observed cycle spread without applying a performance threshold.

Options:
  --calibrate             measure the baseline noise floor
  --output DIR            output directory (default: timestamped directory in /tmp)
  --targets FILE          target TSV (default: frozen solver-profile.tsv)
  --timeout SECONDS       per-repetition wall budget (default: 60)
  --repetitions N         override 3 comparison / 5 calibration repetitions
  --min-improvement PCT   aggregate cycle improvement required (default: 5)
  --max-regression PCT    per-target cycle regression ceiling (default: 6)
EOF
  exit 2
}

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
targets="$repo_root/tests/suites/benchmarks/solver-profile.tsv"
timeout_seconds=60
calibrate=0
repetitions=""
output=""
min_improvement=${SOLVER_PROFILE_MIN_IMPROVEMENT_PERCENT:-5.0}
max_regression=${SOLVER_PROFILE_MAX_REGRESSION_PERCENT:-6.0}

while (($#)); do
  case $1 in
    --calibrate)
      calibrate=1
      shift
      ;;
    --output)
      [[ $# -ge 2 ]] || usage
      output=$2
      shift 2
      ;;
    --targets)
      [[ $# -ge 2 ]] || usage
      targets=$2
      shift 2
      ;;
    --timeout)
      [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*([.][0-9]+)?$ ]] || usage
      timeout_seconds=$2
      shift 2
      ;;
    --repetitions)
      [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] || usage
      repetitions=$2
      shift 2
      ;;
    --min-improvement)
      [[ $# -ge 2 && $2 =~ ^[0-9]+([.][0-9]+)?$ ]] || usage
      min_improvement=$2
      shift 2
      ;;
    --max-regression)
      [[ $# -ge 2 && $2 =~ ^[0-9]+([.][0-9]+)?$ ]] || usage
      max_regression=$2
      shift 2
      ;;
    -h|--help) usage ;;
    --) shift; break ;;
    -*) usage ;;
    *) break ;;
  esac
done

if ((calibrate == 1)); then
  [[ $# == 1 ]] || usage
  baseline_bin=$(realpath "$1")
  candidate_bin=""
  repetitions=${repetitions:-5}
else
  [[ $# == 2 ]] || usage
  baseline_bin=$(realpath "$1")
  candidate_bin=$(realpath "$2")
  repetitions=${repetitions:-3}
fi

[[ -x $baseline_bin ]] || { echo "GATE FAIL: baseline is not executable: $baseline_bin"; exit 1; }
if ((calibrate == 0)); then
  [[ -x $candidate_bin ]] || {
    echo "GATE FAIL: candidate is not executable: $candidate_bin"
    exit 1
  }
fi
[[ -r $targets ]] || { echo "GATE FAIL: target file is not readable: $targets"; exit 1; }
command -v perf >/dev/null || { echo "GATE FAIL: perf is not installed"; exit 1; }
command -v systemd-run >/dev/null || { echo "GATE FAIL: systemd-run is not installed"; exit 1; }
command -v timeout >/dev/null || { echo "GATE FAIL: timeout is not installed"; exit 1; }

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output=${output:-/tmp/acacia-solver-profile.$timestamp}
mkdir -p "$output"
output=$(realpath "$output")
samples="$output/samples.tsv"
summary="$output/summary.tsv"
calibration_summary="$output/calibration.tsv"
printf 'label\tsuite\tinstance\tfixpoint_bucket\trepetition\tcycles\tinstructions\tllc_load_misses\tbranch_misses\texit\ttimed_out\tperf_file\n' >"$samples"

part_value() {
  local part=$1 section=$2
  awk -v section="$section" '
    $1 == section {
      $1=""
      sub(/^[[:space:]]+/, "")
      gsub(/[[:space:]]+/, ",")
      print
      exit
    }
  ' "$part"
}

counter_value() {
  local file=$1 event=$2
  awk -F '\t' -v event="$event" '
    $3 ~ ("^" event "(:u)?$") {
      value=$1
      gsub(/[ ,]/, "", value)
      if (value ~ /^[0-9]+$/) { print value; exit }
    }
  ' "$file"
}

case_counter=0
run_case() {
  local label=$1 binary=$2 suite=$3 instance=$4 bucket=$5 repetition=$6
  local ltl="$repo_root/tests/ltl/$suite/$instance"
  local part="${ltl%.ltl}.part"
  if [[ ! -r $ltl || ! -r $part ]]; then
    echo "GATE FAIL: missing $ltl or $part"
    exit 1
  fi

  local ins outs
  ins=$(part_value "$part" .inputs)
  outs=$(part_value "$part" .outputs)
  if [[ -z $ins || -z $outs ]]; then
    echo "GATE FAIL: incomplete partition file: $part"
    exit 1
  fi

  case_counter=$((case_counter + 1))
  local stem="$output/$label-$suite-${instance%.ltl}-r$repetition"
  local perf_file="$stem.perf" stdout_file="$stem.stdout"
  local unit="acacia-g2s-$$-$case_counter"
  local exit_code
  if systemd-run --user --quiet --pipe --wait --collect "--unit=$unit" \
      --property=MemoryMax=8G --property=MemorySwapMax=0 --property=OOMPolicy=continue \
      --working-directory="$repo_root" \
      perf stat --no-big-num -x $'\t' -o "$perf_file" \
        -e cycles,instructions,LLC-load-misses,branch-misses -- \
      timeout --foreground --signal=TERM --kill-after=3s "${timeout_seconds}s" \
        env "ACACIA_DIAG_INSTANCE=$instance" "$binary" -F "$ltl" -i "$ins" -o "$outs" \
        >"$stdout_file" 2>&1; then
    exit_code=0
  else
    exit_code=$?
  fi

  case $exit_code in
    0|1|2|124) ;;
    *)
      echo "GATE FAIL: $label/$suite/$instance repetition $repetition exited $exit_code"
      sed -n '1,80p' "$stdout_file"
      exit 1
      ;;
  esac

  local cycles instructions llc branch timed_out=0
  cycles=$(counter_value "$perf_file" cycles)
  instructions=$(counter_value "$perf_file" instructions)
  llc=$(counter_value "$perf_file" LLC-load-misses)
  branch=$(counter_value "$perf_file" branch-misses)
  if [[ -z $cycles || -z $instructions || -z $llc || -z $branch ]]; then
    echo "GATE FAIL: incomplete counters for $label/$suite/$instance repetition $repetition"
    exit 1
  fi
  [[ $exit_code == 124 ]] && timed_out=1
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$label" "$suite" "$instance" "$bucket" "$repetition" "$cycles" "$instructions" \
    "$llc" "$branch" "$exit_code" "$timed_out" "$(basename "$perf_file")" >>"$samples"
  printf '%-9s %-11s %-43s r%d cycles=%s%s\n' \
    "$label" "$suite" "$instance" "$repetition" "$cycles" \
    "$([[ $timed_out == 1 ]] && printf ' TIMEOUT' || true)"
}

mapfile -t target_rows < <(awk -F '\t' 'NR > 1 && NF >= 3 { print $1 "\t" $2 "\t" $3 }' "$targets")
if [[ ${#target_rows[@]} != 10 ]]; then
  echo "GATE FAIL: expected exactly 10 frozen targets, found ${#target_rows[@]}"
  exit 1
fi

for row in "${target_rows[@]}"; do
  IFS=$'\t' read -r suite instance bucket <<<"$row"
  for ((repetition = 1; repetition <= repetitions; ++repetition)); do
    run_case baseline "$baseline_bin" "$suite" "$instance" "$bucket" "$repetition"
    if ((calibrate == 0)); then
      run_case candidate "$candidate_bin" "$suite" "$instance" "$bucket" "$repetition"
    fi
  done
done

set +e
python3 - "$samples" "$summary" "$calibration_summary" "$calibrate" \
  "$min_improvement" "$max_regression" <<'PY'
import csv
import statistics
import sys
from collections import defaultdict

samples_path, summary_path, calibration_path, calibration, min_improvement, max_regression = (
    sys.argv[1:]
)
calibration = calibration == "1"
min_improvement = float(min_improvement)
max_regression = float(max_regression)

with open(samples_path, newline="") as handle:
    rows = list(csv.DictReader(handle, delimiter="\t"))

groups = defaultdict(list)
for row in rows:
    key = (row["label"], row["suite"], row["instance"], row["fixpoint_bucket"])
    groups[key].append(int(row["cycles"]))

with open(summary_path, "w", newline="") as handle:
    fields = ["label", "suite", "instance", "fixpoint_bucket", "median_cycles",
              "min_cycles", "max_cycles", "spread_percent"]
    writer = csv.DictWriter(handle, fields, delimiter="\t")
    writer.writeheader()
    for key in sorted(groups):
        values = groups[key]
        minimum = min(values)
        maximum = max(values)
        writer.writerow(dict(zip(fields[:4], key)) | {
            "median_cycles": int(statistics.median(values)),
            "min_cycles": minimum,
            "max_cycles": maximum,
            "spread_percent": f"{100.0 * (maximum - minimum) / minimum:.2f}",
        })

if calibration:
    spreads = {
        (suite, instance): 100.0 * (max(values) - min(values)) / min(values)
        for (label, suite, instance, bucket), values in groups.items()
    }
    repetition_totals = defaultdict(int)
    for row in rows:
        repetition_totals[int(row["repetition"])] += int(row["cycles"])
    aggregate = list(repetition_totals.values())
    aggregate_spread = 100.0 * (max(aggregate) - min(aggregate)) / min(aggregate)
    worst_target, worst_spread = max(spreads.items(), key=lambda item: item[1])
    with open(calibration_path, "w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(["metric", "spread_percent", "threshold_percent"])
        writer.writerow(["aggregate_cycles", f"{aggregate_spread:.2f}",
                         f"{min_improvement:.2f}"])
        writer.writerow(["maximum_target_cycles", f"{worst_spread:.2f}",
                         f"{max_regression:.2f}"])
    print(f"maximum target spread: {worst_spread:.2f}% ({worst_target[0]}/{worst_target[1]})")
    print(f"aggregate cycle spread: {aggregate_spread:.2f}%")
    raise SystemExit(0)

medians = {
    (label, suite, instance): statistics.median(values)
    for (label, suite, instance, bucket), values in groups.items()
}
failures = []
baseline_total = 0
candidate_total = 0
targets = sorted({(suite, instance) for _, suite, instance, _ in groups})
for suite, instance in targets:
    baseline = medians[("baseline", suite, instance)]
    candidate = medians[("candidate", suite, instance)]
    baseline_total += baseline
    candidate_total += candidate
    improvement = 100.0 * (baseline - candidate) / baseline
    print(f"{suite}/{instance}: {improvement:+.2f}% cycles")
    if improvement < -max_regression:
        failures.append(
            f"{suite}/{instance} regressed {-improvement:.2f}% (> {max_regression:.2f}%)"
        )

aggregate_improvement = 100.0 * (baseline_total - candidate_total) / baseline_total
print(
    f"aggregate: baseline={baseline_total:.0f} candidate={candidate_total:.0f} "
    f"improvement={aggregate_improvement:.2f}%"
)
if aggregate_improvement < min_improvement:
    failures.append(
        f"aggregate improved only {aggregate_improvement:.2f}% (< {min_improvement:.2f}%)"
    )
if failures:
    for failure in failures:
        print(f"- {failure}")
    raise SystemExit(1)
PY
compare_status=$?
set -e

echo "samples: $samples"
echo "summary: $summary"
if ((calibrate == 1)); then
  echo "calibration: $calibration_summary"
fi
if ((compare_status != 0)); then
  echo "GATE FAIL: solver profile thresholds not met"
  exit 1
fi
echo "GATE PASS"
