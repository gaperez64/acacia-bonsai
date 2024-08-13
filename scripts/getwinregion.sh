#!/bin/bash
export LD_LIBRARY_PATH=../dbgbuild/subprojects/spot/dist/usr/local/lib64:$LD_LIBRARY_PATH

SYFCO=./syfco
ACABON=../dbgbuild/src/acacia-bonsai
FILE=../tests/ltl/mod-theories/andoni_ex1.tlsf
k=2

PART=$(mktemp)
RES=wreg.aag
LTL=$($SYFCO $FILE -f ltlxba -m fully -pf $PART -op k=$k)
echo "LTL formula: $LTL"

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

echo "Calling acacia-bonsai: $ACABON -c REAL --formula=$LTL --ins=$ins --outs=$outs --winreg=$RES"
$ACABON -c REAL --formula="$LTL" --ins="$ins" --outs="$outs" --winreg="$RES" -v -v -v # --init="0,-1,0,0"
echo "$(cat $RES)"
rm -f $PART
# rm -f $RES
