#!/bin/sh
# The offline CPre replay must reproduce the solver's own update exactly.  This
# is the correctness floor for every compressed-representation experiment: a
# compression claim about the *operation* is meaningless if the explicit
# operation cannot first be reproduced outside the solver.
set -eu

solver=$1
replay=$2
ltl=$3
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

part="${ltl%.ltl}.part"
ins=$(sed -n 's/^\.inputs *//p' "$part" | tr -s ' ' ',' | sed 's/,$//')
outs=$(sed -n 's/^\.outputs *//p' "$part" | tr -s ' ' ',' | sed 's/,$//')

ACACIA_DIAG=1 \
ACACIA_ANTICHAIN_SNAPSHOT_DIR="$temporary" \
ACACIA_ANTICHAIN_SNAPSHOT_CPRE=1 \
ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX=4 \
ACACIA_DIAG_PROGRESS_EVERY=0 \
  "$solver" --spot-fast off -r small -F "$ltl" -i "$ins" -o "$outs" >/dev/null 2>&1 || true

# At least one automaton directory must carry at least one recorded update, or
# the test would pass by measuring nothing.
found=0
for dir in "$temporary"/aut-*; do
  [ -d "$dir" ] || continue
  events=$(find "$dir" -name 'cpre-*.tsv' | wc -l)
  [ "$events" -gt 0 ] || continue
  found=$((found + events))

  output=$("$replay" --dir "$dir")
  # Every row must report an exact reproduction.
  printf '%s\n' "$output" | awk -F '\t' '
    NR == 1 { if ($7 != "exact") exit 1; next }
    { if ($7 != "yes") exit 1
      if ($5 != $6) exit 1 }
  '
done

if [ "$found" -eq 0 ]; then
  echo "check-cpre-replay: no CPre events were recorded" >&2
  exit 1
fi
