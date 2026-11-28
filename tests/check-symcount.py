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
    "pairwise.tlsf": "REALIZABLE",
    "recursive-exclusion.tlsf": "REALIZABLE",
    "asymmetric-constraint.tlsf": "DECLINED",
    "eventual-constraint.tlsf": "REALIZABLE",  # initial weight 4 must remain available
    "local-event-a.tlsf": "DECLINED",
    "local-event-b.tlsf": "DECLINED",
    "local-event-c.tlsf": "DECLINED",
    "local-event-d.tlsf": "DECLINED",
    "since-one-family.tlsf": "DECLINED",
    "since-cardinality.tlsf": "DECLINED",
    "empty-safe-intersection.tlsf": "UNREALIZABLE",
    "empty-safe-set.tlsf": "UNREALIZABLE",
    "weights-04-copy.tlsf": "UNREALIZABLE",
    "input-constraint-must-lose.tlsf": "UNREALIZABLE",
    "under-x.tlsf": "REALIZABLE",
    "under-f.tlsf": "REALIZABLE",
    "under-implication.tlsf": "DECLINED",
    "under-disjunction.tlsf": "REALIZABLE",
    "under-assumption.tlsf": "REALIZABLE",
    "strict-under-assumption.tlsf": "REALIZABLE",
    "strict-moore-under-assumption.tlsf": "REALIZABLE",
    "moore-under-x.tlsf": "REALIZABLE",
    "strict-moore-under-x.tlsf": "REALIZABLE",
    "input-composition-budget.tlsf": "DECLINED",
    "output-composition-budget.tlsf": "DECLINED",
    "eager-compositions.tlsf": "DECLINED",
    "duplicate-storage.tlsf": "DECLINED",
    "pruned-duplicate-storage.tlsf": "DECLINED",
    "partial-family.tlsf": "DECLINED",
    "no-family.tlsf": "DECLINED",
    "guard-false-sink.tlsf": "REALIZABLE",
    "guard-late-failure.tlsf": "REALIZABLE",
    "guard-pending-prefix.tlsf": "UNREALIZABLE",
    "guard-mixed.tlsf": "DECLINED",
    "guard-incomplete.tlsf": "DECLINED",
    # Nested since monitors are outside the fragment; reference verdicts below.
    "since-under-x.tlsf": "DECLINED",  # REALIZABLE
    "since-under-next.tlsf": "DECLINED",  # REALIZABLE
    "since-negated-next.tlsf": "DECLINED",  # UNREALIZABLE
}
# Independent parity verdicts for the guarded and generated-event cases.
references = {
    "local-event-a.tlsf": "UNREALIZABLE",
    "local-event-b.tlsf": "UNREALIZABLE",
    "local-event-c.tlsf": "UNREALIZABLE",
    "local-event-d.tlsf": "UNREALIZABLE",
    "since-one-family.tlsf": "REALIZABLE",
    "since-cardinality.tlsf": "REALIZABLE",
    "under-assumption.tlsf": "REALIZABLE",
    "strict-under-assumption.tlsf": "REALIZABLE",
    "strict-moore-under-assumption.tlsf": "REALIZABLE",
    "under-implication.tlsf": "REALIZABLE",
    "guard-false-sink.tlsf": "REALIZABLE",
    "guard-late-failure.tlsf": "REALIZABLE",
    "guard-pending-prefix.tlsf": "UNREALIZABLE",
    "guard-mixed.tlsf": "REALIZABLE",
    "guard-incomplete.tlsf": "REALIZABLE",
}
vertex_limits = {
    "input-composition-budget.tlsf": 1,
    "output-composition-budget.tlsf": 2,
    "eager-compositions.tlsf": 2,
    "duplicate-storage.tlsf": 3,
    "pruned-duplicate-storage.tlsf": 3,
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
reasons = {
    "partial-family.tlsf": "a local conjunct is not instantiated at every index",
    "asymmetric-constraint.tlsf": "an aggregate covers part of the index range",
    "guard-mixed.tlsf": "mixed local guards",
    "guard-incomplete.tlsf": "a local conjunct is not instantiated at every index",
}
codes = {"REALIZABLE": 0, "UNREALIZABLE": 1, "DECLINED": 2}
failed = False
for name, expected in cases.items():
    path = root / name
    args = [binary, path]
    if name in vertex_limits:
        args += ["--max-vertices", str(vertex_limits[name])]
    done = subprocess.run(args, capture_output=True, text=True, timeout=120)
    word = (done.stdout.split() or ["?"])[0]
    if (word != expected or done.returncode != codes[expected]
            or (name in reasons and f"reason={reasons[name]}" not in done.stdout)):
        print(f"{path.name}: expected {expected}, got {done.returncode} {done.stdout}{done.stderr}")
        failed = True
    if name in vertex_limits and "reason=split limit" not in done.stdout:
        print(f"{name}: expected a pre-allocation split decline, got {done.stdout}")
        failed = True
    if name in references:
        reference = subprocess.run(
            [binary.with_name("acacia-bonsai"), "--arms", "both:parity:spot", "-T", path],
            capture_output=True, text=True, timeout=120)
        if (reference.stdout.strip() != references[name]
                or reference.returncode != codes[references[name]]):
            print(f"{name}: parity expected {references[name]}, got {reference.stdout}{reference.stderr}")
            failed = True
sys.exit(1 if failed else 0)
