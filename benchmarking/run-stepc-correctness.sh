#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
candidate_build=${1:?usage: run-stepc-correctness.sh CANDIDATE-BUILD POSETS-BUILD [OUTPUT-DIR]}
posets_build=${2:?usage: run-stepc-correctness.sh CANDIDATE-BUILD POSETS-BUILD [OUTPUT-DIR]}
output=${3:-_bm-logs.stepc-correctness-20260814}

cd "$repo_root"
mkdir -p "$output"
printf 'RUNNING G0\n' > "$output/status.txt"
gate_complete=0
trap 'rc=$?; if (( rc == 0 && gate_complete == 1 )); then printf "COMPLETE PASS\n" > "$output/status.txt"; else (( rc != 0 )) || rc=1; printf "COMPLETE FAIL exit=%d\n" "$rc" > "$output/status.txt"; fi' EXIT

env ACACIA_TEST_CGROUP=0 MESON_TESTTHREADS=1 \
  meson test -C "$candidate_build" --no-rebuild --num-processes 1 \
    --suite unit --print-errorlogs --logbase stepc-g0 \
    > "$output/g0-acacia.txt" 2>&1
env MESON_TESTTHREADS=1 \
  meson test -C "$posets_build" --no-rebuild --num-processes 1 \
    --print-errorlogs --logbase stepc-g0-posets \
    > "$output/g0-posets.txt" 2>&1

printf 'RUNNING G4\n' > "$output/status.txt"
set +e
env ACACIA_TEST_CGROUP=0 ACACIA_TEST_RESOURCE_UNKNOWN=1 MESON_TESTTHREADS=1 \
  meson test -C "$candidate_build" --no-rebuild --num-processes 1 \
    --suite=ab/realizable --suite=ab/unrealizable \
    --print-errorlogs --logbase stepc-g4 \
    > "$output/g4.txt" 2>&1
g4_exit=$?
set -e

g4_ok=$(awk '$1 == "Ok:" {print $2}' "$output/g4.txt" | tail -1)
g4_fail=$(awk '$1 == "Fail:" {print $2}' "$output/g4.txt" | tail -1)
g4_timeout=$(awk '$1 == "Timeout:" {print $2}' "$output/g4.txt" | tail -1)
if [[ ! $g4_ok =~ ^[0-9]+$ || ! $g4_fail =~ ^[0-9]+$ ||
      ! $g4_timeout =~ ^[0-9]+$ ]]; then
  echo 'GATE FAIL: G4 did not emit a complete Meson summary' >&2
  exit 1
fi
if (( g4_fail != 0 || g4_ok + g4_timeout != 624 )); then
  echo "GATE FAIL: G4 summary ok=$g4_ok fail=$g4_fail timeout=$g4_timeout" >&2
  exit 1
fi
if rg -n 'FALSE POSITIVE|FALSE NEGATIVE|[[:space:]]INTERRUPT[[:space:]]' \
    "$output/g4.txt"; then
  echo 'GATE FAIL: G4 emitted a false verdict or interrupt marker' >&2
  exit 1
fi

{
  printf 'G0 Acacia: PASS\n'
  tail -12 "$output/g0-acacia.txt"
  printf '\nG0 Posets: PASS\n'
  tail -12 "$output/g0-posets.txt"
  printf '\nG4 labelled correctness: PASS\n'
  printf 'meson exit=%d (timeouts are performance non-answers)\n' "$g4_exit"
  tail -16 "$output/g4.txt"
} > "$output/summary.txt"
gate_complete=1
