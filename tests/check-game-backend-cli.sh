#!/usr/bin/env bash
set -euo pipefail

binary=$1

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
[[ $output == *REALIZABLE* ]]

output=$(run 0 -f 'G(o)' -i i -o o -r small -u formula -v \
    --real-backend backward --unreal-backend forward)
[[ $output == *'[real=small,backend=backward] '* ]]
[[ $output == *'[unreal=formula,pref=small,backend=forward] '* ]]

output=$(run 3 -f 'G(o)' -i i -o o --real-backend bogus)
[[ $output == *backward* && $output == *forward* ]]
