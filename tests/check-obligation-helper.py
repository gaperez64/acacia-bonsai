#!/usr/bin/env python3
"""Check original-game decisions, routing, failure boundaries and worker cleanup."""
from __future__ import annotations

import argparse
import json
import os
import resource
from pathlib import Path
import signal
import subprocess
import tempfile
import time


def records(directory: Path) -> list[dict]:
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines() if line.strip()]


def gone(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


def run(binary: Path, args: list[str], root: Path, fault_lib: Path,
        fault: str = "", delay_ms: int = 0, interrupt: bool = False,
        outer_seconds: float = 0, rss_limit: bool = False) -> tuple[subprocess.CompletedProcess, list[dict]]:
    with tempfile.TemporaryDirectory(dir=root) as location:
        directory = Path(location)
        env = os.environ.copy()
        env["ACACIA_PHASE_RECORDS"] = str(directory)
        env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
        env.pop("ACACIA_TEST_CHILD_MODES", None)
        env.pop("ACACIA_TEST_DELAY_AFTER_REAP", None)
        if fault or delay_ms:
            env["LD_PRELOAD"] = str(fault_lib)
            env["ACACIA_TEST_OBLIGATION_FAULT"] = fault
            env["ACACIA_TEST_OBLIGATION_LEGACY_DELAY_MS"] = str(delay_ms)
        if outer_seconds:
            env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + outer_seconds)
        process = subprocess.Popen([str(binary), *args], env=env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   preexec_fn=(lambda: resource.setrlimit(
                                       resource.RLIMIT_AS, (256 * 1024 * 1024, 256 * 1024 * 1024)))
                                   if rss_limit else None)
        if interrupt:
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if any(row.get("event") == "obligation_start" for row in records(directory)):
                    break
                if process.poll() is not None:
                    raise AssertionError("parent exited before interruption")
                time.sleep(0.005)
            else:
                process.kill()
                process.communicate()
                raise AssertionError("helper failed to start")
            process.send_signal(signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=12)
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGTERM)
            process.communicate(timeout=3)
            raise
        rows = records(directory)
        completed = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        for row in rows:
            if row.get("event") != "worker_spawn":
                continue
            pid = row["worker_pid"]
            assert gone(pid), ("surviving child", row, completed)
            try:
                os.killpg(pid, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError(("surviving process group", pid))
        return completed, rows


def helper_rows(rows: list[dict], event: str) -> list[dict]:
    return [row for row in rows if row.get("event") == "obligation_" + event]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("unit", type=Path)
    parser.add_argument("fault_lib", type=Path)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--ltlsynt", type=Path)
    parser.add_argument("--tlsf", action="store_true")
    parser.add_argument("--native", action="store_true")
    options = parser.parse_args()
    binary, library = options.binary.resolve(), options.fault_lib.resolve()
    root = options.build_dir.resolve()

    unit = subprocess.run([str(options.unit.resolve())], capture_output=True, text=True, check=True)
    if options.ltlsynt:
        for line in unit.stdout.splitlines():
            if "\t" not in line:
                continue
            timing, formula, expected = line.split("\t")
            oracle = subprocess.run(
                [str(options.ltlsynt), "--obligation-synthesis=no", "--bypass=no", "--realizability",
                 "--ins=i,unused_input", "--outs=o,unused_output", "-f", formula,
                 *(["--semantics=Moore"] if timing == "moore" else [])],
                capture_output=True, text=True, timeout=10)
            assert oracle.returncode == (0 if expected == "completed_REAL" else 1), oracle

    source = ["-f", "G (i <-> o)", "-i", "i", "-o", "o"]
    pair = ["--arms", "real:small:backward,unreal:formula:backward"]
    real = ["--arms", "real:small:backward"]
    for mode in ([], ["--obligation-helper", "off"]):
        result, rows = run(binary, [*source, *pair, *mode], root, library)
        assert result.returncode == 0 and "REALIZABLE" in result.stdout, result
        assert not any(row.get("event", "").startswith("obligation_") for row in rows)
        assert len([row for row in rows if row.get("event") == "worker_spawn"]) == 2

    on = ["--obligation-helper", "on"]
    for formula, expected in [("G (i <-> o)", 0), ("G (X i <-> o)", 1)]:
        args = ["-f", formula, "-i", "i", "-o", "o", *pair]
        exact, _ = run(binary, args, root, library)
        candidate, rows = run(binary, [*args, *on], root, library, delay_ms=300)
        assert exact.returncode == candidate.returncode == expected, (exact, candidate)
        winners = [row for row in rows if row.get("event") == "parent_winner"]
        assert len(winners) == 1 and winners[0]["effective_backend"] == "obligation", rows
        finish = helper_rows(rows, "completion")
        assert len(finish) == 1 and finish[0]["outcome"] == (
            "completed_REAL" if expected == 0 else "completed_UNREAL"), rows
        assert finish[0]["same_library_checked"] == (expected == 0)
        helper_specs = [row for row in rows if row.get("kind") == "obligation-helper"]
        assert len(helper_specs) == 1, rows
        helper_index = helper_specs[0]["worker"]
        assert helper_index == 2, rows  # existing arms start first

    for formula, arms, expected in [
            ("G (i <-> o)", "unreal:formula:backward", 0),
            ("G (X i <-> o)", "real:small:backward", 1)]:
        result, rows = run(binary, ["-f", formula, "-i", "i", "-o", "o",
                                   "--arms", arms, *on], root, library, delay_ms=300)
        assert result.returncode == expected, result
        assert [row for row in rows if row.get("event") == "parent_winner"][0][
            "effective_backend"] == "obligation", rows

    ineligible, rows = run(binary, ["-f", "G F (i <-> o)", "-i", "i", "-o", "o",
                                   *pair, *on], root, library)
    assert ineligible.returncode == 0, ineligible
    assert helper_rows(rows, "eligibility")[0]["outcome"] == "inapplicable", rows
    assert not helper_rows(rows, "start"), rows
    assert len([row for row in rows if row.get("event") == "worker_spawn"]) == 2
    # Eligibility uses the post-simplification original objective: GF i becomes false.
    simplified, rows = run(binary, ["-f", "G F i", "-i", "i", "-o", "o", *pair, *on],
                           root, library)
    assert simplified.returncode == 1, simplified
    assert helper_rows(rows, "eligibility")[0]["reason"] == "eligible", rows

    for fault, outcome in [("null", "error"), ("empty", "error"), ("exception", "error"),
                           ("allocation", "exhausted"), ("signal", "error"),
                           ("unreal-exit", "error"), ("real-exit", "error")]:
        result, rows = run(binary, [*source, *real, *on], root, library, fault, 300)
        assert result.returncode == 0, result
        finish = helper_rows(rows, "completion")
        assert len(finish) == 1 and finish[0]["outcome"] == outcome, (fault, rows)
        assert all(row.get("effective_backend") != "obligation"
                   for row in rows if row.get("event") == "parent_winner"), rows

    result, rows = run(binary, [*source, *real, *on], root, library, "stall", 300)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert len(finish) == 1 and finish[0]["outcome"] == "cancelled", rows
    assert finish[0]["reason"] == "winner_cancelled", rows
    result, rows = run(binary, [*source, *real, *on], root, library, "stall", 7000)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert len(finish) == 1 and finish[0]["outcome"] == "exhausted", rows
    assert finish[0]["reason"] == "wall_allowance", rows
    result, rows = run(binary, [*source, *real, *on], root, library, "memory", 300,
                       rss_limit=True)
    assert result.returncode == 0, result
    finish = helper_rows(rows, "completion")
    assert len(finish) == 1 and finish[0]["outcome"] == "exhausted", rows
    assert finish[0]["reason"] == "rss_share", rows
    result, rows = run(binary, [*source, *real, *on], root, library, "stall", 7000,
                       outer_seconds=0.2)
    assert result.returncode == 2, result
    assert helper_rows(rows, "completion")[0]["outcome"] == "cancelled", rows
    result, rows = run(binary, [*source, *real, *on], root, library, "stall", 7000, interrupt=True)
    assert result.returncode == 2, result
    assert helper_rows(rows, "completion")[0]["outcome"] == "cancelled", rows

    with tempfile.TemporaryDirectory(dir=root) as directory:
        controller = Path(directory) / "controller.aag"
        result, rows = run(binary, [*source, *real, *on, "-s", str(controller)],
                           root, library, "exception")
        assert result.returncode == 0 and controller.read_text().startswith("aag"), result
        assert helper_rows(rows, "eligibility")[0]["reason"] == "synthesis_requested"
        assert not helper_rows(rows, "start"), rows
        assert len([row for row in rows if row.get("event") == "worker_spawn"]) == 1
        if options.tlsf:
            for semantics, expected in [("Mealy", 0), ("Moore", 1), ("Strict,Mealy", 0),
                                        ("Strict,Moore", 1)]:
                tlsf = Path(directory) / "source.tlsf"
                tlsf.write_text(f'''INFO {{ TITLE: "generated" SEMANTICS: {semantics} TARGET: Moore }}
MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }} GUARANTEES {{ G (i <-> o); }} }}
''')
                exact, _ = run(binary, ["-T", str(tlsf), *pair], root, library)
                result, rows = run(binary, ["-T", str(tlsf), *pair, *on], root, library, delay_ms=300)
                assert exact.returncode == result.returncode == expected, (exact, result)
                assert helper_rows(rows, "completion")[0]["outcome"] == (
                    "completed_REAL" if expected == 0 else "completed_UNREAL"), rows
            # Safety assumptions exercise the strict normalized objective too,
            # rather than only using a strict label on an assumption-free game.
            for semantics in ("Mealy", "Moore", "Strict,Mealy", "Strict,Moore"):
                tlsf.write_text(f'''INFO {{ TITLE: "generated" SEMANTICS: {semantics} TARGET: Mealy }}
MAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }}
       ASSUMPTIONS {{ G i; }} GUARANTEES {{ G (i <-> o); }} }}
''')
                exact, _ = run(binary, ["-T", str(tlsf), *pair], root, library)
                result, rows = run(binary, ["-T", str(tlsf), *pair, *on], root, library, delay_ms=300)
                assert exact.returncode in (0, 1) and result.returncode == exact.returncode, (
                    exact, result)
                finish = helper_rows(rows, "completion")
                assert len(finish) == 1 and finish[0]["outcome"] == (
                    "completed_REAL" if exact.returncode == 0 else "completed_UNREAL"), rows
            if options.native:
                tlsf.write_text('''INFO { TITLE: "generated" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G (i <-> o); } }
''')
                result, rows = run(binary, ["-T", str(tlsf), "--arms", "both:gr1:oxidd", *on],
                                   root, library)
                assert result.returncode == 0, result
                assert helper_rows(rows, "eligibility")[0]["reason"] == "eligible", rows

    invalid = subprocess.run([str(binary), *source, "--obligation-helper", "maybe"],
                             capture_output=True, text=True)
    assert invalid.returncode == 3 and "expects off or on" in invalid.stderr, invalid
    print("obligation routing, oracle, faults, synthesis and cleanup checks passed")


if __name__ == "__main__":
    main()
