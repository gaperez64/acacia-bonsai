#!/usr/bin/env bash

set -uo pipefail

solver=$1
default_children=$2
assert_child_metadata=$3
default_backend=$4
formula='G(i -> o)'
failures=0

run_case() {
  local label=$1
  local expected_status=$2
  local expected_children=$3
  local expected_prefix=$4
  shift 4

  local output status
  output=$("$solver" -f "$formula" -i i -o o -v "$@" 2>&1)
  status=$?
  if (( status != expected_status )); then
    printf '%s: expected status %d, got %d\n%s\n' \
      "$label" "$expected_status" "$status" "$output" >&2
    failures=$((failures + 1))
  fi
  if [[ $assert_child_metadata == true ]]; then
    if ! grep -Fq "Starting $expected_children solver children" <<<"$output"; then
      printf '%s: expected %d children\n%s\n' \
        "$label" "$expected_children" "$output" >&2
      failures=$((failures + 1))
    fi
    if [[ -n $expected_prefix ]] && ! grep -Fq "$expected_prefix" <<<"$output"; then
      printf '%s: expected child prefix %s\n%s\n' \
        "$label" "$expected_prefix" "$output" >&2
      failures=$((failures + 1))
    fi
  fi
}

run_error() {
  local label=$1
  local expected_text=$2
  shift 2

  local output status
  output=$("$solver" -f "$formula" -i i -o o "$@" 2>&1)
  status=$?
  if (( status != 3 )) || ! grep -Fq "$expected_text" <<<"$output"; then
    printf '%s: expected a loud CLI error containing %s\n%s\n' \
      "$label" "$expected_text" "$output" >&2
    failures=$((failures + 1))
  fi
}

run_case default 0 "$default_children" ''
run_case race_both 0 4 '' -r small,any -u both
run_case race_real_only 0 2 '' -r small,any
run_case any_real_only 0 1 "[real=any,backend=$default_backend]" -r any
run_case automaton_unreal_only 2 1 \
  "[unreal=automaton,pref=small,backend=$default_backend]" -u automaton
run_case formula_unreal_only 2 1 \
  "[unreal=formula,pref=small,backend=$default_backend]" -u formula

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
run_case synthesis_truncates_race 0 1 '[real=small,backend=backward]' \
  -r small,any -s "$tmpdir/out.aag"
if [[ ! -s $tmpdir/out.aag ]]; then
  printf 'synthesis_truncates_race: no AIGER output was written\n' >&2
  failures=$((failures + 1))
fi

run_error deterministic_is_not_runtime_strategy 'unexpected realizability strategy' -r deterministic
run_error trailing_comma_is_not_a_strategy 'empty strategy in -r list' -r small,
run_error embedded_whitespace_is_not_ignored 'unexpected realizability strategy' -r 's mall'
run_error legacy_bare_real_flag 'option requires an argument' -r
run_error removed_unreal_only_flag 'invalid option' -U

exit "$failures"
