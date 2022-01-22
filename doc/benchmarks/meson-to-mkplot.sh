#!/bin/zsh -f

(( $# == 2 )) || { echo "usage: $0 PROGNAME MESON-TEST.JSON"; exit 2 }

cat <<EOF
{
  "stats": {  
EOF

perl -pe 's@.*? / (.*?)".*"result": (.*?),.*"duration": (.*?),.*@"\1": { "status": \2, "rtime": \3 },@;s/"OK"/true/;s@"(KO|TIMEOUT)"@false@' $2

cat <<EOF
   "result": { "status": false, "rtime": 0 }
 },
 "preamble": {
    "program": "$1",
    "prog_alias": "$1"
  }
}
EOF
