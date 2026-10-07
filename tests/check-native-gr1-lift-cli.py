#!/usr/bin/env python3
"""Exercise the native checked pre-pass routes through the real worker."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


REAL = '''INFO { TITLE: "repeated liveness" SEMANTICS: Mealy TARGET: Mealy }
GLOBAL { PARAMETERS { extent = 5; } }
MAIN { INPUTS { demand[extent]; } OUTPUTS { response[extent]; }
GUARANTEES { &&[0 <= i < extent] G F response[i]; } }
'''
UNREAL = '''INFO { TITLE: "anchored recurrence" SEMANTICS: Mealy TARGET: Mealy }
GLOBAL { PARAMETERS { width = 7; } }
MAIN { INPUTS { req; } OUTPUTS { grant[0..width-1]; }
ASSUME { G F req; }
GUARANTEE { G F grant[0]; &&[0 <= i < width] G (!grant[i]); } }
'''
PLAIN = '''INFO { TITLE: "plain" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEES { G F b; } }
'''


def run(binary: Path, source: Path, arm: str, records: Path,
        fault: str | None = None, typed: str | None = None) -> tuple[int, str, list[dict]]:
    env = os.environ.copy()
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 60)
    env["ACACIA_PHASE_RECORDS"] = str(records)
    if fault:
        env["ACACIA_NATIVE_TEST_LIFT_FAULT"] = fault
    else:
        env.pop("ACACIA_NATIVE_TEST_LIFT_FAULT", None)
    command = [str(binary), "-T", str(source), "--arms", arm]
    if typed is not None:
        command.extend(["--r-typed-roles", typed])
    process = subprocess.run(command,
                             capture_output=True, text=True, env=env, timeout=65)
    rows = [json.loads(line) for path in records.glob("*.jsonl")
            for line in path.read_text().splitlines()]
    return process.returncode, process.stdout, rows


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    build = Path(sys.argv[2]).resolve()
    hook_binary = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    invalid = subprocess.run([str(binary), "--r-typed-roles", "maybe"],
                             capture_output=True, text=True)
    assert invalid.returncode == 3 and "expects on or off" in invalid.stderr, invalid
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        for name, source, verdict, route, polarity, code in (
            ("real", REAL, "REALIZABLE", "R", "seed_REAL", 0),
            ("unreal", UNREAL, "UNREALIZABLE", "U", "seed_UNREAL", 1),
            ("plain", PLAIN, "REALIZABLE", "direct", "seed_none", 0),
        ):
            path = root / f"{name}.tlsf"
            path.write_text(source)
            records = root / f"{name}-records"
            records.mkdir()
            result, output, rows = run(binary, path, "both:gr1-lift:oxidd", records)
            assert result == code and verdict in output, (name, result, output, rows)
            phases = {row.get("phase"): row for row in rows
                      if row.get("arm") == "both:gr1-lift:oxidd"}
            routes = {row.get("route") for row in rows if row.get("event") == "route_start"}
            assert route in routes and polarity in phases, (name, rows)
            assert phases["final_checks"]["work_count"] == 1, (name, phases)
            assert phases["target_reductions"]["work_count"] == 1, (name, phases)
            direct_records = root / f"{name}-direct-records"
            direct_records.mkdir()
            baseline, baseline_output, _ = run(binary, path, "both:gr1:oxidd",
                                               direct_records)
            assert (baseline, baseline_output) == (result, output), name

            real_only_records = root / f"{name}-real-only-records"
            real_only_records.mkdir()
            real_only_arm = "both:gr1-real-lift:oxidd"
            only_result, only_output, only_rows = run(
                binary, path, real_only_arm, real_only_records)
            only_phases = {row.get("phase"): row for row in only_rows
                           if row.get("arm") == real_only_arm}
            assert (only_result, only_output) == (baseline, baseline_output), name
            only_routes = {row.get("route") for row in only_rows
                           if row.get("event") == "route_start"}
            assert ("R" if name == "real" else "direct") in only_routes, only_rows
            assert "U" not in only_routes, only_rows
            if name == "unreal":
                assert "seed_UNREAL" in only_phases, only_phases
            assert only_phases["final_checks"]["work_count"] == 1, only_phases
            assert only_phases["target_reductions"]["work_count"] == 1, only_phases

            for setting in ("off", "on"):
                typed_records = root / f"{name}-typed-{setting}"
                typed_records.mkdir()
                typed_result, typed_output, typed_rows = run(
                    binary, path, real_only_arm, typed_records, typed=setting)
                assert (typed_result, typed_output) == (baseline, baseline_output)
                typed_phases = {row.get("phase"): row for row in typed_rows
                                if row.get("arm") == real_only_arm}
                assert f"r_typed_roles_{setting}" in typed_phases, typed_rows
                assert typed_phases["final_checks"]["work_count"] == 1, typed_rows
                assert typed_phases["target_reductions"]["work_count"] == 1, typed_rows
                if name == "real":
                    assert any(row.get("event") == "route_start" and row.get("route") == "R"
                               for row in typed_rows), typed_rows

            if name == "real" and hook_binary is not None:
                decline_records = root / "real-decline-records"
                decline_records.mkdir()
                decline_result, decline_output, decline_rows = run(
                    hook_binary, path, real_only_arm, decline_records, "r-decline")
                decline_phases = {row.get("phase"): row for row in decline_rows
                                  if row.get("arm") == real_only_arm}
                assert (decline_result, decline_output) == (baseline, baseline_output)
                decline_routes = {row.get("route") for row in decline_rows
                                  if row.get("event") == "route_start"}
                assert "direct" in decline_routes and "U" not in decline_routes, decline_rows
                assert decline_phases["final_checks"]["work_count"] == 1, decline_phases
                assert decline_phases["target_reductions"]["work_count"] == 1, decline_phases


if __name__ == "__main__":
    main()
