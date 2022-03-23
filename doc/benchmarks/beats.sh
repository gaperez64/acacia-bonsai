#!/bin/zsh
(( $# == 2 )) || { echo "usage: $0 file1.json file2.json"; exit 2 }

tobeat=$1
fighter=$2

while read test; do
  case $test; in
    *status*) ;;
    *) continue ;;
  esac
  name=$(sed 's@[^/]*/\([^"]*\).*@\1@' <<< $test)
  timetobeat=$(sed 's/.*"rtime": \([0-9.]*\) .*/\1/' <<< $test)
  fightertime=$(grep -F "/$name\"" $fighter | grep '"status": true' | sed 's/.*"rtime": \([0-9.]*\) .*/\1/')
  [[ -z $fightertime ]] && continue
  case $test; in
    *'"status": false'*) echo "$name (TO vs $fightertime)";;
    *)  if (( fightertime < timetobeat )); then
          echo "$name ($timetobeat vs $fightertime, diff $((timetobeat - fightertime)))"
        fi;;
  esac
done < $tobeat
