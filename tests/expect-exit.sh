#!/bin/sh
# Usage: expect-exit.sh EXPECTED_CODE PROGRAM [ARGS...]

expected="$1"
shift

"$@"
ec=$?

if [ "$ec" -eq "$expected" ]; then
    exit 0
else
    printf 'Expected exit code %s, got %s\n' "$expected" "$ec" >&2
    exit 1
fi
