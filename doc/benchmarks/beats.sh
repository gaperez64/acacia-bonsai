#!/bin/zsh
(( $# == 2 || $# == 3 )) || { echo "usage: $0 tobeat.json fighter.json [timeoutvalue]"; exit 2 }

tobeat=$1
fighter=$2
timeout=${3:-17}

all_tests () {
  cat $tobeat
  while read test; do
    name=$(sed 's@[^/]*/\([^"]*\).*@\1@' <<< $test)
    if ! grep -qF "/$name\"" $tobeat; then
      cat <<EOF
"/$name": { "status": false, "rtime": 0.0 },
EOF
    fi
  done < $fighter
}


all_tests | while read test; do
  case $test; in
    *status*) ;;
    *'"result"'*) continue;;
    *) continue ;;
  esac
  name=$(sed 's@[^/]*/\([^"]*\).*@\1@' <<< $test)
  timetobeat=$(sed 's/.*"rtime": \([0-9.e-]*\) .*/\1/' <<< $test)
  fightertime=$(grep -F "/$name\"" $fighter | grep '"status": true' | sed 's/.*"rtime": \([0-9.e-]*\) .*/\1/')
  [[ -z $fightertime ]] && continue
  case $test; in
    *'"status": false'*) echo "$name (TO vs $fightertime, at least diff $(printf '%08f' $((timeout - fightertime))))";;
    *'"status": true'*)
        if (( fightertime < timetobeat )); then
          echo "$name ($timetobeat vs $fightertime, diff $(printf '%08f' $((timetobeat - fightertime))))"
        fi;;
    *) echo "$tobeat: incorrect status in $test." >&2
       exit 2;;
  esac
done
