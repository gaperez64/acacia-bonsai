#!/usr/bin/env python3
"""Check identical obligation results and Acacia-owned recording boundaries."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
plain_env = dict(os.environ)
plain_env.pop("ACACIA_PHASE_RECORDS", None)
plain = subprocess.run([binary], env=plain_env, capture_output=True, timeout=60)
assert plain.returncode == 0, plain.stderr

with tempfile.TemporaryDirectory(dir=sys.argv[2]) as directory:
    env = dict(os.environ, ACACIA_PHASE_RECORDS=directory, ACACIA_TEST_RECORD_ACK="1")
    if len(sys.argv) > 3:
        env["LD_PRELOAD"] = str(Path(sys.argv[3]).resolve())
    run = subprocess.run(
        [binary], env=env, capture_output=True, timeout=60
    )
    assert run.returncode == plain.returncode, run.stderr
    assert run.stdout == plain.stdout, (plain.stdout, run.stdout)
    events = [
        json.loads(line)
        for path in Path(directory).glob("*.jsonl")
        for line in path.read_text().splitlines()
    ]
    assert all(e.get("dropped_records", 0) == 0 for e in events), events
    segments = sorted(
        (e for e in events if e.get("event") == "accounting_segment"), key=lambda e: e["entry_ns"]
    )
    assert segments and all(
        a["mono_ns"] <= b["entry_ns"] for a, b in zip(segments, segments[1:])
    ), segments
    ends = [e for e in events if e.get("event") == "accounting_end"]
    for end in ends:
        children = [e for e in ends if e["parent_phase_id"] == end["phase_id"]]
        own = [e for e in segments if e["phase_id"] == end["phase_id"]]
        for key in ["wall_ns", "cpu_ns"]:
            assert sum(e[key] for e in own) == end[key] - sum(e[key] for e in children), (
                end,
                own,
                children,
            )
    completed = {e["phase"] for e in events if e.get("event") == "accounting_end"}
    assert {"obligation_preprocessing", "obligation_spot", "obligation_strategy"} <= completed
    assert not {"obligation_translation", "obligation_game"} & completed
    for scope in (e for e in ends if e["phase"] == "obligation_spot"):
        assert scope["wall_ns"] >= 0 and scope["cpu_ns"] >= 0
        assert not any(e["parent_phase_id"] == scope["phase_id"] for e in ends)
        identities = [e for e in events if e.get("event") == "work_identity"
                      and e["mono_ns"] <= scope["entry_ns"]]
        assert identities and all(len(identities[-1][key]) == 64 for key in (
            "formula_sha256", "alphabet_sha256", "translation_options_sha256",
            "preprocessing_options_sha256"))
    assert any(e.get("event") == "work_identity" for e in events)
print("recorded obligation decisions and controllers passed the helper semantic checks")
