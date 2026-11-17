"""Materialized provider parity and transactional fallback checks."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, test_dir, guarded = sys.argv[1:]
base = [binary, "-K", "5", "--weakening", "off"]
arms = ["real:small:backward"]
if guarded == "true":
    arms += ["real:small:spot-guarded-sparse", "real:any:spot-guarded-sparse",
             "unreal:formula:spot-guarded-sparse"]


def run(args, extra=(), env=None, arm=arms[0]):
    result = subprocess.run(base + ["--arms", arm] + list(extra) + args, env=env, text=True,
                            capture_output=True, timeout=15)
    assert result.returncode in (0, 1, 2), (result.returncode, result.stdout, result.stderr)
    return result.returncode, result.stdout, result.stderr


provider = ["--automaton-provider", "closure-recurrence"]
for name in ("realizable/ltl2dba_U1_1", "realizable/ltl2dba_C1_1",
             "unrealizable/ltl2dba_psi_1", "unrealizable/ltl2dba27"):
    source = Path(test_dir) / "ltl" / name
    partition = dict(line.split(maxsplit=1) for line in source.with_suffix(".part").read_text().splitlines())
    args = ["-F", str(source.with_suffix(".ltl")), "-i", partition[".inputs"],
            "-o", partition[".outputs"]]
    for arm in arms:
        reference = run(args, arm=arm)
        for mode in ("fallback", "only"):
            actual = run(args, ["--candidate-mode", mode],
                         arm=arm + ":closure-recurrence")
            assert actual[:2] == reference[:2], (name, arm, mode, reference, actual)

# A real arm is a realizability semidecision: an unrealizable input returns
# UNKNOWN when no unreal arm is selected. Neither union branch may win alone.
for formula in ("G o", "G !o"):
    assert run(["-f", formula, "-i", "i", "-o", "o"], provider)[0] == 0
conflict = ["-f", "G o & G !o", "-i", "i", "-o", "o"]
assert run(conflict, provider)[0] == 2
full = subprocess.run([binary, "--arms", "real:small:backward:closure-recurrence,"
                       "unreal:formula:backward"] + conflict,
                      text=True, capture_output=True, timeout=15)
assert full.returncode == 1 and full.stdout.strip() == "UNREALIZABLE", full
for arm in arms[1:]:
    reference = run(conflict, arm=arm)
    assert reference[0] == (1 if arm.startswith("unreal:") else 2), reference
    for mode in ("fallback", "only"):
        actual = run(conflict, ["--candidate-mode", mode], arm=arm + ":closure-recurrence")
        assert actual[:2] == reference[:2], (arm, mode, reference, actual)

# These distinguish the original Mealy objective from the swapped, shifted
# formula-unreal objective (unlike recurrence formulas invariant under X).
for formula in ("G(i <-> X(o))", "G(o <-> X(i))"):
    args = ["-f", formula, "-i", "i", "-o", "o"]
    for arm in arms[1:]:
        reference = run(args, arm=arm)
        for mode in ("only", "fallback"):
            actual = run(args, ["--candidate-mode", mode], arm=arm + ":closure-recurrence")
            assert actual[:2] == reference[:2], (formula, arm, mode, reference, actual)

# Force construction failure through the existing provider state cap; records
# must identify the actual translation route, including the failure in only.
with tempfile.TemporaryDirectory(prefix="closure-recurrence-") as tmp:
    args = ["-f", "G F i <-> G F o", "-i", "i", "-o", "o"]
    for arm in arms:
        reference = run(args, arm=arm)
        for mode in ("only", "fallback"):
            records = Path(tmp) / (arm + mode)
            records.mkdir()
            env = dict(os.environ, ACACIA_SPOT_MAX_PROVIDER_STATES="1",
                       ACACIA_PHASE_RECORDS=str(records))
            result = run(args, ["--candidate-mode", mode], env, arm + ":closure-recurrence")
            assert result[0] == (2 if mode == "only" else reference[0]), (arm, mode, result)
            events = [json.loads(line) for path in records.glob("*.jsonl")
                      for line in path.read_text().splitlines()]
            metrics = {(e.get("key"), e.get("value")) for e in events}
            assert ("provider", "closure-recurrence") in metrics, events
            assert ("construction_result", "unknown" if mode == "only" else "fallback") in metrics
            assert any(e.get("event") == "route_stopped" and e.get("reason") == "resource"
                       and e.get("stage") == "closure-recurrence" for e in events), events
            assert any(e.get("event") == "stage_stopped" and e.get("reason") == "resource"
                       and e.get("stage") == "closure-recurrence" for e in events), events
            if mode == "only":
                assert any(e.get("event") == "terminal_result" and e.get("reason") == "resource"
                           and e.get("exit_code") == 2 for e in events), events
                assert not any(e.get("stage") in ("translation", "preprocessing", "k-attempt")
                               for e in events), events
            else:
                assert ("graph_provider", "frozen-graph") in metrics, events
                assert any(e.get("route") == "frozen_fallback" and
                           e.get("effective_backend") == arm.split(":")[-1] for e in events), events
                assert any(e.get("stage") == "translation" for e in events), events

    for arm in arms:
        records = Path(tmp) / (arm + "success")
        records.mkdir()
        capture = records / "capture"
        result = run(args, env=dict(os.environ, ACACIA_PHASE_RECORDS=str(records),
                                   ACACIA_SPOT_CAPTURE_DIR=str(capture)),
                     arm=arm + ":closure-recurrence")
        assert result[0] == run(args, arm=arm)[0], (arm, result)
        events = [json.loads(line) for path in records.glob("*.jsonl")
                  for line in path.read_text().splitlines()]
        metrics = {(e.get("key"), e.get("value")) for e in events}
        assert ("construction_result", "used") in metrics, events
        assert ("graph_provider", "closure-recurrence") in metrics, events
        assert {"provider", "branches", "recurrence_blocks", "watches", "states", "edges",
                "construction_ns"} <= {key for key, _ in metrics}, events
        assert not any(e.get("stage") == "translation" for e in events), events
        snapshots = [json.loads(path.read_text()) for path in capture.glob("*.json")]
        assert snapshots and all(s["provider"] == "closure-recurrence" for s in snapshots), snapshots

for arm in ("real:any:backward", "real:small:spot-guarded", "unreal:formula:backward",
            "unreal:automaton:spot-guarded-sparse"):
    rejected = subprocess.run([binary, "--arms", arm + ":closure-recurrence"] + conflict,
                              text=True, capture_output=True, timeout=15)
    assert rejected.returncode == 3, rejected
for arm in arms:
    for mode in ("only", "fallback"):
        rejected = subprocess.run(
            base + ["--arms", arm + ":closure-recurrence", "--candidate-mode", mode,
                    "-s", "-"] + conflict, text=True, capture_output=True, timeout=15)
        assert rejected.returncode == 3 and "does not support controller synthesis" in rejected.stderr, rejected
print("closure recurrence CLI parity, union, scope and fallback passed")
