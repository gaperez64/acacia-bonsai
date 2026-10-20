#!/usr/bin/env python3
"""Exercise portfolio group cleanup and the absolute deadline with fake children."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import resource
import signal
import sys
import tempfile
import time


REAL = '''INFO { TITLE: "real" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G o; } }
'''


def gone(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


def check(binary: Path, args: list[str], modes: str, seconds: float,
          expected_children: int, expected_code: int, expected_text: str,
          max_elapsed: float, delay_after_reap: bool = False) -> None:
    env = os.environ.copy()
    env["ACACIA_TEST_CHILD_MODES"] = modes
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + seconds)
    env.pop("ACACIA_TEST_DELAY_AFTER_REAP", None)
    if delay_after_reap:
        env["ACACIA_TEST_DELAY_AFTER_REAP"] = "1"
    started = time.monotonic()
    process = subprocess.Popen([str(binary), *args], env=env, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               start_new_session=True)
    stdout, stderr = process.communicate(timeout=3)
    children: set[int] = set()
    reports = []
    for line in stderr.splitlines():
        if not line.startswith('{"stage":"child_group",'):
            continue
        report = json.loads(line)
        reports.append(report)
        pid, group = report["pid"], report["pgid"]
        if pid <= 0 or group != pid:
            raise AssertionError(("child lacks its own process group", pid, group))
        children.add(pid)
    elapsed = time.monotonic() - started
    if len(children) != expected_children or len(reports) != expected_children:
        raise AssertionError(("missing fake children", modes, children, stdout, stderr))
    if (process.returncode != expected_code or expected_text not in stdout + stderr
            or elapsed >= max_elapsed):
        raise AssertionError((modes, process.returncode, elapsed, stdout, stderr))
    if delay_after_reap and '"before_deadline":true' not in stderr:
        raise AssertionError(("child was not reaped before expiry", stderr))
    for pid in children:
        if not gone(pid):
            raise AssertionError(("unreaped child", pid, modes))
        try:
            os.killpg(pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(("surviving child group", pid, modes))


def check_native_abort_race(binary: Path, source: Path, directory: Path) -> None:
    """Hold a real competitor until the parent has reaped the OxiDD failure."""
    records = directory / "abort-records"
    records.mkdir()
    gate_read, gate_write = os.pipe()
    env = dict(os.environ, OXIDD_STACK_SIZE=str(1024 * 1024 * 1024),
               ACACIA_TEST_CHILD_MODES=f"wait:{gate_read},", ACACIA_PHASE_RECORDS=str(records))
    env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)

    def limit():
        bound = 512 * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (bound, bound))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    process = subprocess.Popen(
        [str(binary), "-T", str(source), "--arms", "real:small:backward,both:gr1:oxidd"],
        env=env, preexec_fn=limit, pass_fds=(gate_read,), start_new_session=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    os.close(gate_read)
    try:
        deadline = time.monotonic() + 15
        while True:
            failures = []
            for path in records.glob("*.jsonl"):
                for line in path.read_text().splitlines():
                    try:
                        row = json.loads(line)
                    except json.JSONDecodeError:
                        continue  # The writer may still be appending this row.
                    if (row.get("event") == "parent_terminal" and
                            row.get("requested_backend") == "oxidd"):
                        failures.append(row)
            if failures:
                assert failures[0]["exit_code"] == 2 or failures[0]["signal"] == signal.SIGABRT
                break
            assert process.poll() is None, process.communicate()
            assert time.monotonic() < deadline, "parent did not reap native failure"
            time.sleep(.01)
        # The parent remains alive with no verdict until a healthy arm solves.
        assert process.poll() is None
        os.write(gate_write, b"1")
        stdout, stderr = process.communicate(timeout=15)
        assert process.returncode == 0 and stdout.strip() == "REALIZABLE", (stdout, stderr)
        assert "panicked" in stderr, stderr
    finally:
        os.close(gate_write)
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
    print("OxiDD constructor abort reaped as native failure; healthy sibling REALIZABLE")


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    build_dir = Path(sys.argv[2]).resolve()
    native = "--native" in sys.argv[3:]
    with tempfile.TemporaryDirectory(dir=build_dir) as location:
        source = Path(location) / "real.tlsf"
        source.write_text(REAL)
        for kind in ("legacy", "native") if native else ("legacy",):
            if kind == "native":
                single = ["-T", str(source), "--arms", "real:gr1:oxidd"]
                pair = ["-T", str(source), "--arms",
                        "unreal:gr1:oxidd,real:gr1:oxidd"]
            else:
                source_args = ["-f", "G o", "-i", "i", "-o", "o"]
                single = [*source_args, "--arms", "real:small:backward"]
                pair = [*source_args, "--arms",
                        "unreal:formula:backward,real:small:backward"]
            # Signal deaths have no exit verdict: WEXITSTATUS alone would
            # misread SIGKILL as REALIZABLE. Cover both native and legacy arms.
            check(binary, single, "signal", 2.0, 1, 2, "UNKNOWN", 1.2)
            check(binary, pair, "signal,real-delayed", 2.0, 2, 0, "REALIZABLE", 1.0)
            check(binary, single, "stall", 0.4, 1, 2, "UNKNOWN", 1.2)
            check(binary, pair, "stall,stall", 0.4, 2, 2, "UNKNOWN", 1.2)
            check(binary, single, "real-late", 0.4, 1, 2, "UNKNOWN", 1.2)
            check(binary, pair, "stall,real-delayed", 2.0, 2, 0,
                  "REALIZABLE", 1.0)
            check(binary, single, "real", 0.3, 1, 2, "UNKNOWN", 1.2,
                  delay_after_reap=True)
            if kind == "native":
                both_single = ["-T", str(source), "--arms", "both:gr1:oxidd"]
                both_pair = ["-T", str(source), "--arms",
                            "unreal:gr1:oxidd,both:gr1:oxidd"]
                check(binary, both_single, "stall", 0.4, 1, 2, "UNKNOWN", 1.2)
                check(binary, both_pair, "stall,real-delayed", 2.0, 2, 0,
                      "REALIZABLE", 1.0)
        if native and sys.platform == "linux":
            if os.environ.get("ACACIA_TEST_SKIP_RLIMIT_AS") == "1":
                print("Skipping RLIMIT_AS case: incompatible with ASan shadow memory")
            else:
                check_native_abort_race(binary, source, Path(location))
    print("portfolio deadline and group cleanup checks passed")


if __name__ == "__main__":
    main()
