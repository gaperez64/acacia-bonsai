#!/bin/bash
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH

SYFCO=./syfco
ACABON=../debug/src/acacia-bonsai
FILE=$1
PART=$(mktemp)
RES=$(mktemp)
LTL=$($SYFCO $FILE -f ltlxba -m fully -pf $PART)

parttoinsouts () {
    while IFS= read line; do
        line=$(echo "$line" | sed 's/[[:space:]]*$//;s/[[:space:]]\+/,/g')
        head=${line/,*/}
        args=${line/$head,/}
        case "$head" in
            .inputs) ins=$args;;
            .outputs) outs=$args;;
        esac
    done < $1
}

parttoinsouts $PART

$ACABON -c REAL --formula="$LTL" --ins="$ins" --outs="$outs" --winreg="$RES" -v --init-states="0,1,2;2,4,2;1,1,1"
echo "$(cat $RES)"
rm -f $PART $RES
