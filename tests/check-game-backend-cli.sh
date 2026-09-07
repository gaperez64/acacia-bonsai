#!/usr/bin/env bash
set -euo pipefail

binary=$1
forward_enabled=$2
guarded_enabled=$3
lazy_enabled=$4

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

output=$(run 3 -f 'G(o)' -i i -o o --real-provider spot-guarded)
[[ $output == *'expected frozen-graph, spot-lazy or spot-eager'* ]]
output=$(run 3 -f 'G(o)' -i i -o o --candidate-mode bogus)
[[ $output == *'only or fallback'* ]]
output=$(run 0 -f 'G(o)' -i i -o o -r small --real-backend backward \
    --real-provider frozen-graph -v)
[[ $output == *'backend=backward'* && $output == *'provider=frozen-graph'* ]]

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
[[ $output == *'invalid backend sideways'* && $output == *'backward, forward, spot-guarded or spot-guarded-sparse'* ]]

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

if [[ $guarded_enabled == true ]]; then
    output=$(run 0 -f 'G(i <-> X(o))' -i i -o o --spot-fast off -v \
        --arms real:small:spot-guarded)
    [[ $output == *'[real=small,backend=spot-guarded] '* ]]
    [[ $output == *'spot-guarded K='* ]]
    grep -qx REALIZABLE <<<"$output"
    output=$(run 1 -f 'G(i)' -i i -o o -v --arms unreal:formula:spot-guarded)
    [[ $output == *'backend=spot-guarded'* ]]
    grep -qx UNREALIZABLE <<<"$output"

    # An actual synthesis request must use backward, with an emitted circuit.
    synthesis_output=$(mktemp)
    trap 'rm -f -- "$synthesis_output"' EXIT
    output=$(run 0 -f 'G(i <-> X(o))' -i i -o o --spot-fast off -v \
        -s "$synthesis_output" --arms real:small:spot-guarded)
    [[ $output == *'Forcing the real backend to backward for synthesis'* ]]
    [[ $output == *'backend=backward'* && $output != *'spot-guarded K='* ]]
    [[ -s $synthesis_output ]]

    for mode in only fallback; do
        status=2
        [[ $mode == fallback ]] && status=0
        output=$(ACACIA_SPOT_MAX_EXPANSIONS=0 run "$status" \
            -f 'G(i <-> X(o))' -i i -o o --spot-fast off -v \
            --arms real:small:spot-guarded --candidate-mode "$mode")
        [[ $output == *'spot-guarded K='* ]]
        if [[ $mode == only ]]; then
            grep -qx UNKNOWN <<<"$output"
            [[ $output != *'fallback provider='* ]]
        else
            grep -qx REALIZABLE <<<"$output"
            [[ $output == *'fallback provider=frozen-graph backend=backward'* ]]
        fi
    done
else
    for selection in '--real-backend spot-guarded' '--unreal-backend spot-guarded' \
                     '--arms real:small:spot-guarded' '--arms unreal:formula:spot-guarded'; do
        read -r option value <<<"$selection"
        output=$(run 3 "$option" "$value")
        [[ $output == *'built without the Spot guarded backend'* ]]
        [[ $output == *'acacia_spot_guarded_backend=true'* ]]
    done
fi

if [[ $lazy_enabled == true ]]; then
    output=$(run 3 "${common[@]}" --unreal-provider spot-lazy -u automaton)
    [[ $output == *'unsupported configuration'* && $output == *'automaton-unreal route must remain eager'* ]]
    output=$(run 3 "${common[@]}" --real-provider spot-lazy -r small --real-backend backward)
    [[ $output == *'unsupported configuration'* && $output == *'requires --real-backend spot-guarded'* ]]
    if [[ $guarded_enabled == true ]]; then
        lazy=(-f 'G(i <-> X(o))' -i i -o o --spot-fast off -v \
              --arms real:small:spot-guarded --real-provider spot-lazy)
        output=$(run 0 "${lazy[@]}")
        [[ $output == *'provider=spot-lazy'* && $output == *'backend=spot-guarded'* ]]
        [[ $output == *'spot-lazy worker_formula='* && $output == *'status=WIN_K'* ]]
        [[ $output == *'wrapper_rows_generated=0'* && $output == *'verification_ms='* ]]
        # The live worker's captured boundary and lazy factory must see the
        # same formula, including existing simplification/decomposition.
        for formula in 'G(i <-> X(o))' 'G(i <-> X(o)) & G(j <-> X(p))'; do
            eager_output=$(run 0 -f "$formula" -i i,j -o o,p --spot-fast off -v \
                --arms real:small:spot-guarded)
            lazy_output=$(run 0 -f "$formula" -i i,j -o o,p --spot-fast off -v \
                --arms real:small:spot-guarded --real-provider spot-lazy)
            eager_formula=$(sed -n 's/.*Captured worker_formula=//p' <<<"$eager_output")
            lazy_formula=$(sed -n 's/.*spot-lazy worker_formula=//p' <<<"$lazy_output")
            [[ -n $eager_formula && $eager_formula == "$lazy_formula" ]]
        done
        for mode in only fallback; do
            status=2
            [[ $mode == fallback ]] && status=0
            output=$(ACACIA_SPOT_MAX_EXPANSIONS=0 run "$status" "${lazy[@]}" \
                --candidate-mode "$mode")
            if [[ $mode == only ]]; then
                grep -qx UNKNOWN <<<"$output"
                [[ $output != *'fallback provider='* ]]
            else
                grep -qx REALIZABLE <<<"$output"
                [[ $output == *'fallback provider=frozen-graph backend=backward'* ]]
                [[ $output == *'lazy_materialization=none'* && $output == *'fallback rebuild_ms='* ]]
            fi
        done
        output=$(run 0 "${lazy[@]}" -s "$synthesis_output")
        [[ $output == *'backend=backward'* && $output == *'provider=frozen-graph'* ]]
        [[ $output != *'spot-lazy worker_formula='* && $output != *'spot-guarded K='* ]]
        [[ -s $synthesis_output ]]
    fi
else
    for option in --real-provider --unreal-provider; do
        output=$(run 3 "$option" spot-lazy)
        [[ $output == *'unsupported configuration'* && $output == *'built without the Spot lazy provider'* ]]
        [[ $output == *'acacia_spot_lazy_provider=true'* ]]
    done
fi

if [[ $guarded_enabled == true ]]; then
    output=$(run 0 -f 'G(i <-> X(o))' -i i -o o --spot-fast off \
        --arms real:small:spot-guarded-sparse)
    grep -qx REALIZABLE <<<"$output"
    output=$(run 2 -f 'G(o <-> X(i))' -i i -o o --spot-fast off \
        --arms unreal:formula:spot-guarded-sparse)
    grep -qx UNKNOWN <<<"$output"
fi
if [[ $lazy_enabled == true && $guarded_enabled == true ]]; then
    for provider in spot-eager spot-lazy; do
        for formula in 'G(o <-> X(i))' 'GF(i) & GF(o)' 'G(o <-> X(i)) & G(p <-> X(j))'; do
            output=$(run 1 -f "$formula" -i i,j -o o,p --spot-fast off -v \
                --arms "unreal:formula:spot-guarded:$provider")
            grep -qx UNREALIZABLE <<<"$output"
            [[ $output == *"provider=$provider"* && $output == *'status=WIN_K'* ]]
        done
        output=$(run 2 -f 'G(i <-> X(o))' -i i -o o --spot-fast off -K 3 \
            --arms "unreal:formula:spot-guarded:$provider")
        grep -qx UNKNOWN <<<"$output"
        output=$(run 0 -f 'G(i <-> X(o))' -i i -o o --spot-fast off \
            --arms "real:small:backward,real:small:spot-guarded:$provider")
        grep -qx REALIZABLE <<<"$output"
    done
fi
