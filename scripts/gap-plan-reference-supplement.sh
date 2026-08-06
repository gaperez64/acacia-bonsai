#!/usr/bin/env bash

# Complete the 2024 reference with corpus files omitted from all four
# historical runtime buckets.  This waits for the frozen-binary bucket sweep,
# then reconfigures only Meson's suite metadata and benchmarks the disjoint
# supplement with the same binary and resource policy.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

UPSTREAM_UNIT=${UPSTREAM_UNIT:-acacia-gap-syntcomp24-full-20260805.service}
SESSION_NAME=${SESSION_NAME:-gap-plan-syntcomp24-unbucketed-20260806}
MEMORY_MAX=${MEMORY_MAX:-8G}
SWAP_MAX=${SWAP_MAX:-0}

while systemctl --user is-active --quiet "$UPSTREAM_UNIT"; do
  sleep 60
done

if [[ $(systemctl --user show "$UPSTREAM_UNIT" -p Result --value) != success ]]; then
  printf 'upstream reference campaign failed: %s\n' "$UPSTREAM_UNIT" >&2
  exit 1
fi

meson setup --reconfigure build_best_decomp_mona

rm -f \
  build_best_decomp_mona/benchmarked-slice-1-of-1 \
  build_best_decomp_mona/benchmarked-ltlsynt-slice-1-of-1 \
  _bm-logs/best_decomp_mona-slice-1-of-1.json \
  _bm-logs/best_decomp_mona-slice-1-of-1.log \
  _bm-logs/best_decomp_mona-slice-1-of-1.meta \
  _bm-logs/ltlsynt-slice-1-of-1.json \
  _bm-logs/ltlsynt-slice-1-of-1.log \
  _bm-logs/ltlsynt-slice-1-of-1.meta

export BENCHMARK_TOOL_HOST_BUILD=build_best_decomp_mona
export BENCHMARK_CGROUP=strict
export BENCHMARK_CGROUP_SCOPE=solver
export BENCHMARK_CGROUP_MEMORY_MAX=$MEMORY_MAX
export BENCHMARK_CGROUP_SWAP_MAX=$SWAP_MAX
export BENCHMARK_TEST_JOBS=1
export BENCHMARK_COMPILE_JOBS=1

env \
  SESSION_NAME="$SESSION_NAME" \
  SUITE=ab/syntcomp24/reference-unbucketed \
  TIMEOUT_FACTOR=1.7 \
  MEMORY_MAX="$MEMORY_MAX" \
  SWAP_MAX="$SWAP_MAX" \
  SLICES=1 \
  TOOL_SLICES=1 \
  ACACIA_CONFIGS=best_decomp_mona \
  TOOL_CONFIGS=ltlsynt \
  scripts/overnight-benchmark-session.sh
