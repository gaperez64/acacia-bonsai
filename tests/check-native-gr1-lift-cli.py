#!/usr/bin/env python3
"""Exercise the native checked pre-pass routes through the real worker."""

from __future__ import annotations

import importlib.util
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

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "attribution_completeness", ROOT / "benchmarking/attribution-completeness.py")
auditor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(auditor)


def check_complete(records: Path, arm: str, code: int) -> None:
    audit = auditor.audit_row({"flags": f"--arms {arm}",
                               "result": {0: "REALIZABLE", 1: "UNREALIZABLE", 2: "UNKNOWN"}[code],
                               "timed_out": "false"}, records)
    assert audit["complete"], audit["issues"]


def run(binary: Path, source: Path, arm: str, records: Path,
        fault: str | None = None, *, extra_args=(), expected_arity=None, target_checks=None
        ) -> tuple[int, str, list[dict]]:
    env = os.environ.copy()
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 60)
    env["ACACIA_PHASE_RECORDS"] = str(records)
    if expected_arity is not None:
        env["ACACIA_NATIVE_TEST_EXPECT_LIFT_ARITY"] = str(expected_arity)
    if target_checks is not None:
        env["ACACIA_NATIVE_TEST_TARGET_CHECKS"] = str(target_checks)
    if fault:
        env["ACACIA_NATIVE_TEST_LIFT_FAULT"] = fault
    else:
        env.pop("ACACIA_NATIVE_TEST_LIFT_FAULT", None)
    process = subprocess.run([str(binary),
                              "-T", str(source), "--arms", arm, *extra_args],
                             capture_output=True, text=True, env=env, timeout=65)
    rows = [json.loads(line) for path in records.glob("*.jsonl")
            for line in path.read_text().splitlines()]
    return process.returncode, process.stdout, rows


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    build = Path(sys.argv[2]).resolve()
    hook_binary = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        root = Path(temporary)
        fixture = (ROOT / "subprojects/tlsf-tools/test/fixtures/"
                   "gr1_monitor_game/arbiter_pb_2_pe_.tlsf")
        rejected = root / "rejected-axis.tlsf"
        rejected.write_text(REAL.replace("extent = 5;", "aaa = 2; extent = 5;"))
        exhausted = root / "exhausted-probes.tlsf"
        exhausted.write_text(REAL.replace("extent = 5;", "".join(
            f"aaa{i:02d} = 3; " for i in range(13)) + "extent = 5;"))
        for index, arm in enumerate(("both:gr1-lift:oxidd", "both:gr1-real-lift:oxidd",
                                     "real:param-lift:oxidd")):
            records = root / f"axis-decline-{index}"
            records.mkdir()
            code, _, rows = run(binary, fixture, arm, records)
            assert code == (2 if arm.startswith("real:") else 0), (arm, code, rows)
            details = [row for row in rows if row.get("event") == "decline_detail"]
            assert any(row["stage"] == "seed_window" and
                       "n=insufficient_sizes" in row["decline_detail"]
                       for row in details), (arm, rows)
            assert all(len(json.dumps(row).encode()) < 1024 for row in details), details
            check_complete(records, arm, code)

            for label, source, expected_code, reason in (
                ("success", rejected, 0, "aaa[1]=parameter_not_indexed"),
                ("resource", exhausted, 2 if arm.startswith("real:") else 0,
                 "aaa00[1]=parameter_not_indexed"),
            ):
                records = root / f"{label}-{index}"
                records.mkdir()
                code, output, rows = run(binary, source, arm, records)
                assert code == expected_code, (arm, label, code, output, rows)
                assert any(reason in row["decline_detail"] for row in rows
                           if row.get("event") == "decline_detail"), (arm, label, rows)
                if label == "success":
                    assert "REALIZABLE" in output, output
                    assert any(row.get("event") == "verification_complete" for row in rows), rows
                    assert any(row.get("event") == "parent_winner" and row["route"] == "R"
                               for row in rows), rows
                else:
                    stops = [row for row in rows if row.get("event") == "route_stopped"
                             and row["reason"] == "resource"]
                    assert stops, rows
                    assert any(reason in row["decline_detail"] and
                               row["stage"] == stop["stage"] and row["route"] == stop["route"]
                               for row in rows if row.get("event") == "decline_detail"
                               for stop in stops), rows
                    assert not any(row.get("event") == "decline" for row in rows), rows
                    if code == 2:
                        assert not any(row.get("event") == "verification_complete"
                                       for row in rows), rows
                    else:
                        # Combined arms retain their direct fallback after a seed resource stop.
                        assert any(row.get("event") == "parent_winner" and
                                   row["route"] == "direct" for row in rows), rows
                check_complete(records, arm, code)

        for value in ("0", "-1", "+4", "1.5", "6x", "", " 6", "2147483648",
                      "4294967295", "999999999999999999999"):
            result = subprocess.run([str(binary), "--lift-max-predicate-arity", value],
                                    capture_output=True, text=True, timeout=5)
            assert result.returncode == 3 and "expects an integer in 1..2147483647" in \
                result.stderr, (value, result)
        missing = subprocess.run([str(binary), "--lift-max-predicate-arity"],
                                 capture_output=True, text=True, timeout=5)
        assert missing.returncode == 3, missing
        help_output = subprocess.run([str(binary), "--help"], capture_output=True,
                                     text=True, check=True, timeout=5).stdout
        assert "--lift-max-predicate-arity N" in help_output and "Raising it is sound" in help_output

        arity_source = root / "arity.tlsf"
        arity_source.write_text(REAL)
        if hook_binary:
            for index, arm in enumerate(("both:gr1-lift:oxidd", "both:gr1-real-lift:oxidd",
                                         "real:param-lift:oxidd")):
                for value in (None, 1, 6, 8, 2147483647):
                    records = root / f"arity-{index}-{value}"
                    records.mkdir()
                    code, output, rows = run(
                        hook_binary, arity_source, arm, records,
                        extra_args=(() if value is None else
                                    ("--lift-max-predicate-arity", str(value))),
                        expected_arity=value or 0,
                        # Reuse these checked proofs to simulate R retries without BDD exhaustion.
                        target_checks=2 if not arm.startswith("real:") else None)
                    assert code == 0 and "REALIZABLE" in output, (arm, value, code, rows)
                    if value is None and not arm.startswith("real:"):
                        assert any(row.get("event") == "parent_winner" and row["route"] == "R"
                                   for row in rows), rows
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

            if hook_binary is not None:
                max_checks = {"real": 2, "unreal": 1, "plain": 3}[name]
                for arm in (("both:gr1-lift:oxidd", real_only_arm) if name == "real"
                            else ("both:gr1-lift:oxidd",)):
                    records = root / f"{name}-{arm}-excess-checks"
                    records.mkdir()
                    result, output, rows = run(
                        hook_binary, path, arm, records, target_checks=max_checks + 1)
                    assert result == 2, (name, result, output, rows)
                    assert not any(row.get("event") == "verification_complete"
                                   for row in rows), rows
                    phases = {row.get("phase"): row for row in rows
                              if row.get("arm") == arm}
                    assert phases["final_checks"]["work_count"] == max_checks + 1, phases
                if name == "real":
                    for fault in ("unverified-region", "source-hash",
                                  "swapped-game-with-matching-hash"):
                        records = root / f"retry-{fault}"
                        records.mkdir()
                        result, _, rows = run(hook_binary, path, real_only_arm, records,
                                              fault, target_checks=2)
                        assert result == 2, (fault, result, rows)
                        assert not any(row.get("event") == "verification_complete"
                                       for row in rows), rows

            if name == "real" and hook_binary is not None:
                decline_records = root / "real-decline-records"
                decline_records.mkdir()
                decline_result, decline_output, decline_rows = run(
                    hook_binary, path, real_only_arm, decline_records, "r-decline",
                    target_checks=3)
                decline_phases = {row.get("phase"): row for row in decline_rows
                                  if row.get("arm") == real_only_arm}
                assert (decline_result, decline_output) == (baseline, baseline_output)
                decline_routes = {row.get("route") for row in decline_rows
                                  if row.get("event") == "route_start"}
                assert "direct" in decline_routes and "U" not in decline_routes, decline_rows
                assert decline_phases["final_checks"]["work_count"] == 3, decline_phases
                assert decline_phases["target_reductions"]["work_count"] == 1, decline_phases


if __name__ == "__main__":
    main()
