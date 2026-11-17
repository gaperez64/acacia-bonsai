"""Small real/backward provider parity and transactional fallback checks."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, test_dir = sys.argv[1:]
base = [binary, "--arms", "real:small:backward", "-K", "5", "--weakening", "off"]


def run(args, extra=(), env=None):
    result = subprocess.run(base + list(extra) + args, env=env, text=True,
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
    reference = run(args)
    for mode in ("fallback", "only"):
        actual = run(args, provider + ["--candidate-mode", mode])
        assert actual[:2] == reference[:2], (name, mode, reference, actual)

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

# Force construction failure through the existing provider state cap; records
# must identify the actual translation route, including the failure in only.
with tempfile.TemporaryDirectory(prefix="closure-recurrence-") as tmp:
    args = ["-f", "G F i <-> G F o", "-i", "i", "-o", "o"]
    for mode, expected in (("only", 2), ("fallback", 0)):
        records = Path(tmp) / mode
        records.mkdir()
        env = dict(os.environ, ACACIA_SPOT_MAX_PROVIDER_STATES="1",
                   ACACIA_PHASE_RECORDS=str(records))
        result = run(args, provider + ["--candidate-mode", mode], env)
        assert result[0] == expected, result
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
        if mode == "fallback":
            assert any(e.get("route") == "frozen_fallback" for e in events), events
            assert any(e.get("stage") == "translation" for e in events), events

    records = Path(tmp) / "success"
    records.mkdir()
    result = run(["-f", "G F i <-> G F o", "-i", "i", "-o", "o"], provider,
                 dict(os.environ, ACACIA_PHASE_RECORDS=str(records)))
    assert result[0] == 0, result
    events = [json.loads(line) for path in records.glob("*.jsonl")
              for line in path.read_text().splitlines()]
    metrics = {(e.get("key"), e.get("value")) for e in events}
    assert ("construction_result", "used") in metrics, events
    assert {"provider", "branches", "recurrence_blocks", "watches", "states", "edges",
            "construction_ns"} <= {key for key, _ in metrics}, events
    assert not any(e.get("stage") == "translation" for e in events), events

for arm in ("real:any:backward", "real:small:spot-guarded-sparse",
            "unreal:formula:backward"):
    rejected = subprocess.run([binary, "--arms", arm + ":closure-recurrence"] + conflict,
                              text=True, capture_output=True, timeout=15)
    assert rejected.returncode == 3, rejected
print("closure recurrence CLI parity, union, scope and fallback passed")
