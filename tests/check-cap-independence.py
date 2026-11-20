#!/usr/bin/env python3
"""Compare real lift-arm records at two caps, including durable final records (#443)."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_phase_records, load_tool, phase_record_dir  # noqa: E402

capcheck = load_tool("benchmarking/tools/check-cap-independence.py")
auditor = load_tool("benchmarking/attribution-completeness.py")
ARM = "both:gr1-real-lift:oxidd"


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = ROOT / "tests/fixtures/native_gr1/exhausted-probes.tlsf"
    with tempfile.TemporaryDirectory(dir=sys.argv[2]) as temporary:
        root = Path(temporary)
        rows = []
        for cap in (17, 60):
            row = {"instance": fixture.stem, "solver_label": "lift", "cap_s": str(cap),
                   "run_index": "0", "flags": f"--arms {ARM}", "result": "REALIZABLE",
                   "exit_code": "0", "timed_out": "false", "resource_reason": ""}
            directory = phase_record_dir(root, "lift", cap, fixture.stem, "0")
            directory.mkdir(parents=True)
            start = time.monotonic()
            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                       ACACIA_OUTER_DEADLINE_MONOTONIC=str(start + cap))
            result = subprocess.run([str(binary), "-T", str(fixture), "--arms", ARM],
                                    env=env, capture_output=True, text=True, timeout=20)
            row["seconds"] = str(time.monotonic() - start)
            assert result.returncode == 0 and "REALIZABLE" in result.stdout, result
            audit = auditor.audit_row(row, directory)
            assert audit["complete"], audit["issues"]
            records = load_phase_records(directory)
            assert any(p.name.endswith("-final.jsonl") and p.events
                       for p in records.processes), records
            rows.append({fixture.stem: row})
        checked, failures, notes = capcheck.compare(*rows, root, root, arms=[ARM])
        assert checked == 1 and not failures and not notes, (
            checked, [failure["message"] for failure in failures], notes)
    print("lift cap comparison with durable records passed at 17 s and 60 s")


if __name__ == "__main__":
    main()
