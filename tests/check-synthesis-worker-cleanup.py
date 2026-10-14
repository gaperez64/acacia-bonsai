#!/usr/bin/env python3
"""Stopped synthesis workers must die with their parent or before verdict output."""

import ctypes
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time


def stopped(pid):
    return "\nState:\tT" in Path(f"/proc/{pid}/status").read_text()


def registration_race(binary, root):
    with tempfile.TemporaryDirectory(dir=root) as temporary:
        startup_read, startup_write = os.pipe()
        env = os.environ.copy()
        env.pop("ACACIA_PHASE_RECORDS", None)
        env.pop("LD_PRELOAD", None)
        env["ACACIA_TEST_SYNTHESIS_STARTUP_FD"] = str(startup_read)
        process = subprocess.Popen(
            [binary, "-f", "G o", "-i", "", "-o", "o", "--arms", "real:small:backward",
             "-s", str(Path(temporary) / "controller.aag")],
            env=env, pass_fds=(startup_read,), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        os.close(startup_read)
        worker = None
        try:
            assert select.select([process.stderr], [], [], 15)[0], "no startup readiness"
            row = json.loads(process.stderr.readline())
            assert row["stage"] == "synthesis_starting", row
            worker = row["pid"]
            assert not stopped(worker), "parent suspended a worker before registration"
            process.kill()
            assert process.wait(timeout=15) == -signal.SIGKILL
            os.close(startup_write)
            startup_write = None
            deadline = time.monotonic() + 15
            while True:
                pid, status = os.waitpid(worker, os.WNOHANG)
                if pid:
                    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 2, status
                    break
                assert time.monotonic() < deadline, "worker missed parent death during registration"
                select.select([], [], [], .001)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=15)
            if startup_write is not None:
                os.close(startup_write)
            if worker is not None:
                try:
                    os.kill(worker, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    os.waitpid(worker, 0)
                except ChildProcessError:
                    pass


def run(binary, root, *, parent_death, helper_first):
    with tempfile.TemporaryDirectory(dir=root) as temporary:
        gates = [os.pipe() for _ in range(4)]
        env = os.environ.copy()
        # Exercise the shipping SIGPIPE disposition without diagnostic producers.
        env.pop("ACACIA_PHASE_RECORDS", None)
        env.pop("LD_PRELOAD", None)
        env.update(ACACIA_TEST_SYNTHESIS_VERDICT_FD=str(gates[0][0]),
                   ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD=str(gates[1][0]),
                   ACACIA_TEST_SYNTHESIS_EXPORT_FD=str(gates[2][0]),
                   ACACIA_TEST_SYNTHESIS_PUBLICATION_FD=str(gates[3][0]))
        process = subprocess.Popen(
            [binary, "-f", "G(o <-> i)", "-i", "i", "-o", "o",
             "--arms", "real:small:backward,both:obligation:spot", "--spot-fast", "off",
             "-s", str(Path(temporary) / "controller.aag")],
            env=env, pass_fds=tuple(read for read, _ in gates),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for read, _ in gates:
            os.close(read)
        workers = []
        rows = []
        pending = b""

        def stage(name, count):
            nonlocal pending
            deadline = time.monotonic() + 15
            while len([row for row in rows if row.get("stage") == name]) < count:
                remaining = deadline - time.monotonic()
                assert remaining > 0 and select.select([process.stderr], [], [], remaining)[0], rows
                chunk = os.read(process.stderr.fileno(), 4096)
                assert chunk, (rows, process.poll())
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    if line.startswith(b"{"):
                        rows.append(json.loads(line))
            return [row for row in rows if row.get("stage") == name]

        def release(index):
            read, write = gates[index]
            os.close(write)
            gates[index] = (read, -1)

        try:
            workers = [row["pid"] for row in stage("synthesis_ready", 2)]
            release(1 if helper_first else 0)
            selected, = stage("synthesis_selected", 1)
            winner = selected["pid"]
            loser, = [pid for pid in workers if pid != winner]
            deadline = time.monotonic() + 15
            while not stopped(loser):
                assert time.monotonic() < deadline, ("competitor did not stop", loser)
                select.select([], [], [], .001)
            # No timing margin: the selected exporter is blocked and the other
            # worker has actually entered SIGSTOP before triggering either exit.
            if parent_death:
                process.kill()
                assert process.wait(timeout=15) == -signal.SIGKILL
                for pid in workers:
                    deadline = time.monotonic() + 15
                    while True:
                        reaped, status = os.waitpid(pid, os.WNOHANG)
                        if reaped:
                            assert os.WIFSIGNALED(status) and os.WTERMSIG(status) == signal.SIGKILL
                            break
                        assert time.monotonic() < deadline, ("orphan survived parent death", pid)
                        select.select([], [], [], .001)
            else:
                release(2)
                publishing, = stage("synthesis_publishing", 1)
                assert publishing["pid"] == process.pid and stopped(loser), publishing
                process.stdout.close()
                release(3)
                assert process.wait(timeout=15) == -signal.SIGPIPE
                for pid in workers:
                    # The solver must reap competitors before the fatal output;
                    # merely relying on their parent-death signal leaves adoptees.
                    try:
                        reaped, _ = os.waitpid(pid, os.WNOHANG)
                    except ChildProcessError:
                        pass
                    else:
                        raise AssertionError(("worker was not reaped before verdict output", pid,
                                              reaped))
            for pid in workers:
                assert not Path(f"/proc/{pid}").exists(), ("worker survived", pid)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=15)
            for pid in workers:
                try:
                    os.killpg(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            for _, write in gates:
                if write >= 0:
                    os.close(write)
            # The subreaper cleans up failed regressions as well as passing ones.
            deadline = time.monotonic() + 15
            while True:
                try:
                    pid, _ = os.waitpid(-1, os.WNOHANG)
                except ChildProcessError:
                    break
                if not pid:
                    assert time.monotonic() < deadline, "test descendants did not exit"
                    select.select([], [], [], .001)


def main():
    libc = ctypes.CDLL(None, use_errno=True)
    # Adopt and reap workers if the regression kills their parent.
    assert libc.prctl(36, 1, 0, 0, 0) == 0, ctypes.get_errno()  # PR_SET_CHILD_SUBREAPER
    binary = str(Path(sys.argv[1]).resolve())
    for parent_death in (False, True):
        for helper_first in (False, True):
            run(binary, sys.argv[2], parent_death=parent_death, helper_first=helper_first)
    registration_race(binary, sys.argv[2])
    print("diagnostics-off stdout, stopped parent-death cleanup, and registration race passed")


if __name__ == "__main__":
    main()
