#!/usr/bin/env python3
"""Repeat native GR(1) solves and checks across OxiDD manager lifetimes."""

from __future__ import annotations

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
CORPUS_REAL = (ROOT / "tests/syntcomp-benchmarks/tlsf/round_robin_arbiter" /
               "parametric/round_robin_arbiter.tlsf")
FIXTURE_REAL = ROOT / "tests/fixtures/native_gr1/round_robin_arbiter.tlsf"
UNREAL = '''INFO { TITLE: "unreal" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G i; } }
'''


def repeat(binary: Path, source: Path, arm: str, count: int,
           expected_code: int, expected_verdict: str) -> None:
    for iteration in range(1, count + 1):
        env = os.environ.copy()
        env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 30)
        process = subprocess.Popen(
            [str(binary), "--arms", arm, "-T", str(source)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env=env, start_new_session=True,
        )
        try:
            stdout, stderr = process.communicate(timeout=35)
        except subprocess.TimeoutExpired as error:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            raise AssertionError(f"{arm} run {iteration}/{count} timed out") from error

        # Acacia only publishes a verdict from a normally exited child. It
        # reports child signals separately when no other arm can win.
        if (process.returncode != expected_code or
                stdout.splitlines() != [expected_verdict] or
                '"stage":"child_signal"' in stderr):
            raise AssertionError(
                f"{arm} run {iteration}/{count}: exit={process.returncode}, "
                f"stdout={stdout!r}, stderr={stderr!r}"
            )
        if iteration % 25 == 0:
            print(f"{arm}: {iteration}/{count}", flush=True)


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    build_dir = Path(sys.argv[2]).resolve()
    source = CORPUS_REAL if CORPUS_REAL.is_file() else FIXTURE_REAL
    if not source.is_file():
        raise AssertionError("round-robin TLSF fixture is missing")
    with tempfile.TemporaryDirectory(dir=build_dir) as location:
        unreal = Path(location) / "unreal.tlsf"
        unreal.write_text(UNREAL)
        repeat(binary, source, "real:gr1:oxidd", 200, 0, "REALIZABLE")
        repeat(binary, unreal, "unreal:gr1:oxidd", 100, 1, "UNREALIZABLE")


if __name__ == "__main__":
    main()
