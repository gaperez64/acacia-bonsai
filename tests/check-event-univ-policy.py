#!/usr/bin/env python3
"""Check runtime policy parsing and propagation through forked translation workers."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    invalid = subprocess.run([binary, "--event-univ-policy", "invalid"],
                             capture_output=True, text=True)
    assert invalid.returncode > 2 and "expects guarded or expanded" in invalid.stderr
    formula = "G (a -> F b) & G (b -> X !b) & G (b -> F c) & G (c -> X !c)"
    with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR", ".")) as directory:
        for arm in ("real:small:backward", "real:small:forward",
                    "unreal:formula:spot-guarded-sparse", "unreal:automaton:forward"):
            root = Path(directory) / arm.replace(":", "-")
            (root / "phases").mkdir(parents=True)
            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(root / "phases"),
                       ACACIA_SPOT_CAPTURE_DIR=str(root / "capture"))
            command = [binary, "-f", formula, "-i", "a,unused_i", "-o", "b,c,unused_o",
                       "--arms", arm, "--r-prepass", "off", "--weakening", "off",
                       "--equivariance", "off", "--event-univ-policy", "expanded"]
            process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            stdout, stderr = process.communicate(timeout=15)
            assert process.returncode in (0, 1, 2), (command, stdout, stderr)
            rows = [json.loads(line) for path in (root / "phases").rglob("*.jsonl")
                    for line in path.read_text().splitlines()]
            policy = [row for row in rows if row.get("key") == "event_univ_policy"]
            assert policy and all(row["value"] == "expanded" for row in policy), rows
            assert all(row["worker_pid"] != process.pid for row in policy), policy
            assert any(row.get("event") == "stage_completion" and
                       row.get("stage") == "translation-event-univ" for row in rows), rows
            captures = [json.loads(path.read_text()) for path in (root / "capture").glob("*.json")]
            assert captures and all(row["event_univ_policy"] == "expanded" for row in captures)
    print("expanded policy reaches forked REAL and both UNREAL timing workers")


if __name__ == "__main__":
    main()
