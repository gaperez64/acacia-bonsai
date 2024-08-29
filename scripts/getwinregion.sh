#!/bin/bash
export LD_LIBRARY_PATH=../dbgbuild/subprojects/spot/dist/usr/local/lib64:$LD_LIBRARY_PATH

SYFCO=./syfco
ACABON=../dbgbuild/src/acacia-bonsai
FILE=$1
RES=$2
OPTK=$3
INIT=$4

PART=$(mktemp)
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

if [ -z "${INIT}" ]; then
    echo $ACABON -c REAL --formula="$LTL" --K="$OPTK" --ins="$ins" --outs="$outs" --winreg="$RES"
    $ACABON -c REAL --formula="$LTL" --K="$OPTK" --ins="$ins" --outs="$outs" --winreg="$RES"
    exit
else
    echo $ACABON -c REAL --formula="$LTL" --K="$OPTK" --ins="$ins" --outs="$outs" --winreg="$RES" --init="$INIT"
    $ACABON -c REAL --formula="$LTL" --K="$OPTK" --ins="$ins" --outs="$outs" --winreg="$RES" --init="$INIT"
    exit
fi
