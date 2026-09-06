#!/usr/bin/env bash
set -euo pipefail

binary=$1
forward_enabled=$2

run() {
    local wanted_status=$1
    shift

    local output
    local status
    set +e
    output=$("$binary" "$@" 2>&1)
    status=$?
    set -e
    if [[ $status -ne $wanted_status ]]; then
        printf 'expected status=%s, got status=%s output=%q\n' \
            "$wanted_status" "$status" "$output" >&2
        return 1
    fi
    printf '%s' "$output"
}

output=$(run 0 -f 'G(o)' -i i -o o -r small)
grep -qx REALIZABLE <<<"$output"

output=$(run 3 -f 'G(o)' -i i -o o --real-backend bogus)
[[ $output == *backward* && $output == *forward* ]]

common=(-f 'G(o)' -i i -o o)
backward_arms='real:any:backward,unreal:formula:backward'

output=$(run 0 "${common[@]}" --arms "$backward_arms" -v)
[[ $output == *'Starting 2 solver children'* ]]

output=$(run 3 "${common[@]}" \
    --arms real:any:backward,real:any:backward)
[[ $output == *'duplicate arm'* && $output == *'real:any:backward'* ]]

output=$(run 3 --arms=)
[[ $output == *'at least one arm'* ]]

check_conflict() {
    local output
    output=$(run 3 "${common[@]}" "$@")
    [[ $output == *'mutually exclusive'* && $output == *'--arms'* ]]
}

check_conflict --arms real:small:backward -r small
check_conflict -r small --arms real:small:backward
check_conflict --arms unreal:formula:backward -u formula
check_conflict -u formula --arms unreal:formula:backward
check_conflict --arms real:small:backward --real-backend backward
check_conflict --real-backend backward --arms real:small:backward
check_conflict --arms unreal:formula:backward --unreal-backend backward
check_conflict --unreal-backend backward --arms unreal:formula:backward
check_conflict --arms unreal:formula:backward --unreal-translation-pref small
check_conflict --unreal-translation-pref small --arms unreal:formula:backward

output=$(run 3 "${common[@]}" --arms real:small:sideways)
[[ $output == *'invalid backend sideways'* && $output == *'backward or forward'* ]]

output=$(run 3 "${common[@]}" --arms sideways:small:forward)
[[ $output == *'invalid polarity sideways'* && $output == *'real or unreal'* ]]

output=$(run 3 "${common[@]}" --arms real:formula:forward)
[[ $output == *'invalid transform formula'* && $output == *'small or any'* ]]

if [[ $forward_enabled == true ]]; then
    output=$(run 0 "${common[@]}" -r small -u formula -v \
        --real-backend backward --unreal-backend forward)
    [[ $output == *'[real=small,backend=backward] '* ]]
    [[ $output == *'[unreal=formula,pref=small,backend=forward] '* ]]

    four_arms='real:any:backward,real:small:forward,unreal:formula:forward,unreal:automaton:forward'
    output=$(run 0 "${common[@]}" --arms "$four_arms" -v)
    [[ $output == *'Starting 4 solver children'* ]]

    output=$(run 0 "${common[@]}" \
        --arms real:any:backward,real:any:forward)
    grep -qx REALIZABLE <<<"$output"

    synthesis_output=$(mktemp)
    trap 'rm -f -- "$synthesis_output"' EXIT
    output=$(run 0 "${common[@]}" -v -s "$synthesis_output" \
        --arms real:small:forward,real:any:backward,unreal:formula:backward)
    [[ $output == *'Starting 2 solver children'* ]]
    [[ $output == *'Forcing the real backend to backward for synthesis'* ]]
    [[ $output == *'[real=small,backend=backward] '* ]]
    [[ $output != *'[real=any,'* ]]
else
    output=$(run 3 --real-backend forward)
    [[ $output == *'built without the forward safety solver'* ]]
    [[ $output == *'acacia_forward_safety_solver=true'* ]]

    output=$(run 3 --unreal-backend forward)
    [[ $output == *'built without the forward safety solver'* ]]
    [[ $output == *'acacia_forward_safety_solver=true'* ]]

    output=$(run 3 --arms real:small:forward)
    [[ $output == *'built without the forward safety solver'* ]]
    [[ $output == *'acacia_forward_safety_solver=true'* ]]
fi
