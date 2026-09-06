#!/usr/bin/env bash
set -euo pipefail

binary=$1

# A verdict is a line of its own, so match it as one.  Substring matching here
# would accept UNREALIZABLE where REALIZABLE was wanted, and the exit status is
# the only thing that would have caught it.
expect() {
    local wanted_status=$1
    local wanted_verdict=$2
    shift 2

    local output
    local status
    set +e
    output=$("$binary" "$@" 2>&1)
    status=$?
    set -e
    if [[ $status -ne $wanted_status || $output == *'BDD error:'* ]] || \
       ! grep -qxF "$wanted_verdict" <<<"$output"; then
        printf 'expected status=%s verdict=%s, got status=%s output=%q\n' \
            "$wanted_status" "$wanted_verdict" "$status" "$output" >&2
        return 1
    fi
}

# A diagnostic is prose, not a line we control, so it stays a substring match.
expect_diagnostic() {
    local wanted_status=$1
    local wanted_text=$2
    shift 2

    local output
    local status
    set +e
    output=$("$binary" "$@" 2>&1)
    status=$?
    set -e
    if [[ $status -ne $wanted_status || $output != *"$wanted_text"* ]]; then
        printf 'expected status=%s text=%s, got status=%s output=%q\n' \
            "$wanted_status" "$wanted_text" "$status" "$output" >&2
        return 1
    fi
}

expect 0 REALIZABLE -f 'G(i | !i)' -i i -o ''
expect 1 UNREALIZABLE -f 'G(i)' -i i -o ''
expect 0 REALIZABLE -f 'F(o)' -i '' -o o
expect 1 UNREALIZABLE -f 'G(o) & G(!o)' -i '' -o o
expect 1 UNREALIZABLE -f 'false & (i | o)' -i i -o o
expect_diagnostic 3 "output value '.outputs' is a partition marker" \
    -f 'G(i)' -i i -o .outputs
