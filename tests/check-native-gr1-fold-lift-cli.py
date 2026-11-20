#!/usr/bin/env python3
"""Require a checked fold-lift answer for a small binary-encoded arbiter."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_phase_records  # noqa: E402


def main() -> None:
    binary, build = (Path(arg).resolve() for arg in sys.argv[1:3])
    arm = "both:gr1-real-lift:oxidd"
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        records = Path(temporary)
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(records),
                   ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 20))
        result = subprocess.run(
            [str(binary), "-T", str(ROOT / "tests/fixtures/encoded-fold.tlsf"), "--arms", arm],
            env=env, capture_output=True, text=True, timeout=25)
        rows = [row for process in load_phase_records(records).processes
                for row in process.events]
        assert result.returncode == 0 and "REALIZABLE" in result.stdout, (result, rows)
        assert any(row.get("event") == "accounting_end" and
                   row.get("phase") == "native_fold_seed_check" for row in rows), rows
        phases = {row["phase"]: row for row in rows if row.get("arm") == arm}
        assert phases["seed_REAL"]["work_count"] > 0, phases
        assert phases["final_checks"]["work_count"] == 1, phases
        assert phases["target_reductions"]["work_count"] == 1, phases
        assert any(row.get("event") == "check_complete" and row.get("route") == "R" and
                   row.get("stage") == "target_check" for row in rows), rows
        assert any(row.get("event") == "terminal_result" and row.get("route") == "R" and
                   row.get("original_polarity") == row.get("proof_polarity") == "REAL" and
                   row.get("exit_code") == 0 for row in rows), rows


if __name__ == "__main__":
    main()
