#!/bin/zsh -f

PROG=$0

usage () {
  cat <<EOF
Usage: $0 [-d] PROGNAME MESON-TEST.JSON
  -d: Disregard Spot computational time.
EOF
  exit 1
}

(( $# == 2 || $# == 3 )) || usage

disregard_spot=false
if (( $# == 3 )); then
  case $1; in
    '-d') disregard_spot=true;;
    *) usage;;
  esac
  shift
fi

cat <<EOF
{
  "stats": {  
EOF

if $disregard_spot; then
  grep 'Time disregarding' $2 | \
     perl -pe 's@.*? / (.*?)".*Time disregarding Spot translation: (.*?) seconds.*"result": (.*?),.*@"\1": { "status": \3, "rtime": \2 },@;s/"OK"/true/;s@"(KO|TIMEOUT|FAIL)"@false@'
else
  perl -pe 's@.*? / (.*?)".*"result": (.*?),.*"duration": (.*?),.*@"\1": { "status": \2, "rtime": \3 },@;s/"OK"/true/;s@"(KO|TIMEOUT|FAIL)"@false@' $2
fi


cat <<EOF
   "result": { "status": false, "rtime": 0 }
 },
 "preamble": {
    "program": "$1",
    "prog_alias": "$1"
  }
}
EOF
