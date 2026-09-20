#!/bin/sh
set -eu

replay=$1
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

printf '%s\n' \
  'schema_version	states	bool_threshold	init_state	worker	instance' \
  '3	1	1	0	real	dual-smoke' >"$temporary/meta.tsv"

printf '%s\n' \
  '# schema_version=1 inputs=1 actions=1 transitions=1' \
  '[input	0]' \
  'action	0' \
  '0	0	1' >"$temporary/all-input-actions.tsv"

printf '%s\n' \
  '# schema_version=2 loop=0 k=1 actions=1 before=1 input=0' \
  '[before]' \
  '0' \
  '[actions]' \
  'action	0' \
  '0	0	1' \
  '[after]	1' \
  '-1' >"$temporary/cpre-0.tsv"

"$replay" --dir "$temporary" --task convert --deadline-ms 0 \
  | awk -F '\t' 'NR == 2 { if ($14 != "complete" || $15 != "yes") exit 1 }'

"$replay" --dir "$temporary" --task cpre --deadline-ms 0 \
  | awk -F '\t' 'NR == 2 { if ($19 != "yes" || $20 != "yes" || $21 != "complete") exit 1 }'

for mode in positive negative auto; do
  "$replay" --dir "$temporary" --task solve --k 1 --mode "$mode" --deadline-ms 0 \
    | awk -F '\t' 'NR == 2 { if ($1 != "lose_k" || $16 != "complete") exit 1 }'
done
