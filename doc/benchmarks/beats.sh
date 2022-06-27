#!/bin/zsh
(( $# == 2 || $# == 3 )) || { echo "usage: $0 file1.json file2.json [timeoutvalue]"; exit 2 }

tobeat=$1
fighter=$2
timeout=${3:-17}

while read test; do
  case $test; in
    *status*) ;;
    *) continue ;;
  esac
  name=$(sed 's@[^/]*/\([^"]*\).*@\1@' <<< $test)
  timetobeat=$(sed 's/.*"rtime": \([0-9.e-]*\) .*/\1/' <<< $test)
  fightertime=$(grep -F "/$name\"" $fighter | grep '"status": true' | sed 's/.*"rtime": \([0-9.e-]*\) .*/\1/')
  [[ -z $fightertime ]] && continue
  case $test; in
    *'"status": false'*) echo "$name (TO vs $fightertime, at least diff $(printf '%08f' $((timeout - fightertime))))";;
    *)  if (( fightertime < timetobeat )); then
          echo "$name ($timetobeat vs $fightertime, diff $(printf '%08f' $((timetobeat - fightertime))))"
        fi;;
  esac
done < $tobeat
