#!/usr/bin/env python3
"""Check allowances with/without a deadline, cumulative handoff, and incumbent defaults."""

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

REFERENCE_NS = 5_000_000_000


def execute(binary: Path, args: list[str], directory: Path | None,
            deadline: bool) -> subprocess.CompletedProcess:
    # The shared attribution helper always supplies a deadline; exercise shipping's
    # genuinely absent environment variable here, including with diagnostics on.
    env = os.environ.copy()
    for name in ("ACACIA_PHASE_RECORDS", "ACACIA_TEST_ATTRIBUTION_PAUSE",
                 "ACACIA_OUTER_DEADLINE_MONOTONIC"):
        env.pop(name, None)
    if deadline:
        env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 20)
    if directory is not None:
        env["ACACIA_PHASE_RECORDS"] = str(directory)
    return subprocess.run([str(binary), *args], env=env, capture_output=True, timeout=20)


def check_budget(budget: dict, limit: dict, deadline: bool,
                 fraction: float | None, absolute: int | None) -> None:
    assert budget["fraction"] == fraction, budget
    assert bool(budget["outer_deadline_ns"]) == deadline, budget
    expected_mode = "absolute" if absolute is not None else "remaining" if deadline else "reference"
    assert limit["mode"] == expected_mode, limit
    assert limit["reference_ns"] == (REFERENCE_NS if fraction is not None and not deadline else 0)
    assert limit["absolute_ns"] == (absolute or 0), limit
    if fraction is not None:
        if not deadline:
            assert budget["initial_remaining_ns"] == REFERENCE_NS, budget
        assert budget["total_ns"] == int(budget["initial_remaining_ns"] * fraction), budget
    else:
        assert budget["total_ns"] == absolute, budget
    remaining = budget["total_ns"] - budget["consumed_ns"]
    assert budget["allowance_ns"] == (min(remaining, budget["remaining_ns"])
                                      if deadline else remaining), budget
    assert budget["deadline_ns"] == budget["mono_ns"] + budget["allowance_ns"], budget
    if deadline:
        assert budget["deadline_ns"] <= budget["outer_deadline_ns"], budget


def main() -> None:
    binary, build = (Path(value).resolve() for value in sys.argv[1:3])
    incumbent = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    args = ["-f", "G (i <-> X o)", "-i", "i", "-o", "o", "--spot-fast", "off",
            "--arms", "real:small:backward"]
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        for deadline in (False, True):
            regime = root / ("deadline" if deadline else "no-deadline")
            regime.mkdir()
            route_histories = {}
            defaults = []
            for label, value, fraction, absolute in (
                ("default", None, None, None),
                ("unbounded", "unbounded", None, None),
                ("quarter", "0.25", 0.25, None),
                ("zero", "0", 0.0, None),
                ("decimal-zero", "+0.0e-400", 0.0, None),
                ("fraction-tiny", "2e-10", 2e-10, None),
                ("seconds", "3s", None, 3_000_000_000),
                ("milliseconds", "3000ms", None, 3_000_000_000),
                ("decimal-seconds", "0.003s", None, 3_000_000),
                ("absolute-zero", "0ms", None, 0),
                ("absolute-zero-seconds", "0s", None, 0),
                ("absolute-tiny", "0.000001ms", None, 1),
                ("seconds-tiny", "1e-9s", None, 1),
                ("seconds-truncated", "1.9e-9s", None, 1),
                ("reset-unbounded", "unbounded", None, None),
                ("seconds-over-fraction", "3s", None, 3_000_000_000),
                ("fraction-over-seconds", "0.25", 0.25, None),
            ):
                directory = regime / label
                directory.mkdir()
                switches = [] if value is None else ["--equivariance-budget", value]
                if label in ("reset-unbounded", "fraction-over-seconds"):
                    switches = ["--equivariance-budget", "0ms", *switches]
                elif label == "seconds-over-fraction":
                    switches = ["--equivariance-budget", "0", *switches]
                result = execute(binary, args + switches, directory, deadline)
                plain = execute(binary, args + switches, None, deadline)
                assert result.returncode == plain.returncode == 0, (result, plain)
                assert result.stdout == plain.stdout and result.stderr == plain.stderr
                records = attribution.rows(directory)
                attribution.complete_records(records)
                route_histories[label] = [(r["route"], r["effective_backend"])
                                          for r in records if r.get("event") == "route_start"]
                budget, = [r for r in records if r.get("phase") == "equivariance_budget"]
                limits = [r for r in records if r.get("phase") == "equivariance_limit"]
                if fraction is not None or absolute is not None:
                    limit, = limits
                    check_budget(budget, limit, deadline, fraction, absolute)
                    assert budget["consumed_ns"] == 0, budget
                else:
                    assert not limits and budget["deadline_ns"] == 0, records
                    defaults.append((plain.returncode, plain.stdout, plain.stderr))
                if label in ("zero", "decimal-zero", "fraction-tiny", "absolute-zero",
                             "absolute-zero-seconds", "absolute-tiny", "seconds-tiny",
                             "seconds-truncated"):
                    stopped, = [r for r in records if r.get("event") == "route_stopped"]
                    assert stopped["reason"] == "equivariance_budget", stopped
                    assert stopped["route"] == "equivariance", stopped
                    assert not any(r.get("event") == "decline" for r in records), records
                    backward, = [r for r in records if r.get("event") == "route_start"
                                 and r.get("route") == "backward"]
                    assert stopped["worker_pid"] == backward["worker_pid"]
                    assert stopped["mono_ns"] <= backward["mono_ns"]
                    if deadline:
                        assert backward["mono_ns"] < budget["outer_deadline_ns"]
                    terminal, = [r for r in records if r.get("event") == "terminal_result"]
                    assert terminal["exit_code"] == 0 and terminal["reason"] == "none", terminal
            assert defaults == [defaults[0]] * len(defaults), defaults
            if incumbent is not None:
                original = execute(incumbent, args, None, deadline)
                assert defaults[0] == (original.returncode, original.stdout, original.stderr)
            assert route_histories["default"] == route_histories["unbounded"]
            assert route_histories["default"] == route_histories["reset-unbounded"]
            for value, fraction, absolute in (("0.25", 0.25, None), ("3000ms", None, 3_000_000_000),
                                              ("0ms", None, 0)):
                directory = regime / f"decomposed-{value}"
                directory.mkdir()
                decomposed = ["-f", "G (i <-> X o) & G (j <-> X p)", "-i", "i,j", "-o", "o,p",
                              "--spot-fast", "off", "--arms", "real:small:backward",
                              "--equivariance-budget", value]
                result = execute(binary, decomposed, directory, deadline)
                assert result.returncode == 0, result
                records = attribution.rows(directory)
                attribution.complete_records(records)
                budgets = [r for r in records if r.get("phase") == "equivariance_budget"]
                limits = [r for r in records if r.get("phase") == "equivariance_limit"]
                assert len(budgets) == len(limits) >= 2, records
                assert len({r["total_ns"] for r in budgets}) == 1, budgets
                assert len({r["outer_deadline_ns"] for r in budgets}) == 1, budgets
                assert budgets[0]["consumed_ns"] == 0, budgets
                for budget, limit in zip(budgets, limits):
                    check_budget(budget, limit, deadline, fraction, absolute)
                for previous, current in zip(budgets, budgets[1:]):
                    assert current["consumed_ns"] >= previous["consumed_ns"], budgets
                if absolute == 0:
                    assert len([r for r in records if r.get("event") == "route_stopped"]) >= 2
                else:
                    assert budgets[-1]["consumed_ns"] > 0, budgets
            for label, extra in (
                ("forward", ["--arms", "real:small:forward"]),
                ("synthesis", ["--arms", "real:small:backward", "-s", str(regime / "out.aag")]),
                ("off", ["--arms", "real:small:backward", "--equivariance", "off"]),
            ):
                directory = regime / label
                directory.mkdir()
                result = execute(binary, args[:-2] + extra + ["--equivariance-budget", "0ms"],
                                 directory, deadline)
                assert result.returncode == 0, result
                assert not any(r.get("route") == "equivariance"
                               for r in attribution.rows(directory))
            for value in ("invalid", "nan", "inf", "-0.1", "1", "1.1", "0.25junk", "",
                          "s", "ms", "-1s", "nans", "infms", "+NaN", "-nan", "Infinitys",
                          " +nanms", "3m", "3sjunk", "1e100s"):
                result = execute(binary, args + ["--equivariance-budget", value], None, deadline)
                assert result.returncode == 3 and b"expects 0 <= F < 1" in result.stderr, result
            for values, message in (
                (("-0", "-0.0", "-0e10", "-0s", "-0ms", "-0.0s", " -0.000ms", "-0x0p0s"),
                 b"negative zero"),
                (("1e-400", "1e-5000", "1e-320", "1e-10", "1.999999999e-10",
                  "0.0000000001s", "0.0000001ms", "0.999999999e-9s", "0.999999999e-6ms",
                  "1e-400s", "1e-400ms", "1e-5000s", "1e-5000ms"),
                 b"at least 1 ns"),
            ):
                for value in values:
                    directory = regime / f"rejected-{value.strip()}"
                    directory.mkdir()
                    result = execute(binary, args + ["--equivariance-budget", value],
                                     directory, deadline)
                    plain = execute(binary, args + ["--equivariance-budget", value],
                                    None, deadline)
                    assert result.returncode == plain.returncode == 3, (
                        value, deadline, result, plain)
                    assert result.stdout == plain.stdout and result.stderr == plain.stderr
                    assert b"--equivariance-budget" in result.stderr, (value, result)
                    assert message in result.stderr, (value, result)
                    assert result.stdout == b"", (value, result)
                    records = attribution.rows(directory)
                    assert not any(r.get("event") in ("worker_start", "route_start")
                                   or r.get("phase") == "equivariance_budget"
                                   for r in records), records
        # A shorter supplied deadline clips an absolute allowance; it cannot reset the deadline.
        directory = root / "outer-clips-absolute"
        directory.mkdir()
        result = execute(binary, args + ["--equivariance-budget", "30s"], directory, True)
        assert result.returncode == 0, result
        budget, = [r for r in attribution.rows(directory) if r.get("phase") == "equivariance_budget"]
        assert budget["total_ns"] == 30_000_000_000
        assert budget["deadline_ns"] == budget["outer_deadline_ns"], budget
    print("Equivariance allowances with/without deadline and same-worker backward handoff: PASS")


if __name__ == "__main__":
    main()
