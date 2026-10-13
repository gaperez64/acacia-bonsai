"""Test-only ordering for obligation races without scheduler-dependent sleeps."""

import json
import os
import subprocess
import time


def records(directory, complete_only=False):
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines(keepends=True)
            if line.strip() and (not complete_only or line.endswith("\n"))]


class LegacyGate:
    def __init__(self, env, enabled):
        self.descriptors = ()
        self.release_fd = None
        for name in ("ACACIA_TEST_OBLIGATION_LEGACY_GATE_FD",
                     "ACACIA_TEST_OBLIGATION_STALL_READY_FD"):
            env.pop(name, None)
        if enabled:
            legacy_read, self.release_fd = os.pipe()
            self.ready_read, ready_write = os.pipe()
            os.set_blocking(self.ready_read, False)
            self.descriptors = (legacy_read, ready_write)
            env["ACACIA_TEST_OBLIGATION_LEGACY_GATE_FD"] = str(legacy_read)
            env["ACACIA_TEST_OBLIGATION_STALL_READY_FD"] = str(ready_write)

    def __enter__(self):
        return self

    def release(self):
        if self.release_fd is not None:
            os.close(self.release_fd)
            self.release_fd = None

    def __exit__(self, *args):
        self.release()
        for descriptor in self.descriptors:
            os.close(descriptor)
        if self.descriptors:
            os.close(self.ready_read)

    def communicate(self, process, directory, timeout, release_stalls=True):
        if not self.descriptors:
            return process.communicate(timeout=timeout)
        deadline = time.monotonic() + timeout
        stall_ready = False
        while True:
            # A successful helper keeps ordinary workers gated until the parent
            # accepts it and cancels them. Declines and actual injected stalls
            # release the existing fallback, whose winner is still asserted.
            # Wait for reaping before releasing a decline: completion is emitted
            # before the child publishes its shared result and exits.
            # Deadline/interruption tests hold stalls until the parent stops them.
            if self.release_fd is not None:
                try:
                    stall_ready |= bool(os.read(self.ready_read, 1))
                except BlockingIOError:
                    pass
                rows = records(directory, complete_only=True)
                declined = any(
                    row.get("event") == "obligation_completion"
                    and row.get("outcome") not in {"completed_REAL", "completed_UNREAL"}
                    and any(terminal.get("event") == "parent_terminal"
                            and terminal.get("worker_pid") == row["worker_pid"]
                            for terminal in rows)
                    or row.get("event") == "obligation_eligibility"
                    and row.get("outcome") == "inapplicable"
                    for row in rows)
                if stall_ready and release_stalls or declined:
                    self.release()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, timeout)
            try:
                return process.communicate(timeout=min(0.02, remaining))
            except subprocess.TimeoutExpired:
                pass
