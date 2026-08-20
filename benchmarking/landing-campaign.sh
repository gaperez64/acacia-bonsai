#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
baseline_bin=
candidate_bin=
timeout=17
output=
declare -a suites=()
declare -a lists=()

usage () {
  cat <<'EOF'
usage: benchmarking/landing-campaign.sh \
  --baseline-bin PATH --candidate-bin PATH \
  --suite NAME --list PATH [--suite NAME --list PATH ...] \
  --timeout SECONDS --output DIR
EOF
}

while (( $# )); do
  case "$1" in
    --baseline-bin) baseline_bin=${2:?}; shift 2 ;;
    --candidate-bin) candidate_bin=${2:?}; shift 2 ;;
    --suite) suites+=("${2:?}"); shift 2 ;;
    --list) lists+=("${2:?}"); shift 2 ;;
    --timeout) timeout=${2:?}; shift 2 ;;
    --output) output=${2:?}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z $baseline_bin || -z $candidate_bin || -z $output || ${#suites[@]} -eq 0 ||
      ${#suites[@]} -ne ${#lists[@]} ]]; then
  usage >&2
  exit 2
fi
if [[ ! $timeout =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v value="$timeout" 'BEGIN { exit !(value > 0) }'; then
  printf '%s\n' '--timeout must be positive' >&2
  exit 2
fi

baseline_bin=$(realpath "$baseline_bin")
candidate_bin=$(realpath "$candidate_bin")
output=$(realpath -m "$output")
mkdir -p "$output"

# Put the complete campaign in one bounded scope.  Child tools see the marker
# and create process groups only, avoiding nested per-instance systemd scopes.
if [[ ${ACACIA_OUTER_CGROUP:-0} != 1 ]]; then
  scope_command=(
    systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0
    env ACACIA_OUTER_CGROUP=1 "$0"
    --baseline-bin "$baseline_bin" --candidate-bin "$candidate_bin"
  )
  for i in "${!suites[@]}"; do
    scope_command+=(--suite "${suites[$i]}" --list "${lists[$i]}")
  done
  scope_command+=(--timeout "$timeout" --output "$output")
  exec "${scope_command[@]}"
fi

gate_complete=0
printf 'RUNNING\n' > "$output/status.txt"

write_summary () {
  local verdict=$1
  {
    printf 'GATE %s\n' "$verdict"
    for report in "$output"/landing-*.txt; do
      [[ -e $report ]] || continue
      printf '\n== %s ==\n' "$(basename "$report")"
      cat "$report"
    done
  } > "$output/summary.txt"
}

on_exit () {
  local rc=$?
  if (( rc == 0 && gate_complete == 1 )); then
    printf 'COMPLETE PASS\n' > "$output/status.txt"
  else
    (( rc != 0 )) || rc=1
    write_summary FAIL
    printf 'COMPLETE FAIL exit=%d\n' "$rc" > "$output/status.txt"
  fi
}
trap on_exit EXIT

git_value () {
  local path=$1 spec=$2
  git -C "$(dirname "$path")" rev-parse "$spec" 2>/dev/null || printf 'unknown\n'
}

source_root () {
  git -C "$(dirname "$1")" rev-parse --show-toplevel 2>/dev/null || true
}

build_options () {
  local binary=$1 root build
  root=$(source_root "$binary")
  build=$(dirname "$(dirname "$binary")")
  if [[ -n $root && -d $build/meson-info ]]; then
    meson configure "$build" 2>/dev/null || printf 'unavailable\n'
  else
    printf 'unavailable\n'
  fi
}

meta_tmp="$output/meta.txt.tmp"
{
  printf 'baseline_binary=%s\n' "$baseline_bin"
  printf 'baseline_sha256=%s\n' "$(sha256sum "$baseline_bin" | awk '{print $1}')"
  printf 'baseline_source_revision=%s\n' "$(git_value "$baseline_bin" HEAD)"
  printf 'baseline_posets_revision=%s\n' "$(git_value "$baseline_bin" HEAD:subprojects/posets)"
  printf 'baseline_tlsf_tools_revision=%s\n' "$(git_value "$baseline_bin" HEAD:subprojects/tlsf-tools)"
  printf 'candidate_binary=%s\n' "$candidate_bin"
  printf 'candidate_sha256=%s\n' "$(sha256sum "$candidate_bin" | awk '{print $1}')"
  printf 'candidate_source_revision=%s\n' "$(git_value "$candidate_bin" HEAD)"
  printf 'candidate_posets_revision=%s\n' "$(git_value "$candidate_bin" HEAD:subprojects/posets)"
  printf 'candidate_tlsf_tools_revision=%s\n' "$(git_value "$candidate_bin" HEAD:subprojects/tlsf-tools)"
  printf '\n[baseline meson options]\n'
  build_options "$baseline_bin"
  printf '\n[candidate meson options]\n'
  build_options "$candidate_bin"
  printf '\n[spot metadata]\n'
  python3 "$repo_root/scripts/spot-metadata.py"
} > "$meta_tmp"
mv "$meta_tmp" "$output/meta.txt"

expected_rows () {
  awk 'NF && $1 !~ /^#/ { n++ } END { print n + 0 }' "$1"
}

csv_complete () {
  local csv=$1 expected=$2
  [[ -f $csv ]] || return 1
  python3 - "$csv" "$expected" <<'PY'
import csv
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
expected = int(sys.argv[2])
try:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
    good = reader.fieldnames is not None and "instance" in reader.fieldnames
except (OSError, csv.Error):
    good = False
raise SystemExit(0 if good and len(rows) == expected else 1)
PY
}

run_side () {
  local binary=$1 suite=$2 list=$3 csv=$4
  local tmp="$csv.tmp"
  local source_map
  source_map="$(dirname "$list")/sources.tsv"
  rm -f "$tmp"
  local -a source_args
  if [[ -f $source_map ]]; then
    source_args=(--source-map "$source_map")
  else
    source_args=(--instances-dir "$repo_root/tests/ltl/$suite")
  fi
  python3 "$repo_root/benchmarking/run-subset.py" \
    --bin "$binary" "${source_args[@]}" \
    --list "$list" --timeout "$timeout" --csv "$tmp"
  mv "$tmp" "$csv"
}

for i in "${!suites[@]}"; do
  suite=${suites[$i]}
  list=$(realpath "${lists[$i]}")
  expected=$(expected_rows "$list")
  baseline_csv="$output/baseline-$suite.csv"
  candidate_csv="$output/candidate-$suite.csv"
  report="$output/landing-$suite.txt"

  if ! csv_complete "$baseline_csv" "$expected"; then
    run_side "$baseline_bin" "$suite" "$list" "$baseline_csv"
  fi
  if ! csv_complete "$candidate_csv" "$expected"; then
    run_side "$candidate_bin" "$suite" "$list" "$candidate_csv"
  fi

  report_tmp="$report.tmp"
  source_map="$(dirname "$list")/sources.tsv"
  if [[ -f $source_map ]]; then
    remeasure_source=$source_map
  else
    remeasure_source="$repo_root/tests/ltl/$suite"
  fi
  set +e
  python3 "$repo_root/benchmarking/landing-bar.py" \
    "$baseline_csv" "$candidate_csv" \
    --timeout "$timeout" \
    --baseline-bin "$baseline_bin" \
    --candidate-bin "$candidate_bin" \
    --instances-dir "$remeasure_source" \
    > "$report_tmp" 2>&1
  rc=$?
  set -e
  mv "$report_tmp" "$report"
  if (( rc != 0 )); then
    exit "$rc"
  fi
done

write_summary PASS
gate_complete=1
