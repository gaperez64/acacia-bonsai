#!/usr/bin/env python3
"""Serial single-arm parity with frozen-graph on an existing corpus suite."""
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
decided = unknown = comparisons = records = explored = 0
with tempfile.TemporaryDirectory(prefix="fm-otf-") as directory:
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
        for arm in ("real:small", "unreal:formula"):
            pair = []
            for provider in ("frozen-graph", "spot-fm-otf"):
                for old in captures.glob("*.json"):
                    old.unlink()
                command = [binary, "-F", str(spec), "-i", ",".join(inputs), "-o", ",".join(outputs),
                           "--arms", f"{arm}:spot-guarded-sparse:{provider}", "--spot-fast", "off",
                           "--weakening", "off", "-M", "1", "-I", "1", "-K", "3"]
                # timeout owns a process group, so a capped call drains its
                # workers before the next solver invocation starts.
                result = subprocess.run(["timeout", "--kill-after=2s", "15s", *command],
                                        capture_output=True, text=True,
                                        env=dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(captures),
                                                 ACACIA_SPOT_MAX_EXPANSIONS="1000",
                                                 ACACIA_SPOT_MAX_QUERY_STEPS="100000",
                                                 ACACIA_SPOT_MAX_RANK_NODES="10000"))
                assert result.returncode in (0, 1, 2, 124, 137), (command, result.stdout, result.stderr)
                verdict = result.returncode if result.returncode < 3 else 2
                if provider == "spot-fm-otf":
                    for capture in captures.glob("*.json"):
                        record = json.loads(capture.read_text())
                        if record.get("provider") != provider:
                            continue  # An earlier exact bypass can decide without the provider.
                        assert int(record["provider_rows_explored"]) >= 0, record
                        explored += int(record["provider_rows_explored"])
                        records += 1
                    decided += verdict != 2
                    unknown += verdict == 2
                if verdict != 2:
                    assert verdict == (0 if folder == "realizable" else 1), (command, verdict)
                pair.append(verdict)
            if pair[0] != 2 and pair[1] != 2:
                assert pair[0] == pair[1], (name, arm, pair)
                comparisons += 1
assert decided and comparisons and records and explored, (decided, comparisons, records, explored)
print(f"{suite}: {len(entries)} inputs, {comparisons} decided pairs, "
      f"FM {decided} decided/{unknown} UNKNOWN, {records} provider records; 0 disagreements")
