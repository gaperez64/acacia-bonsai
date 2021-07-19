echoandrun () {
  echo "$@"
  "$@"
}

FILE=$1
LTL='!('$(syfco $FILE -f ltlxba -m fully -os moore)')'
OUT=$(syfco $FILE --print-input-signals)
IN=$(syfco $FILE --print-output-signals)
echoandrun ltlsynt --realizability --formula="$LTL" --ins="$IN" --outs="$OUT"
