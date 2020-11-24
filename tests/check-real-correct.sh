#!/bin/zsh -f

PROG=$0

setandwith () {
    eval "val=\$$1"
    if [[ $val ]]; then
        eval "$1+=\" * $2\""
    else
        eval "$1=\"$2\""
    fi
}

echoandrun () {
    echo "$@"
    eval "$@"
}

usage () {
    echo "Usage: $PROG -r REF_EXE -a ACACIA_EXE -f LTL_FILE -p ACACIA_PLUS_PY -P PYTHON"
    echo "Runs REF_EXE and ACA_EXE on LTL_FILE and ensures they have the same output."
    echo "The part file is LTL_FILE with .ltl replaced by .part."
    exit 1
}

PYTHON=$(whence python2)

while getopts "r:a:f:P:p:x" opt; do
    case $opt; in
        r) REF=$OPTARG;;
        a) ACA=$OPTARG;;
        f) LTL=$OPTARG;;
        P) PYTHON=$OPTARG;;
        p) ACAPLUS=$OPTARG;;
        x) setopt xtrace;;
        h|*) usage;;
    esac
done

shift $((OPTIND - 1))
(( $# )) && usage

[[ -x $REF && -x $ACA && -e $LTL && -x $PYTHON && -e $ACAPLUS ]] || usage

PART=${LTL/.ltl/.part}

## Prefix output with the test file name.
shortfile=${LTL/*tests\//}
exec >  >(sed "s!^![$shortfile] !")
exec 2> >(sed "s!^![$shortfile] !" >&2)

while IFS= read line; do
    line=$(echo "$line" | sed 's/[[:space:]]*$//;s/[[:space:]]\+/,/g')
    head=${line/,*/}
    args=${line/$head,/}
    case "$head" in
        .inputs) ins=$args;;
        .outputs) outs=$args;;
    esac
done < $PART

echo "formula: $(cat $LTL)"
echo "Running the reference..."
echoandrun $REF --realizability -F $LTL --ins $ins --outs $outs
resref=$?

# echo "Running Acacia-+..."
# echoandrun echo \$formula \| $PYTHON $ACAPLUS -L /dev/stdin -P $PART -k 3 -K 30 -C REAL -v 1 -t SPOT -a BACKWARD
# resacaplus=$?

# if (( resref != resacaplus )); then
#     echo "[WARNING]  Acacia+ and the reference differ."
# fi

echo "Running Acacia-Bonsai..."
echoandrun $ACA -F $LTL --ins $ins --outs $outs -k 10
resaca=$?

if (( resref != resaca )); then
    echo -n "FAILED:"
    if (( resref == 0 )); then
        echo 'FALSE NEGATIVE (may happen if K is too small)'
        exit 1
    else
        echo 'FALSE POSITIVE (should never happen)'
        exit 99
    fi
else
    echo "Pass."
    exit 0
fi
