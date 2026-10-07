#!/usr/bin/env python3
"""Check the runtime budget, same-worker handoff, exclusions, and incumbent default."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location(
    "attribution", Path(__file__).with_name("check-attribution.py"))
attribution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(attribution)


def main() -> None:
    binary, build = (Path(value).resolve() for value in sys.argv[1:3])
    args = ["-f", "G (i <-> X o)", "-i", "i", "-o", "o", "--spot-fast", "off",
            "--arms", "real:small:backward"]
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        old_deadline = os.environ.get("ACACIA_OUTER_DEADLINE_MONOTONIC")
        try:
            route_histories = {}
            for label, switches, bounded in (
                ("default", [], False),
                ("unbounded", ["--equivariance-budget", "unbounded"], False),
                ("quarter", ["--equivariance-budget", "0.25"], True),
                ("zero", ["--equivariance-budget", "0"], True),
            ):
                directory = root / label
                directory.mkdir()
                os.environ["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 20)
                result = attribution.execute(binary, args + switches, directory)
                plain = attribution.execute(binary, args + switches, None)
                assert result.returncode == plain.returncode == 0, (result, plain)
                assert result.stdout == plain.stdout
                records = attribution.rows(directory)
                attribution.complete_records(records)
                route_histories[label] = [(r["route"], r["effective_backend"])
                                          for r in records if r.get("event") == "route_start"]
                budget, = [r for r in records if r.get("phase") == "equivariance_budget"]
                assert (budget["fraction"] is not None) == bounded, budget
                if bounded:
                    assert budget["total_ns"] == int(
                        budget["initial_remaining_ns"] * budget["fraction"]), budget
                    assert budget["consumed_ns"] == 0, budget
                    assert budget["allowance_ns"] == min(
                        budget["total_ns"], budget["remaining_ns"]), budget
                    assert budget["deadline_ns"] == budget["mono_ns"] + budget["allowance_ns"]
                    assert budget["deadline_ns"] <= budget["outer_deadline_ns"]
                if label == "zero":
                    stopped, = [r for r in records if r.get("event") == "route_stopped"]
                    assert stopped["reason"] == "equivariance_budget", stopped
                    assert stopped["route"] == "equivariance", stopped
                    assert not any(r.get("event") == "decline" for r in records), records
                    backward, = [r for r in records if r.get("event") == "route_start"
                                 and r.get("route") == "backward"]
                    assert stopped["worker_pid"] == backward["worker_pid"]
                    assert stopped["mono_ns"] <= backward["mono_ns"] < budget["outer_deadline_ns"]
                    terminal, = [r for r in records if r.get("event") == "terminal_result"]
                    assert terminal["exit_code"] == 0 and terminal["reason"] == "none", terminal
            assert route_histories["default"] == route_histories["unbounded"]
            directory = root / "decomposed"
            directory.mkdir()
            decomposed = ["-f", "G (i <-> X o) & G (j <-> X p)", "-i", "i,j", "-o", "o,p",
                          "--spot-fast", "off", "--arms", "real:small:backward",
                          "--equivariance-budget", "0.25"]
            result = attribution.execute(binary, decomposed, directory)
            assert result.returncode == 0, result
            records = attribution.rows(directory)
            attribution.complete_records(records)
            budgets = [r for r in records if r.get("phase") == "equivariance_budget"]
            assert len(budgets) >= 2, records
            assert len({r["total_ns"] for r in budgets}) == 1, budgets
            assert len({r["outer_deadline_ns"] for r in budgets}) == 1, budgets
            assert budgets[0]["consumed_ns"] == 0, budgets
            for previous, current in zip(budgets, budgets[1:]):
                assert current["consumed_ns"] >= previous["consumed_ns"], budgets
                assert current["allowance_ns"] == min(
                    current["total_ns"] - current["consumed_ns"], current["remaining_ns"]), budgets
                assert current["deadline_ns"] == current["mono_ns"] + current["allowance_ns"]
            for label, extra in (
                ("forward", ["--arms", "real:small:forward"]),
                ("synthesis", ["--arms", "real:small:backward", "-s", str(root / "out.aag")]),
                ("off", ["--arms", "real:small:backward", "--equivariance", "off"]),
            ):
                directory = root / label
                directory.mkdir()
                os.environ["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 20)
                result = attribution.execute(
                    binary, args[:-2] + extra + ["--equivariance-budget", "0"], directory)
                assert result.returncode == 0, result
                assert not any(r.get("route") == "equivariance"
                               for r in attribution.rows(directory))
            for value in ("invalid", "nan", "inf", "-0.1", "1", "1.1", "0.25junk", ""):
                result = attribution.execute(binary, args + ["--equivariance-budget", value], None)
                assert result.returncode == 3 and "expects 0 <= F < 1" in result.stderr, result
            os.environ.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
            result = subprocess.run([str(binary), *args, "--equivariance-budget", "0.25"],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 3 and "requires ACACIA_OUTER_DEADLINE" in result.stderr
            # Unbounded remains usable without an invocation deadline.
            assert subprocess.run([str(binary), *args], capture_output=True).returncode == 0
        finally:
            if old_deadline is None:
                os.environ.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
            else:
                os.environ["ACACIA_OUTER_DEADLINE_MONOTONIC"] = old_deadline
    print("Equivariance budget and same-worker backward handoff: PASS")


if __name__ == "__main__":
    main()
