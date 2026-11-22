#!/usr/bin/env python3
"""acacia-symcount (#454): verdicts inside the count-quotient fragment, declines outside.

With no corpus argument, use only committed fixtures. The optional corpus
cases run separately when acacia_tlsf_corpus_dir is configured.
"""

from pathlib import Path
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parent / "fixtures" / "symcount"
cases = {
    "indexed-copy.tlsf": "REALIZABLE",  # Mealy copy with paired input/output families
    "breakpoint.tlsf": "UNREALIZABLE",  # alternating hits refute simultaneous recurrence
    "color-reset.tlsf": "REALIZABLE",  # top-level since monitor, reset on the first letter
    "event-collision.tlsf": "UNREALIZABLE",  # legal input name matches an internal event
    "event-renamed.tlsf": "UNREALIZABLE",  # same spec, unused input renamed
    "partial-family.tlsf": "DECLINED",
    "no-family.tlsf": "DECLINED",
    # Nested since monitors are outside the fragment; reference verdicts below.
    "since-under-x.tlsf": "DECLINED",  # REALIZABLE
    "since-under-next.tlsf": "DECLINED",  # REALIZABLE
    "since-negated-next.tlsf": "DECLINED",  # UNREALIZABLE
}
if len(sys.argv) > 2:
    root = Path(sys.argv[2])
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
    path = root / name
    done = subprocess.run([binary, path], capture_output=True, text=True, timeout=120)
    word = (done.stdout.split() or ["?"])[0]
    if (word != expected or done.returncode != codes[expected]
            or (name == "partial-family.tlsf"
                and "reason=a local conjunct is not instantiated at every index" not in done.stdout)):
        print(f"{path.name}: expected {expected}, got {done.returncode} {done.stdout}{done.stderr}")
        failed = True
sys.exit(1 if failed else 0)
