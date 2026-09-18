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
        closure_pair = []
        for provider in ("spot-eager", "spot-lazy", "closure-buchi-eager", "closure-buchi"):
            captures = root / f"{index}-{provider}"
            arm = "real:small" if polarity == "real" else "unreal:formula"
            env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(captures))
            backend = "spot-guarded-sparse" if provider.startswith("closure") else "spot-guarded"
            command = [binary, "-T", str(spec), "--spot-fast", "off", "-K", "5",
                       "--arms", f"{arm}:{backend}:{provider}"]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
            assert result.returncode == expected, (command, result.stdout, result.stderr)
            records = [json.loads(p.read_text()) for p in captures.glob("*.json")]
            assert len(records) == 1, records
            record = records[0]
            assert record["status"] == "WIN_K" and float(record["verification_ms"]) >= 0
            assert record["inputs"] == (["i"] if polarity == "real" else ["o"])
            assert record["outputs"] == (["o"] if polarity == "real" else ["i"])
            assert record["requested_provider"] == provider and record["provider"] == provider
            if provider.startswith("closure"):
                checksum = 14695981039346656037
                for byte in record["worker_boundary"].encode():
                    checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
                assert record["worker_boundary_hash"] == f"fnv1a64:{checksum:016x}"
                assert record["worker_formula"] in record["worker_boundary"]
                assert f"target={target}" in record["worker_target_semantics"]
                assert record["initial_convention"] == "cursor=0,rank=0"
                for metric in ("closure_factory_ms", "closure_normalization_ms",
                               "closure_raw_row_ms", "closure_cursor_row_ms",
                               "closure_branches_considered", "closure_branches_pruned",
                               "closure_guards_generated", "closure_states_discovered",
                               "closure_complete_rows", "closure_raw_rows", "closure_edges",
                               "closure_retained_bytes"):
                    assert float(record[metric]) >= 0, (metric, record)
                closure_pair.append(record)
            else:
                pair.append(record)
        for field in ("worker_formula", "inputs", "outputs", "ap_order", "partition", "k", "status"):
            assert pair[0][field] == pair[1][field], (field, pair)
            assert closure_pair[0][field] == closure_pair[1][field], (field, closure_pair)
        assert pair[0]["worker_formula"] == closure_pair[0]["worker_formula"]
        assert pair[0]["worker_pid"] != pair[1]["worker_pid"]
        assert closure_pair[0]["worker_pid"] != closure_pair[1]["worker_pid"]
        assert int(pair[1]["wrapper_rows_generated"]) <= int(pair[0]["total_wrapper_rows"])
        assert int(closure_pair[1]["wrapper_rows_generated"]) <= int(closure_pair[0]["total_wrapper_rows"])
print("5 native TLSF boundary pairs, TAA and closure: semantics, polarity, exact construction, and certificates agree")

with tempfile.TemporaryDirectory(prefix="acacia-spot-worker-closure-limits-") as directory:
    root = Path(directory)
    spec = root / "limit.tlsf"
    spec.write_text('''INFO { TITLE: "closure limit" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G(i <-> X(o)); } }
''')
    for provider in ("closure-buchi", "closure-buchi-eager"):
        for limit, cap in (("ACACIA_SPOT_MAX_PROVIDER_STATES", "0"),
                           ("ACACIA_SPOT_MAX_ROWS", "1")):
            captures = root / f"{provider}-{limit}"
            env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(captures))
            env[limit] = cap
            command = [binary, "-T", str(spec), "--spot-fast", "off", "-K", "5",
                       "--arms", f"real:small:spot-guarded-sparse:{provider}",
                       "--candidate-mode", "fallback"]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
            assert result.returncode == 2, (command, result.stdout, result.stderr)
            records = [json.loads(p.read_text()) for p in captures.glob("*.json")]
            assert len(records) == 1, records
            record = records[0]
            assert record["worker_boundary_hash"].startswith("fnv1a64:")
            assert "closure-buchi:" in record["reason"] or record["reason"] == "resource_limit"
print("PASS closure factory/row limits: typed UNKNOWN, retained boundary identity")
