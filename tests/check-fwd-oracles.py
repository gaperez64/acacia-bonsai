#!/usr/bin/env python3
"""Serial frozen sparse verdict parity, all P0 oracles versus oracle-off."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, source, suite = sys.argv[1:]
root = Path(source)
manifest = subprocess.check_output(
    [sys.executable, root / "tests/suites/expand-lists.py"], text=True)
entries = [line.split("\t") for line in manifest.splitlines()
           if line.startswith("T\t") and line.split("\t")[2] == suite]
assert entries, suite
decided = unknown = pairs = records = 0
with tempfile.TemporaryDirectory(prefix="fwd-oracles-") as directory:
    captures = Path(directory)
    for _, folder, _, name, path, tlsf in entries:
        assert not tlsf, (name, tlsf)
        spec = root / "tests/ltl" / path
        inputs, outputs = [], []
        target = None
        for line in spec.with_suffix(".part").read_text().splitlines():
            words = line.split("#", 1)[0].replace(",", " ").split()
            if not words:
                continue
            if words[0].lower() in (".inputs", ".outputs"):
                target = inputs if words.pop(0).lower() == ".inputs" else outputs
            assert target is not None
            target.extend(words)
        pair = []
        for mode in ("", "all-on"):
            for old in captures.glob("*.json"):
                old.unlink()
            command = [binary, "-F", str(spec), "-i", ",".join(inputs), "-o", ",".join(outputs),
                       "--arms", "unreal:formula:spot-guarded-sparse:frozen-graph",
                       "--spot-fast", "off", "--weakening", "off", "-M", "1", "-I", "1", "-K", "3"]
            result = subprocess.run(["timeout", "--kill-after=2s", "15s", *command],
                                    capture_output=True, text=True,
                                    env=dict(os.environ, ACACIA_FWD_ORACLE=mode,
                                             ACACIA_SPOT_CAPTURE_DIR=str(captures),
                                             ACACIA_SPOT_MAX_EXPANSIONS="1000",
                                             ACACIA_SPOT_MAX_QUERY_STEPS="100000",
                                             ACACIA_SPOT_MAX_RANK_NODES="10000"))
            assert result.returncode in (0, 1, 2, 124, 137), (command, result.stdout, result.stderr)
            verdict = result.returncode if result.returncode < 3 else 2
            if verdict != 2:
                assert verdict == (0 if folder == "realizable" else 1), (name, mode, verdict)
            pair.append(verdict)
            for capture in captures.glob("*.json"):
                record = json.loads(capture.read_text())
                if "fwd_oracles" not in record:
                    continue  # An earlier exact bypass can finish the worker.
                assert record["fwd_oracles"] == ("cap,sim,sym" if mode else "none"), record
                assert int(record["game_states"]) >= 1, record
                assert int(record["wrapper_rows_generated"]) >= 0, record
                records += 1
        decided += pair[1] != 2
        unknown += pair[1] == 2
        if 2 not in pair:
            assert pair[0] == pair[1], (name, pair)
            pairs += 1
assert pairs and records, (pairs, records)
print(f"ab/{suite}: {len(entries)} inputs, {pairs} decided pairs, all-on {decided} decided / "
      f"{unknown} UNKNOWN, {records} oracle records; 0 disagreements")
