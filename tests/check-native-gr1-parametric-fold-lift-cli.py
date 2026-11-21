#!/usr/bin/env python3
"""Require a target-checked parametric fold for the SYNTCOMP load balancer."""

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
    hook = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    source = ROOT / "tests/fixtures/parametric-fold.tlsf"
    arm = "both:gr1-real-lift:oxidd"
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        cases = [(binary, arm, source, None, None, 3)]
        # A second distinguished client shifts the seeds to 4/5/6 and reuses U's cache.
        reused = root / "reused.tlsf"
        reused.write_text(source.read_text().replace("n = 6;", "n = 7;").replace(
            "ASSUMPTIONS {", "ASSUMPTIONS {\n    G F request[1];"))
        cases.append((binary, "both:gr1-lift:oxidd", reused, None, None, 4))
        if hook:
            cases.extend((hook, arm, source, checks, fault, 3)
                         for checks, fault in ((2, None), (3, None),
                                               (None, "unverified-region")))
        for index, (executable, arm, source, checks, fault, solves) in enumerate(cases):
            records = root / str(index)
            records.mkdir()
            # All five cases took 28.79 s at -O0; allow CI contention and sanitizer overhead.
            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(records),
                       ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 120))
            if checks is not None:
                env["ACACIA_NATIVE_TEST_TARGET_CHECKS"] = str(checks)
            if fault:
                env["ACACIA_NATIVE_TEST_LIFT_FAULT"] = fault
            result = subprocess.run([str(executable), "-T", str(source), "--arms", arm],
                                    env=env, capture_output=True, text=True, timeout=130)
            rows = [row for process in load_phase_records(records).processes
                    for row in process.events]
            phases = {row["phase"]: row for row in rows if row.get("arm") == arm}
            assert phases["seed_REAL"]["work_count"] == solves, (result, rows)
            assert phases["seed_checks"]["work_count"] == solves, phases
            assert phases["target_reductions"]["work_count"] == 1, phases
            assert phases["final_checks"]["work_count"] == (checks or 1), phases
            assert any(row.get("event") == "check_complete" and row.get("route") == "R" and
                       row.get("stage") == "target_check" for row in rows), rows
            if source == reused:
                assert phases["seed_cache_hits"]["work_count"] == 2, phases
            rejected = checks == 3 or fault is not None
            assert result.returncode == (2 if rejected else 0), (result, rows)
            terminal, = [row for row in rows if row.get("event") == "terminal_result"]
            assert terminal["route"] == "R", terminal
            if rejected:
                assert terminal["reason"] == "error", terminal
                assert not any(row.get("event") == "parent_winner" for row in rows), rows
            else:
                assert "REALIZABLE" in result.stdout, result
                assert "lift_generator_parametric-support-fold-v1" in phases, phases
                assert terminal["original_polarity"] == terminal["proof_polarity"] == "REAL"
                assert any(row.get("event") == "parent_winner" and row.get("route") == "R"
                           for row in rows), rows


if __name__ == "__main__":
    main()
