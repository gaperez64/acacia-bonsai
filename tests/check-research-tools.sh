#!/bin/sh
set -eu

generator=$1
replay=$2
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

printf '%s\n' 'G(o)' >"$temporary/formula.ltl"

metrics=$(
  "$generator" --formula "$temporary/formula.ltl" \
    --hoa "$temporary/real.hoa" --name smoke --schedule off \
    --inputs i --outputs o --orientation real --preference small \
    --realizability-simplify --no-header
)

printf '%s\n' "$metrics" | awk -F '\t' '
  NF != 17 { exit 1 }
  $1 != 1 || $2 != "smoke" || $3 != "off" || $4 != "real" { exit 1 }
  $9 < 1 || $11 != 1 || $12 != 1 { exit 1 }
'
grep -q '^HOA:' "$temporary/real.hoa"

verdict=$(
  "$replay" --hoa "$temporary/real.hoa" --inputs i --outputs o \
    --orientation real --spot-fast det -K 10
)
test "$verdict" = REALIZABLE

for orientation in unreal-formula unreal-automaton; do
  "$generator" --formula "$temporary/formula.ltl" \
    --hoa "$temporary/$orientation.hoa" --name smoke --schedule off \
    --inputs i --outputs o --orientation "$orientation" --preference small \
    --realizability-simplify --no-header >/dev/null
  grep -q '^HOA:' "$temporary/$orientation.hoa"
done
