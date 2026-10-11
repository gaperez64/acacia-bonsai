#!/usr/bin/env python3
"""Exercise process admission, stop/continue, cleanup, and unchanged proof paths."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from importlib.util import module_from_spec, spec_from_file_location

spec = spec_from_file_location("portfolio_deadline", Path(__file__).with_name(
    "check-portfolio-deadline.py"))
deadline = module_from_spec(spec)
spec.loader.exec_module(deadline)


def main() -> None:
    fake, solver, build = (Path(arg).resolve() for arg in sys.argv[1:4])
    source_args = ["-f", "G o", "-i", "i", "-o", "o"]
    pair = [*source_args, "--arms", "real:small:backward,unreal:formula:backward",
            "--stage-concurrency", "1"]
    single = [*source_args, "--arms", "real:small:backward", "--stage-concurrency", "1"]
    deadline.check(fake, single, "memory-stall", 0.4, 1, 2, "UNKNOWN", 1.2)
    deadline.check(fake, pair, "memory-stall,memory-stall", 0.4, 2, 2, "UNKNOWN", 1.2)
    deadline.check(fake, pair, "memory-stall,memory-real-delayed", 6, 2, 0,
                   "REALIZABLE", 5)
    deadline.check(fake, pair, "memory-unknown,memory-real-delayed", 3, 2, 0,
                   "REALIZABLE", 1.5)
    deadline.check(fake, pair, "memory-decline-delayed,memory-real-delayed", 3, 2, 0,
                   "REALIZABLE", 1.5)
    deadline.check(fake, pair, "memory-stall,checking-real-delayed", 3, 2, 0,
                   "REALIZABLE", 1.5)
    deadline.check(fake, pair, "checking-decline-delayed,memory-real-delayed", 3, 2, 0,
                   "REALIZABLE", 1.5)

    deadline.check(fake, single, "memory-real-late", 0.4, 1, 2, "UNKNOWN", 1.2)
    with tempfile.TemporaryDirectory(dir=build) as directory:
        location = Path(directory)
        phases = location / "phases"
        phases.mkdir()
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases),
                   ACACIA_TEST_CHILD_MODES="memory-stall,memory-stall",
                   ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 5))
        process = subprocess.Popen([str(fake), *pair], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, start_new_session=True)
        # Interrupt a portfolio containing an admitted group and a stopped group.
        end = time.monotonic() + 2
        paused = None
        while time.monotonic() < end and paused is None:
            for path in phases.glob("*.jsonl"):
                for line in path.read_text().splitlines():
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if event.get("event") == "scheduler_pause":
                        paused = event["worker_pid"]
            time.sleep(0.005)
        assert paused is not None
        process.terminate()
        stdout, stderr = process.communicate(timeout=2)
        assert process.returncode == 2 and b"UNKNOWN" in stdout + stderr
        assert deadline.gone(paused)

        priority_phases = location / "priority"
        priority_phases.mkdir()
        env.update(ACACIA_PHASE_RECORDS=str(priority_phases),
                   ACACIA_TEST_CHILD_MODES="memory-stall,checking-real-delayed",
                   ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 3))
        result = subprocess.run([str(fake), *pair], env=env, capture_output=True, timeout=4)
        assert result.returncode == 0, result
        events = [json.loads(line) for path in priority_phases.glob("*.jsonl")
                  for line in path.read_text().splitlines()]
        pauses = [e["mono_ns"] for e in events if e.get("event") == "scheduler_pause"
                  and e.get("worker") == 0]
        checking = [e["mono_ns"] for e in events if e.get("event") == "scheduler_admit"
                    and e.get("kind") == 2]
        assert pauses and checking and min(pauses) <= min(checking)

        env.update(ACACIA_PHASE_RECORDS=str(location / "missing-destination"),
                   ACACIA_TEST_CHILD_MODES="memory-stall,memory-real-delayed",
                   ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 6))
        result = subprocess.run([str(fake), *pair], env=env, capture_output=True, timeout=7)
        assert result.returncode == 0, result # failed telemetry cannot block admission

        # The default and explicit off paths have identical output and controller bytes.
        artifacts = []
        for index, option in enumerate(([], ["--stage-concurrency", "off"],
                                       ["--stage-concurrency", "1"])):
            artifact = location / f"controller{index}.aag"
            result = subprocess.run([str(solver), *source_args, "--arms", "real:small:backward",
                                     "-s", str(artifact), *option], capture_output=True, timeout=10)
            assert result.returncode == 0 and b"REALIZABLE" in result.stdout, result
            artifacts.append((result.stdout, result.stderr, artifact.read_bytes()))
        assert artifacts[0] == artifacts[1] == artifacts[2]
        for formula, expected in (("G o", 0), ("G i", 1), ("G (i <-> o)", 0)):
            for count in ("off", "1", "2"):
                result = subprocess.run([str(solver), "-f", formula, "-i", "i", "-o", "o",
                                         "--stage-concurrency", count], capture_output=True,
                                        timeout=10)
                assert result.returncode == expected, result
        for guarantee, expected in (("G o", 0), ("G i", 1)):
            source = location / "control.tlsf"
            source.write_text('INFO { TITLE: "control" SEMANTICS: Mealy TARGET: Mealy }\n'
                              'MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { '
                              + guarantee + '; } }\n')
            for count in ("off", "1", "2"):
                result = subprocess.run([str(solver), "-T", str(source), "--stage-concurrency",
                                         count], capture_output=True, timeout=10)
                assert result.returncode == expected, result
        for value in ("0", "-1", "on", "1x", "4294967296"):
            result = subprocess.run([str(solver), *source_args, "--stage-concurrency", value],
                                    capture_output=True, timeout=5)
            assert result.returncode == 3 and b"off or a positive count" in result.stderr
    print("stage scheduling fairness, release, priority, cleanup and proof checks passed")


if __name__ == "__main__":
    main()
