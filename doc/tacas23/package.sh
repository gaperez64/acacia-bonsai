#!/bin/zsh

cd _build || { echo "_build: not found.  Compile first: latexmk."; exit 1 }
MAIN=${1:-main}
EXT=fdb_latexmk
[[ -e $MAIN.$EXT ]] || { echo "_build/$MAIN.$EXT not found, exiting."; exit 1 }

rm -f $MAIN.tar.bz2
grep '^[[:space:]]*"[^/]' $MAIN.$EXT | sed 's/^[^"]*"\([^"]*\).*/\1/' | \
    grep -v "^$MAIN.tex\$" | \
    xargs -d '\n' tar cvjf $MAIN.tar.bz2 -P -h --hard-dereference --transform 's,.*/,,' ../src/$MAIN.tex && \
    echo "_build/$MAIN.tar.bz2: done."

