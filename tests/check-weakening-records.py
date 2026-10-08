#!/usr/bin/env python3
"""Exercise the basic loop and wire records with generated ASTs and test-only runners."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def load_reader():
    path = Path(__file__).resolve().parents[1] / "benchmarking/weakening-census.py"
    spec = importlib.util.spec_from_file_location("weakening_census", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    binary = str(Path(sys.argv[1]).resolve())
    reader = load_reader()
    with tempfile.TemporaryDirectory(dir=sys.argv[2]) as temporary:
        root = Path(temporary)
        modes = ["success", "inconclusive", "exception", "cancellation", "decline", "mixed",
                 "not_top_level_and", "no_global_conjunction",
                 "global_conjunction_threshold_not_met",
                 "absent_safety_conjuncts",
                 "no_non_safety_obligations", "not_unreal_worker", "synthesis_requested"]
        for mode in modes:
            for small in (False, True):
                directory = root / f"{mode}-{small}"
                directory.mkdir()
                env = os.environ.copy()
                env["ACACIA_PHASE_RECORDS"] = str(directory)
                if small:
                    env["ACACIA_TEST_RECORD_PIPE_512"] = "1"
                actual = subprocess.run([binary, mode], env=env, capture_output=True, timeout=5)
                off = env.copy()
                off.pop("ACACIA_PHASE_RECORDS")
                prior = subprocess.run([binary, mode], env=off, capture_output=True, timeout=5)
                assert (actual.returncode, actual.stdout, actual.stderr) == (
                    prior.returncode, prior.stdout, prior.stderr), (mode, actual, prior)
                records, problems = reader.read_records(directory)
                assert not problems
                assert all(r.get("dropped_records", 0) == 0 for r in records), (mode, records)
                summary, = reader.census(records)
                assert summary["census"] == "complete", (mode, summary, records)
                assert summary["mode"] == summary["effective_mode"] == "basic"
                eligible = mode in modes[:6]
                assert summary["eligible"] == eligible, (mode, summary)
                assert summary["generated"] == (8 if eligible else 0)
                assert summary["started"] == (1 if mode in {"success", "exception"} else
                                              8 if eligible else 0)
                assert summary["successes"] == (1 if mode == "success" else 0)
                assert summary["full_solver_started"] == (mode not in {"success", "exception"})
                if summary["full_solver_started"]:
                    assert 0 < summary["full_solver_remaining_ns"] < 10_000_000_000
                ends = [r for r in records if r.get("event") == "weakening_attempt_end"]
                outcome = {"success": "proof", "cancellation": "cancellation",
                           "decline": "decline", "exception": "exception"}.get(mode, "inconclusive")
                if mode == "mixed":
                    assert [r["outcome"] for r in ends] == ["inconclusive", "decline"] + [
                        "inconclusive"] * 6, ends
                    terminal = next(r for r in records if r.get("event") == "terminal_result")
                    assert terminal["reason"] == "none", terminal
                else:
                    assert all(r["outcome"] == outcome for r in ends), (mode, ends)
                assert all(r["wall_ns"] >= 0 and r["cpu_ns"] >= 0 and r["peak_rss_kb"] > 0
                           for r in ends)
                assert all(r["run_id"] > 0 for r in ends)
                alphabet = sorted((r for r in records if r.get("event") == "weakening_alphabet"),
                                  key=lambda r: r["chunk"])
                content = bytes.fromhex("".join(r["data"] for r in alphabet)).decode()
                assert "unused_input" in content and "unused_output" in content
                if not eligible:
                    decision = next(r for r in records
                                    if r.get("event") == "weakening_eligibility")
                    assert decision["reason"] == mode
                    if mode in {"no_global_conjunction", "global_conjunction_threshold_not_met"}:
                        assert decision["oversized_globals"] == 0
                        assert decision["largest_global_conjunction_size"] == (
                            0 if mode == "no_global_conjunction" else 64)
                        assert (summary["largest_global_conjunction_size"] ==
                                decision["largest_global_conjunction_size"])
        directory = root / "kill"
        directory.mkdir()
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(directory))
        process = subprocess.Popen([binary, "kill"], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert process.stdout.readline() == "candidate\n"
        stdout, stderr = process.communicate("x", timeout=5)
        assert process.returncode == 0 and not stdout and not stderr
        records, problems = reader.read_records(directory)
        assert not problems
        summary, = reader.census(records)
        assert summary["census"] == "complete", (summary, records)
        assert summary["started"] == 1 and summary["completed"] == 0
        assert summary["cancellations"] == 1 and summary["attempt_cpu_ns"] is None
        assert summary["full_solver_started"] is False
        directory = root / "flood"
        directory.mkdir()
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                   ACACIA_TEST_RECORD_WRITER_DELAY="1")
        process = subprocess.Popen([binary, "flood"], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        marker, delivered = process.stdout.readline().split()
        assert marker == "flooded" and 0 < int(delivered) < 20000
        # A created file can still have a full pipe behind it. Drain every
        # accepted flood packet before releasing the real pre-pass.
        end = time.monotonic() + 2
        while True:
            records, problems = reader.read_records(directory)
            if not problems and sum(r.get("event") == "flood" for r in records) == int(delivered):
                break
            assert time.monotonic() < end, (delivered, problems, records)
            time.sleep(.005)
        stdout, stderr = process.communicate("x", timeout=5)
        assert process.returncode == 0 and stdout == "fallback\n" and not stderr
        records, problems = reader.read_records(directory)
        assert not problems and any(r.get("dropped_records", 0) for r in records)
        summary, = reader.census(records)
        assert summary["census"] == "incomplete" and summary["started"] is None
        assert summary["cancellations"] is None and summary["observed_cancellations"] == 0
        if len(sys.argv) > 3:
            solver = str(Path(sys.argv[3]).resolve())
            # The helper selects a compiled formula arm from the generated build config.
            arm = subprocess.check_output([binary, "--solver-arm"], text=True, timeout=5).strip()
            predictive = ["F(o & " + "X " * j + "!o)" for j in range(1, 65)]
            responsive = ["(i -> F(o0 & " + "X " * j + "!o0))" for j in range(1, 65)]
            cases = [
                ("predictive", "G((o <-> X i) & " + " & ".join(predictive) +
                 ") & F(!o & X o)", "o,unused_o", True),
                ("responsive", "G((!o0 | !o1) & " + " & ".join(responsive) +
                 ") & G(!i -> F o1)", "o0,o1,unused_o", False),
            ]
            for name, formula, outputs, proof in cases:
                directory = root / name
                directory.mkdir()
                command = [solver, "-f", formula, "-i", "i,unused_i", "-o", outputs,
                           "--arms", arm, "-K", "2"]
                results = []
                executables = [(solver, True), (solver, False)]
                if len(sys.argv) > 4:
                    executables.append((str(Path(sys.argv[4]).resolve()), False))
                for executable, diagnostics in executables:
                    env = os.environ.copy()
                    env.pop("ACACIA_PHASE_RECORDS", None)
                    if diagnostics:
                        env["ACACIA_PHASE_RECORDS"] = str(directory)
                    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 2)
                    result = subprocess.run([executable, *command[1:]], env=env,
                                            capture_output=True, timeout=4)
                    results.append((result.returncode, result.stdout, result.stderr))
                assert all(r == results[0] for r in results), (name, results)
                assert results[0][0] in (1, 2), (name, arm, results)
                records, problems = reader.read_records(directory)
                assert not problems
                summary, = reader.census(records)
                assert summary["census"] == "complete", (name, summary, records)
                assert summary["mode"] == summary["effective_mode"] == "basic"
                assert summary["eligible"] and summary["generated"] == 8
                assert summary["started"] == (1 if proof else 8)
                assert summary["successes"] == int(proof)
                assert summary["full_solver_started"] is not proof
                if not proof:
                    assert 0 < summary["full_solver_remaining_ns"] < 2_000_000_000
                else:
                    assert results[0][0] == 1 and results[0][1] == b"UNREALIZABLE\n"
    print("weakening lifecycle, reasons, binding, exceptions, cancellation and drops passed")



if __name__ == "__main__":
    main()
