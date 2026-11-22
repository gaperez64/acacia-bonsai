#!/usr/bin/env python3
"""acacia-symcount (#454): verdicts inside the count-quotient fragment, declines outside.

Each case's verdict must match the SYNTCOMP status; the fragment cases cover
local safety templates, the breakpoint aggregate (collector_v2), the since
monitor (collector_v1), an UNREALIZABLE answer, an index output family
(arbiter) and two declines.
"""

from pathlib import Path
import subprocess
import sys

binary, corpus = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
cases = {
    "collector_v1_pb_5_pe_.tlsf": "REALIZABLE",
    "collector_v1_pb_9_pe_.tlsf": "REALIZABLE",
    "collector_v2_pb_5_pe_.tlsf": "REALIZABLE",
    "collector_v2_pb_10_pe_.tlsf": "REALIZABLE",
    "ltl2dba_C2_pb_5_pe_.tlsf": "REALIZABLE",
    "ltl2dba_C2_unreal_pb_2_pe_.tlsf": "UNREALIZABLE",
    "arbiter_pb_2_pe_.tlsf": "REALIZABLE",
    "collector_v3_pb_5_pe_.tlsf": "DECLINED",  # an aggregate in an assumption
    "01.tlsf": "DECLINED",  # no index family
}
codes = {"REALIZABLE": 0, "UNREALIZABLE": 1, "DECLINED": 2}
failed = False
for name, expected in cases.items():
    done = subprocess.run([binary, corpus / name], capture_output=True, text=True, timeout=120)
    word = (done.stdout.split() or ["?"])[0]
    if word != expected or done.returncode != codes[expected]:
        print(f"{name}: expected {expected}, got {done.returncode} {done.stdout}{done.stderr}")
        failed = True
sys.exit(1 if failed else 0)
