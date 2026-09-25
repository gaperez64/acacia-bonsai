#!/usr/bin/env python3
"""Exercise portfolio group cleanup and the absolute deadline with fake children."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
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
    children: set[int] = set()
    child_list = Path(f"/proc/{process.pid}/task/{process.pid}/children")
    while len(children) < expected_children and process.poll() is None:
        try:
            children.update(int(pid) for pid in child_list.read_text().split())
        except FileNotFoundError:
            break
        time.sleep(0.002)
    for pid in children:
        for _ in range(30):
            try:
                group = os.getpgid(pid)
            except ProcessLookupError:
                break
            if group == pid:
                break
            time.sleep(0.001)
        else:
            raise AssertionError(("child lacks its own process group", pid, group))
    stdout, stderr = process.communicate(timeout=3)
    elapsed = time.monotonic() - started
    if len(children) < expected_children:
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
            check(binary, single, "stall", 0.4, 1, 2, "UNKNOWN", 1.2)
            check(binary, pair, "stall,stall", 0.4, 2, 2, "UNKNOWN", 1.2)
            check(binary, single, "real-late", 0.4, 1, 2, "UNKNOWN", 1.2)
            check(binary, pair, "stall,real-delayed", 2.0, 2, 0,
                  "REALIZABLE", 1.0)
            check(binary, single, "real", 0.3, 1, 2, "UNKNOWN", 1.2,
                  delay_after_reap=True)
    print("portfolio deadline and group cleanup checks passed")


if __name__ == "__main__":
    main()
