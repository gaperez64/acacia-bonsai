"""Test-only ordering for obligation races without scheduler-dependent sleeps."""

import json
import os
import select
import struct
import subprocess
import time
from collections import Counter


def records(directory, complete_only=False):
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines(keepends=True)
            if line.strip() and (not complete_only or line.endswith("\n"))]


def assert_parse_calls(rows, expected):
    calls = [row for row in rows if row.get("event") == "tlsf_parse"]
    assert Counter(row["role"] for row in calls) == expected, calls
    # A fork inherits the observer, but never the parent's call count.
    for pid in {row["pid"] for row in calls}:
        sequence = sorted(row["call"] for row in calls if row["pid"] == pid)
        assert sequence == list(range(1, len(sequence) + 1)), calls


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


class StageGate:
    """Reach a specified helper stage before starting a deadline or interruption."""

    def __init__(self, env, stage, seconds):
        self.stage = stage
        self.seconds = seconds
        self.descriptors = ()
        self.owned = []
        self.ready_pid = None
        self.triggered_at = None
        for name in ("ACACIA_TEST_OBLIGATION_STAGE_READY_FD",
                     "ACACIA_TEST_OBLIGATION_STAGE",
                     "ACACIA_TEST_OBLIGATION_DEADLINE_FD"):
            env.pop(name, None)
        if stage:
            self.ready_read, ready_write = os.pipe()
            self.owned.extend((self.ready_read, ready_write))
            self.descriptors = (ready_write,)
            env["ACACIA_TEST_OBLIGATION_STAGE_READY_FD"] = str(ready_write)
            env["ACACIA_TEST_OBLIGATION_STAGE"] = stage
        if seconds:
            assert stage, "controlled deadlines require a readiness stage"
            trigger_read, self.trigger_write = os.pipe()
            os.set_blocking(trigger_read, False)
            self.owned.extend((trigger_read, self.trigger_write))
            self.descriptors += (trigger_read,)
            env["ACACIA_TEST_OBLIGATION_DEADLINE_FD"] = str(trigger_read)
            # Keep the solver's deadline inactive during startup. Only the preload
            # library's parent clock advances by one day after our trigger expires.
            env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(time.monotonic() + 86400)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        for descriptor in self.owned:
            os.close(descriptor)

    def wait_ready(self, process, directory, timeout=12):
        deadline = time.monotonic() + timeout
        message = b""
        while b"\n" not in message:
            assert process.poll() is None, ("parent exited before readiness", self.stage,
                                           records(directory, complete_only=True))
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, timeout)
            if select.select([self.ready_read], [], [], min(.02, remaining))[0]:
                message += os.read(self.ready_read, 128)
        stage, pid = message.decode().strip().split()
        assert stage == self.stage, (stage, self.stage)
        self.ready_pid = int(pid)
        assert process.poll() is None, ("parent exited before trigger", self.stage)

    def trigger(self):
        assert self.ready_pid is not None, "cancellation must follow stage readiness"
        self.triggered_at = time.monotonic()
        if self.seconds:
            expiry = time.monotonic_ns() + round(self.seconds * 1_000_000_000)
            os.write(self.trigger_write, struct.pack("=Q", expiry))
