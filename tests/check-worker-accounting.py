#!/usr/bin/env python3
"""Check setup failures with real solvers and supervisor accounting with fake workers."""

import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile

binary, build, solver, faults = map(Path, sys.argv[1:5])
binary = binary.resolve()
base = ["-f", "G o", "-i", "i", "-o", "o", "--arms", "unreal:formula:backward,real:small:backward"]


def records(directory):
    return [
        json.loads(line)
        for path in directory.glob("*.jsonl")
        for line in path.read_text().splitlines()
    ]


with tempfile.TemporaryDirectory(dir=build) as location:
    root = Path(location)
    for formula, arm, verdict in [("G o", "real:small:backward", b"REALIZABLE"),
                                   ("G i", "unreal:formula:backward", b"UNREALIZABLE")]:
        command = [str(solver.resolve()), "-f", formula, "-i", "i", "-o", "o", "--arms", arm]
        env = dict(os.environ)
        env.pop("ACACIA_PHASE_RECORDS", None)
        baseline = subprocess.run(command, env=env, capture_output=True, timeout=10)
        assert verdict in baseline.stdout, baseline
        for failure in ("prctl", "mmap", "pipe", "fork", "directory"):
            directory = root / (arm + failure)
            if failure != "directory":
                directory.mkdir()
            run = subprocess.run(command, capture_output=True, timeout=10, env=dict(
                env, ACACIA_PHASE_RECORDS=str(directory), LD_PRELOAD=str(faults.resolve()),
                ACACIA_TEST_RECORD_SETUP_FAILURE=failure))
            assert (run.returncode, run.stdout) == (baseline.returncode, baseline.stdout), run
            assert run.stderr.count(b"acacia: phase recording failed") == 1, run.stderr
            assert not any(e.get("worker_pid") for e in records(directory)), failure
        reader, writer = os.pipe()
        os.close(reader)
        try:
            for record_env in (env, dict(env, ACACIA_PHASE_RECORDS=str(root / "missing"))):
                run = subprocess.run(command, env=record_env, stdout=subprocess.PIPE,
                                     stderr=writer, timeout=10)
                assert (run.returncode, run.stdout) == (baseline.returncode, baseline.stdout), run
        finally:
            os.close(writer)
    for modes in [
        "decline,real-delayed",
        "stall,real-delayed",
        "signal,real-delayed",
        "flood-stall,real-delayed",
        "descendant-stall,real-delayed",
    ]:
        directory = root / modes.replace(",", "-")
        directory.mkdir()
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(directory), ACACIA_TEST_CHILD_MODES=modes)
        run = subprocess.run([str(binary), *base], env=env, capture_output=True, timeout=10)
        assert run.returncode == 0, run.stderr
        events = records(directory)
        for pid in {e.get("worker_pid") for e in events}:
            segments = sorted(
                (
                    e
                    for e in events
                    if e.get("event") == "accounting_segment" and e.get("worker_pid") == pid
                ),
                key=lambda e: e["entry_ns"],
            )
            assert all(a["mono_ns"] <= b["entry_ns"] for a, b in zip(segments, segments[1:])), (
                segments
            )
        specs = [e for e in events if e.get("event") == "worker_spec"]
        reaps = [e for e in events if e.get("event") == "worker_reap"]
        assert len(specs) == len(reaps) == 2, events
        assert {e["worker_pid"] for e in specs} == {e["worker_pid"] for e in reaps}
        for e in reaps:
            assert e["user_ns"] >= 0 and e["sys_ns"] >= 0 and e["peak_rss_kb"] > 0, e
            assert e["start_mono_ns"] > 0
        if modes.startswith("decline"):
            boundary = next(e for e in events if e.get("boundary") == "decline")
            assert boundary["process_cpu_ns"] > 0 and boundary["rss_kb"] > 0, boundary
        if "stall" in modes:
            cancelled = next(e for e in events if e.get("boundary") == "winner_cancelled")
            assert cancelled["process_cpu_ns"] is not None and cancelled["rss_kb"] > 0
            censored = next(e for e in events if e.get("event") == "accounting_censored")
            assert censored["phase"] == "fake_unfinished_phase" and censored["cpu_ns"] is None
        if modes.startswith("flood"):
            assert any(e["dropped_records"] > 0 for e in reaps)
        if modes.startswith("descendant"):
            descendant = next(e for e in events if e.get("event") == "descendant_reap")
            assert descendant["descendant_pid"] != descendant["worker_pid"]
            assert descendant["user_ns"] >= 0 and descendant["sys_ns"] >= 0
    directory = root / "interrupted"
    directory.mkdir()
    process = subprocess.Popen(
        [str(binary), *base],
        env=dict(
            os.environ, ACACIA_PHASE_RECORDS=str(directory), ACACIA_TEST_CHILD_MODES="stall,stall"
        ),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )
    # The pipe reports each fake worker at startup; no solver or elapsed gate.
    try:
        seen = 0
        while seen < 2:
            ready, _, _ = select.select([process.stderr], [], [], 5)
            assert ready, "fake workers failed to start"
            if b"child_group" in process.stderr.readline():
                seen += 1
        process.send_signal(signal.SIGTERM)
        process.communicate(timeout=10)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    events = records(directory)
    reaps = [e for e in events if e.get("event") == "worker_reap"]
    assert len(reaps) == 2 and all(e["reason"] == "interrupted" for e in reaps), events
    assert sum(e.get("boundary") == "interrupted" for e in events) == 2, events
print("recorder failure verdict parity and wait4 accounting passed")
