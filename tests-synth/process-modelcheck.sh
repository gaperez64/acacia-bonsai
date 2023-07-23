syntf=$1
origf=$2
TESTFOLDER=$3

# Generate monitor
./meyerphi-syfco --format smv-decomp "$origf" --mode fully | sed -e "/^/n" | ./smvtoaig -L ./ltl2smv > "$TESTFOLDER/monitor.aig"

if [ ! -f "$TESTFOLDER/monitor.aig" ]; then
    echo "Error=The monitor file was not generated!"
    exit -1
fi

# Combine monitor with synthesized file
./combine-aiger "$TESTFOLDER/monitor.aig" "${syntf}" > "${syntf}-combined.aag"

if [ ! -f "${syntf}-combined.aag" ]; then
    echo "Error=The combined file was not generated!"
    exit -1
fi

if ! (head -n 1 "${syntf}-combined.aag" | grep -q '^aag'); then
    echo "Error=during monitor combination"
    exit -1
fi

# Model checking each justice constraint of the combined file in parallel
num_justice=$(head -n 1 "${syntf}-combined.aag" | cut -d" " -f9);

# sequential check
TLIMIT=1000
# TODO: stop hardcoding the above value
echo "read_aiger_model -i ${syntf}-combined.aag; encode_variables; build_boolean_model; check_ltlspec_ic3; quit" | timeout -k 10 ${TLIMIT} ./nuXmv -int >"${syntf}-res" 2>&1
res_val=$?

# check result
if [ $res_val -eq 0 ]; then
    num_true=$(grep -c 'specification .* is true' "${syntf}-res" || true)
    num_false=$(grep -c 'specification .* is false' "${syntf}-res" || true)
    if [ $num_false -ge 1 ]; then
        echo "starexec-result=MC-INCORRECT"
        echo "Error=model checker says no!"
        exit -1
    elif [ $num_true -lt $num_justice ]; then
        echo "starexec-result=MC-ERROR"
        echo "Error=not all justice properties found"
        exit -1
    else
        echo "Model_check_result=SUCCESS"
        exit 0  # <- happy exit code
    fi
elif [ $res_val -eq 124 ] || [ $res_val -eq 137 ]; then
    echo "starexec-result=MC-TIMEOUT"
    exit -1
else
    echo "starexec-result=MC-ERROR"
    echo "Error=unknown MC error, returned ${res_val}"
    exit -1
fi
