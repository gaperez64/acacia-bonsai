#!/usr/bin/env bash

set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT" || exit 2

if [[ -d /usr/local/lib/pkgconfig ]]; then
  case ":${PKG_CONFIG_PATH:-}:" in
    *:/usr/local/lib/pkgconfig:*) ;;
    *) export PKG_CONFIG_PATH="${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}/usr/local/lib/pkgconfig" ;;
  esac
fi

SESSION_NAME=${SESSION_NAME:-overnight-$(date +%Y%m%d-%H%M%S)}
SUITE=${SUITE:-ab/syntcomp24/0s-1s}
TIMEOUT_FACTOR=${TIMEOUT_FACTOR:-1.7}
MEMORY_MAX=${MEMORY_MAX:-8G}
SWAP_MAX=${SWAP_MAX:-0}
SLICES=${SLICES:-4}
TOOL_SLICES=${TOOL_SLICES:-$SLICES}

ACACIA_CONFIG_GROUP=${ACACIA_CONFIG_GROUP:-local_tuning_default}
if [[ -z ${ACACIA_CONFIGS+x} ]]; then
  mapfile -t _ACACIA_CONFIG_GROUP_ROWS < <(python3 scripts/acacia-config.py list-group "$ACACIA_CONFIG_GROUP")
  ACACIA_CONFIGS="${_ACACIA_CONFIG_GROUP_ROWS[*]}"
  unset _ACACIA_CONFIG_GROUP_ROWS
fi
TOOL_CONFIGS=${TOOL_CONFIGS-ltlsynt_no_decompose ltlsynt_no_bypass ltlsynt_no_obligation ltlsynt_no_specials}

export BENCHMARK_CGROUP=${BENCHMARK_CGROUP:-strict}
export BENCHMARK_CGROUP_SCOPE=${BENCHMARK_CGROUP_SCOPE:-solver}
export BENCHMARK_CGROUP_MEMORY_MAX=${BENCHMARK_CGROUP_MEMORY_MAX:-$MEMORY_MAX}
export BENCHMARK_CGROUP_SWAP_MAX=${BENCHMARK_CGROUP_SWAP_MAX:-$SWAP_MAX}
export BENCHMARK_TEST_JOBS=${BENCHMARK_TEST_JOBS:-1}
export BENCHMARK_COMPILE_JOBS=${BENCHMARK_COMPILE_JOBS:-1}

mkdir -p _bm-logs
LOG="_bm-logs/${SESSION_NAME}.log"
SUMMARY="_bm-logs/${SESSION_NAME}.summary"

log() {
  printf '[%s] %s\n' "$(date --iso-8601=seconds)" "$*" | tee -a "$LOG"
}

run_step() {
  log "START $*"
  "$@" >>"$LOG" 2>&1
  local status=$?
  if (( status == 0 )); then
    log "DONE $*"
  else
    log "FAILED($status) $*"
  fi
  return "$status"
}

archive_untracked_build() {
  local config=$1
  local build="build_${config}"
  local stamp="${SESSION_NAME}"
  local archived="${build}.pre-metadata.${stamp}"

  [[ -d "$build" ]] || return 0
  if [[ -e "$build/.acacia-config.json" ]] &&
     python3 scripts/acacia-config.py show "$config" | cmp -s - "$build/.acacia-config.json"; then
    return 0
  fi

  log "ARCHIVE $build -> $archived"
  mv "$build" "$archived"
}

run_acacia_config() {
  local config=$1
  local slice

  archive_untracked_build "$config"
  run_step ./self-benchmark.sh -R -c "$config" -b "$SUITE" -t "$TIMEOUT_FACTOR" || return 0

  for (( slice = 1; slice <= SLICES; slice++ )); do
    run_step ./self-benchmark.sh -B -C -c "$config" -b "$SUITE" -t "$TIMEOUT_FACTOR" -s "${slice}/${SLICES}" || true
  done
}

run_tool_config() {
  local config=$1
  local slice

  for (( slice = 1; slice <= TOOL_SLICES; slice++ )); do
    run_step ./self-benchmark.sh -B -C -c "$config" -b "$SUITE" -t "$TIMEOUT_FACTOR" -s "${slice}/${TOOL_SLICES}" || true
  done
}

summarize_json() {
  python3 - "$@" >>"$SUMMARY" <<'PY'
import json
import pathlib
import sys

for raw in sys.argv[1:]:
    path = pathlib.Path(raw)
    if not path.exists():
        continue
    rows = []
    with path.open() as f:
        first = f.read(1)
        f.seek(0)
        if first == "[":
            rows = json.load(f)
        else:
            rows = [json.loads(line) for line in f if line.strip()]
    timeouts = sum(1 for row in rows if row.get("result") == "TIMEOUT")
    failures = sum(1 for row in rows if row.get("result") not in ("OK", "TIMEOUT", "SKIP"))
    duration = sum(float(row.get("duration", 0) or 0) for row in rows)
    print(f"{path.name}: rows={len(rows)} timeouts={timeouts} failures={failures} duration={duration:.2f}s")
PY
}

log "SESSION $SESSION_NAME"
log "suite=$SUITE timeout_factor=$TIMEOUT_FACTOR slices=$SLICES tool_slices=$TOOL_SLICES memory=$BENCHMARK_CGROUP_MEMORY_MAX swap=$BENCHMARK_CGROUP_SWAP_MAX"
log "acacia_config_group=$ACACIA_CONFIG_GROUP"
log "acacia_configs=$ACACIA_CONFIGS"
log "tool_configs=$TOOL_CONFIGS"

for config in $ACACIA_CONFIGS; do
  run_acacia_config "$config"
done

for config in $TOOL_CONFIGS; do
  run_tool_config "$config"
done

: >"$SUMMARY"
{
  echo "session=$SESSION_NAME"
  echo "suite=$SUITE"
  echo "timeout_factor=$TIMEOUT_FACTOR"
  echo "slices=$SLICES"
  echo "tool_slices=$TOOL_SLICES"
  echo "memory=$BENCHMARK_CGROUP_MEMORY_MAX"
  echo
} >>"$SUMMARY"

for config in $ACACIA_CONFIGS; do
  for (( slice = 1; slice <= SLICES; slice++ )); do
    summarize_json "_bm-logs/${config}-slice-${slice}-of-${SLICES}.json"
  done
done

for config in $TOOL_CONFIGS; do
  for (( slice = 1; slice <= TOOL_SLICES; slice++ )); do
    summarize_json "_bm-logs/${config}-slice-${slice}-of-${TOOL_SLICES}.json"
  done
done

log "SUMMARY $SUMMARY"
log "SESSION COMPLETE"
