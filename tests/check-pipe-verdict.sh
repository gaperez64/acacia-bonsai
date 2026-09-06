#!/usr/bin/env bash
set -euo pipefail

bin=$1

# Exercise the shared process group used by pipelines under shell job control.
set -m

for ((iteration = 1; iteration <= 20; iteration++)); do
    set +e
    "$bin" -f "G(i -> X o)" -i i -o o 2>/dev/null | cat > /dev/null
    statuses=("${PIPESTATUS[@]}")
    set -e
    if [[ ${statuses[0]} -ne 0 || ${statuses[1]} -ne 0 ]]; then
        printf 'iteration %s: expected statuses 0 0, got acacia=%s cat=%s\n' \
            "$iteration" "${statuses[0]}" "${statuses[1]}" >&2
        exit 1
    fi
done

set +e
output=$("$bin" -f "G(i -> X o)" -i i -o o 2>/dev/null | head -1)
status=$?
set -e
if [[ $status -ne 0 || $output != REALIZABLE ]]; then
    printf 'head -1: expected status=0 and REALIZABLE, got status=%s output=%q\n' \
        "$status" "$output" >&2
    exit 1
fi
