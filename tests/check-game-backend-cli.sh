#!/usr/bin/env bash
set -euo pipefail

binary=$1
forward_enabled=$2
guarded_enabled=$3
lazy_enabled=$4
# Release/lowmem compiler profiles intentionally compile out verbose output.
verbose_enabled=${5:-true}
# Older frozen builds have five-argument test manifests. New builds also supply
# their configured defaults, so the same harness can check those end to end.
candidate_default=${6:-only}
taa_cap_default=${7:-}

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
[[ $verbose_enabled != true || ( $output == *'backend=backward'* && $output == *'provider=frozen-graph'* ) ]]

common=(-f 'G(o)' -i i -o o)
backward_arms='real:any:backward,unreal:formula:backward'

output=$(run 0 "${common[@]}" --arms "$backward_arms" -v)
[[ $verbose_enabled != true || ( $output == *'Starting 2 solver children'* ) ]]

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
    [[ $verbose_enabled != true || ( $output == *'[real=small,backend=backward] '* ) ]]
    [[ $verbose_enabled != true || ( $output == *'[unreal=formula,pref=small,backend=forward] '* ) ]]

    four_arms='real:any:backward,real:small:forward,unreal:formula:forward,unreal:automaton:forward'
    output=$(run 0 "${common[@]}" --arms "$four_arms" -v)
    [[ $verbose_enabled != true || ( $output == *'Starting 4 solver children'* ) ]]

    output=$(run 0 "${common[@]}" \
        --arms real:any:backward,real:any:forward)
    grep -qx REALIZABLE <<<"$output"

    synthesis_output=$(mktemp)
    trap 'rm -f -- "$synthesis_output"' EXIT
    output=$(run 0 "${common[@]}" -v -s "$synthesis_output" \
        --arms real:small:forward,real:any:backward,unreal:formula:backward)
    [[ $verbose_enabled != true || ( $output == *'Starting 2 solver children'* ) ]]
    [[ $verbose_enabled != true || ( $output == *'Forcing the real backend to backward for synthesis'* ) ]]
    [[ $verbose_enabled != true || ( $output == *'[real=small,backend=backward] '* ) ]]
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
    [[ $verbose_enabled != true || ( $output == *'[real=small,backend=spot-guarded] '* ) ]]
    [[ $verbose_enabled != true || ( $output == *'spot-guarded K='* ) ]]
    grep -qx REALIZABLE <<<"$output"
    output=$(run 1 -f 'G(i)' -i i -o o -v --arms unreal:formula:spot-guarded)
    [[ $verbose_enabled != true || ( $output == *'backend=spot-guarded'* ) ]]
    grep -qx UNREALIZABLE <<<"$output"

    # An actual synthesis request must use backward, with an emitted circuit.
    synthesis_output=$(mktemp)
    trap 'rm -f -- "$synthesis_output"' EXIT
    output=$(run 0 -f 'G(i <-> X(o))' -i i -o o --spot-fast off -v \
        -s "$synthesis_output" --arms real:small:spot-guarded)
    [[ $verbose_enabled != true || ( $output == *'Forcing the real backend to backward for synthesis'* ) ]]
    [[ $verbose_enabled != true || ( $output == *'backend=backward'* && $output != *'spot-guarded K='* ) ]]
    [[ -s $synthesis_output ]]

    for mode in only fallback; do
        status=2
        [[ $mode == fallback ]] && status=0
        output=$(ACACIA_SPOT_MAX_EXPANSIONS=0 run "$status" \
            -f 'G(i <-> X(o))' -i i -o o --spot-fast off -v \
            --arms real:small:spot-guarded --candidate-mode "$mode")
        [[ $verbose_enabled != true || ( $output == *'spot-guarded K='* ) ]]
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
        [[ $verbose_enabled != true || ( $output == *'provider=spot-lazy'* && $output == *'backend=spot-guarded'* ) ]]
        [[ $verbose_enabled != true || ( $output == *'spot-lazy worker_formula='* && $output == *'status=WIN_K'* ) ]]
        [[ $verbose_enabled != true || ( $output == *'wrapper_rows_generated=0'* && $output == *'verification_ms='* ) ]]
        # The live worker's captured boundary and lazy factory must see the
        # same formula, including existing simplification/decomposition.
        for formula in 'G(i <-> X(o))' 'G(i <-> X(o)) & G(j <-> X(p))'; do
            eager_output=$(run 0 -f "$formula" -i i,j -o o,p --spot-fast off -v \
                --arms real:small:spot-guarded)
            lazy_output=$(run 0 -f "$formula" -i i,j -o o,p --spot-fast off -v \
                --arms real:small:spot-guarded --real-provider spot-lazy)
            eager_formula=$(sed -n 's/.*Captured worker_formula=//p' <<<"$eager_output")
            lazy_formula=$(sed -n 's/.*spot-lazy worker_formula=//p' <<<"$lazy_output")
            [[ $verbose_enabled != true || ( -n $eager_formula && $eager_formula == "$lazy_formula" ) ]]
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
        [[ $verbose_enabled != true || ( $output == *'backend=backward'* && $output == *'provider=frozen-graph'* ) ]]
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
    # Optional contradiction preprocessing can decide this before the rank
    # game. C3s must match the dense frozen route in either configuration.
    set +e
    dense_output=$("$binary" -f 'G(o <-> X(i))' -i i -o o --spot-fast off \
        --arms unreal:formula:spot-guarded 2>&1)
    dense_status=$?
    set -e
    [[ $dense_status == 1 || $dense_status == 2 ]]
    output=$(run "$dense_status" -f 'G(o <-> X(i))' -i i -o o --spot-fast off \
        --arms unreal:formula:spot-guarded-sparse)
    [[ $output == "$dense_output" ]]
fi
if [[ $lazy_enabled == true && $guarded_enabled == true ]]; then
    for provider in spot-eager spot-lazy; do
        for formula in 'G(o <-> X(i))' 'GF(i) & GF(o)' 'G(o <-> X(i)) & G(p <-> X(j))'; do
            output=$(run 1 -f "$formula" -i i,j -o o,p --spot-fast off -v \
                --arms "unreal:formula:spot-guarded:$provider")
            grep -qx UNREALIZABLE <<<"$output"
            [[ $verbose_enabled != true || ( $output == *"provider=$provider"* && $output == *'status=WIN_K'* ) ]]
        done
        output=$(run 2 -f 'G(i <-> X(o))' -i i -o o --spot-fast off -K 3 \
            --arms "unreal:formula:spot-guarded:$provider")
        grep -qx UNKNOWN <<<"$output"
        output=$(run 0 -f 'G(i <-> X(o))' -i i -o o --spot-fast off \
            --arms "real:small:backward,real:small:spot-guarded:$provider")
        grep -qx REALIZABLE <<<"$output"
    done
    if [[ -n $taa_cap_default ]]; then
        output=$(run 2 -h)
        [[ $output == *"--candidate-mode VAL     [only|fallback] on candidate resource limits (default $candidate_default)"* ]]
        for provider in spot-eager spot-lazy; do
            bounded=(-f 'G(i <-> X(o))' -i i -o o --spot-fast off \
                     --arms "real:small:spot-guarded:$provider")
            status=2
            [[ $candidate_default == fallback ]] && status=0
            output=$(ACACIA_SPOT_TAA_MAX_RANK_NODES=0 run "$status" "${bounded[@]}")
            if [[ $candidate_default == fallback ]]; then
                [[ $output == *'fallback provider=frozen-graph backend=backward'* ]]
            else
                grep -qx UNKNOWN <<<"$output"
            fi

            # The explicit mode overrides the compiled default. A TAA-specific
            # cap takes precedence over the common research override.
            output=$(ACACIA_SPOT_TAA_MAX_RANK_NODES=0 run 2 "${bounded[@]}" --candidate-mode only)
            grep -qx UNKNOWN <<<"$output"
            output=$(ACACIA_SPOT_MAX_RANK_NODES=0 ACACIA_SPOT_TAA_MAX_RANK_NODES=128 \
                run 0 "${bounded[@]}" --candidate-mode only)
            grep -qx REALIZABLE <<<"$output"
            output=$(ACACIA_SPOT_MAX_RANK_NODES=128 ACACIA_SPOT_TAA_MAX_RANK_NODES=0 \
                run 2 "${bounded[@]}" --candidate-mode only)
            grep -qx UNKNOWN <<<"$output"

            # Check the configured cap at the actual worker boundary, with no
            # budget environment override and without depending on verbosity.
            (
                capture_dir=$(mktemp -d)
                trap 'rm -rf -- "$capture_dir"' EXIT
                unset ACACIA_SPOT_MAX_RANK_NODES ACACIA_SPOT_TAA_MAX_RANK_NODES
                output=$(ACACIA_SPOT_CAPTURE_DIR="$capture_dir" run 0 "${bounded[@]}")
                python3 - "$capture_dir" "$taa_cap_default" <<'PY'
import json, pathlib, sys
records = [json.loads(p.read_text()) for p in pathlib.Path(sys.argv[1]).glob('*.json')]
caps = [int(r['max_rank_nodes']) for r in records if 'max_rank_nodes' in r]
assert caps and all(cap == int(sys.argv[2]) for cap in caps), caps
PY
            )
        done
        # A TAA override must not relax the frozen guarded worker's own cap.
        output=$(ACACIA_SPOT_MAX_RANK_NODES=0 ACACIA_SPOT_TAA_MAX_RANK_NODES=128 \
            run 2 -f 'G(i <-> X(o))' -i i -o o --spot-fast off \
            --arms real:small:spot-guarded-sparse --candidate-mode only)
        grep -qx UNKNOWN <<<"$output"
    fi
fi
