#!/usr/bin/env python3
"""Exercise the live TLSF/worker boundary and fresh C4/C5 construction."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
cases = [
    ("Mealy", "Mealy", "G(i <-> X(o))", "real", 0),
    ("Mealy", "Moore", "G(i <-> X(o))", "real", 0),
    ("Moore", "Mealy", "G(i <-> o)", "unreal", 1),
    ("Moore", "Moore", "G(i <-> o)", "unreal", 1),
    ("Mealy,Strict", "Mealy", "G(o <-> X(i))", "unreal", 1),
]
with tempfile.TemporaryDirectory(prefix="acacia-spot-worker-") as directory:
    root = Path(directory)
    for index, (semantics, target, formula, polarity, expected) in enumerate(cases):
        spec = root / f"{index}.tlsf"
        spec.write_text(f'''INFO {{ TITLE: "worker boundary" SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }} GUARANTEES {{ {formula}; }} }}
''')
        pair = []
        for provider in ("spot-eager", "spot-lazy"):
            captures = root / f"{index}-{provider}"
            arm = "real:small" if polarity == "real" else "unreal:formula"
            env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(captures))
            command = [binary, "-T", str(spec), "--spot-fast", "off", "-K", "5",
                       "--arms", f"{arm}:spot-guarded:{provider}"]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
            assert result.returncode == expected, (command, result.stdout, result.stderr)
            records = [json.loads(p.read_text()) for p in captures.glob("*.json")]
            assert len(records) == 1, records
            record = records[0]
            assert record["status"] == "WIN_K" and float(record["verification_ms"]) >= 0
            assert record["inputs"] == (["i"] if polarity == "real" else ["o"])
            assert record["outputs"] == (["o"] if polarity == "real" else ["i"])
            assert record["requested_provider"] == provider and record["provider"] == provider
            pair.append(record)
        for field in ("worker_formula", "inputs", "outputs", "ap_order", "partition", "k", "status"):
            assert pair[0][field] == pair[1][field], (field, pair)
        assert pair[0]["worker_pid"] != pair[1]["worker_pid"]
        assert int(pair[1]["wrapper_rows_generated"]) <= int(pair[0]["total_wrapper_rows"])
print("5 native TLSF boundary pairs: semantics, polarity, exact construction, and certificates agree")
