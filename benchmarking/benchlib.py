#!/usr/bin/env python3
"""Small shared helpers for Acacia benchmark scripts."""

from __future__ import annotations

import csv
import json
import os
import pathlib
import re
import signal
import subprocess
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Callable


VERDICT_RE = re.compile(r"(?:^|\]\s)(UNREALIZABLE|REALIZABLE)\s*$", re.MULTILINE)


@dataclass(frozen=True)
class RunResult:
    stdout: str
    stderr: str
    returncode: int
    seconds: float
    timed_out: bool
    stdout_bytes: int = 0
    stderr_bytes: int = 0
    resource_limited: bool = False
    memory_peak_bytes: int | None = None


def _terminate_process_group(proc: subprocess.Popen, grace: float = 2.0) -> None:
    """Terminate a still-running process group, escalating after a short grace period."""
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        proc.poll()
        try:
            os.killpg(proc.pid, 0)
        except ProcessLookupError:
            return
        time.sleep(0.05)
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        return
    if proc.poll() is None:
        proc.wait()


def _stop_user_scope(unit: str) -> None:
    """Stop a named user scope and every process in its control group."""
    try:
        subprocess.run(
            ["systemctl", "--user", "stop", f"{unit}.scope"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5,
            check=False,
        )
    except subprocess.TimeoutExpired:
        pass


def filter_stream(stream, predicate: Callable[[str], bool]) -> tuple[list[str], int]:
    """Drain a text stream, retaining selected lines and counting raw size."""
    retained: list[str] = []
    raw_size = 0
    for line in stream:
        raw_size += len(line)
        if predicate(line):
            retained.append(line)
    return retained, raw_size


def run_process_group(
    cmd: list[str],
    timeout: float,
    env: dict[str, str] | None = None,
    capture_filter: Callable[[str], bool] | None = None,
    capture_consumer: Callable[[str], None] | None = None,
) -> RunResult:
    """Run cmd in a new process group and kill the whole group on timeout."""
    started = time.monotonic()
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
        start_new_session=True,
    )
    try:
        timed_out = False
        if capture_filter is None and capture_consumer is None:
            try:
                stdout, stderr = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    stdout, stderr = proc.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    stdout, stderr = proc.communicate()
            stdout_bytes = len(stdout.encode())
            stderr_bytes = len(stderr.encode())
        else:
            retained_lines: list[list[str]] = [[], []]
            raw_sizes = [0, 0]
            consumer_lock = threading.Lock()

            def drain(stream, index: int) -> None:
                if capture_consumer is None:
                    assert capture_filter is not None
                    retained_lines[index], raw_sizes[index] = filter_stream(stream, capture_filter)
                    return
                for line in stream:
                    raw_sizes[index] += len(line)
                    with consumer_lock:
                        capture_consumer(line)

            assert proc.stdout is not None and proc.stderr is not None
            readers = [
                threading.Thread(target=drain, args=(proc.stdout, 0), daemon=True),
                threading.Thread(target=drain, args=(proc.stderr, 1), daemon=True),
            ]
            for reader in readers:
                reader.start()
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    proc.wait()
            for reader in readers:
                reader.join()
            stdout = "".join(retained_lines[0])
            stderr = "".join(retained_lines[1])
            stdout_bytes, stderr_bytes = raw_sizes
        seconds = time.monotonic() - started
        result = RunResult(
            stdout,
            stderr,
            124 if timed_out else proc.returncode,
            seconds,
            timed_out,
            stdout_bytes,
            stderr_bytes,
        )
        return result
    finally:
        # A successful group leader may still have forked descendants.  Probe
        # the process group even after normal completion so the driver never
        # leaves those children behind.
        _terminate_process_group(proc)


def run_systemd_scope(
    cmd: list[str],
    timeout: float,
    memory_max: str,
    memory_swap_max: str = "0",
    env: dict[str, str] | None = None,
    unit_prefix: str = "acacia-bench",
    capture_filter: Callable[[str], bool] | None = None,
    capture_consumer: Callable[[str], None] | None = None,
) -> RunResult:
    """Run cmd in a memory-limited user scope and stop the scope on timeout.

    A process-group timeout alone is insufficient here: systemd migrates the
    solver out of the systemd-run client's process group.  Naming the scope
    lets the timeout path stop the solver and all decomposed children before
    collecting the client's pipes.
    """
    unit = f"{unit_prefix}-{os.getpid()}-{uuid.uuid4().hex[:12]}"
    scoped_cmd = [
        "systemd-run",
        "--user",
        "--scope",
        "--quiet",
        f"--unit={unit}",
        "--property=KillMode=control-group",
        f"--property=MemoryMax={memory_max}",
        f"--property=MemorySwapMax={memory_swap_max}",
        *cmd,
    ]
    started = time.monotonic()
    proc = subprocess.Popen(
        scoped_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
        start_new_session=True,
    )
    previous_handlers: dict[signal.Signals, object] = {}
    if threading.current_thread() is threading.main_thread():
        for handled_signal in (signal.SIGTERM, signal.SIGHUP):
            previous_handlers[handled_signal] = signal.getsignal(handled_signal)

        def cleanup_on_signal(signum, frame) -> None:
            # systemd may signal the benchmark driver while its solver lives in
            # a sibling transient scope.  Clean that scope synchronously before
            # delegating to the caller's handler or terminating the driver.
            for handled_signal in previous_handlers:
                signal.signal(handled_signal, signal.SIG_IGN)
            _stop_user_scope(unit)
            _terminate_process_group(proc)
            previous = previous_handlers[signal.Signals(signum)]
            if callable(previous):
                previous(signum, frame)
            raise SystemExit(128 + signum)

        for handled_signal in previous_handlers:
            signal.signal(handled_signal, cleanup_on_signal)
    try:
        timed_out = False
        if capture_filter is None and capture_consumer is None:
            try:
                stdout, stderr = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                _stop_user_scope(unit)
                if proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                try:
                    stdout, stderr = proc.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    stdout, stderr = proc.communicate()
            stdout_bytes = len(stdout.encode())
            stderr_bytes = len(stderr.encode())
        else:
            # communicate() retains all raw output in RAM.  Drain both pipes as
            # the child runs and keep only diagnostic lines, so even a worker that
            # emits gigabytes of non-diagnostic text has bounded runner memory.
            retained_lines: list[list[str]] = [[], []]
            raw_sizes = [0, 0]
            consumer_lock = threading.Lock()

            def drain(stream, index: int) -> None:
                if capture_consumer is None:
                    assert capture_filter is not None
                    retained_lines[index], raw_sizes[index] = filter_stream(stream, capture_filter)
                    return
                for line in stream:
                    raw_sizes[index] += len(line)
                    with consumer_lock:
                        capture_consumer(line)

            assert proc.stdout is not None and proc.stderr is not None
            readers = [
                threading.Thread(target=drain, args=(proc.stdout, 0), daemon=True),
                threading.Thread(target=drain, args=(proc.stderr, 1), daemon=True),
            ]
            for reader in readers:
                reader.start()
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                _stop_user_scope(unit)
                if proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    proc.wait()
            for reader in readers:
                reader.join()
            stdout = "".join(retained_lines[0])
            stderr = "".join(retained_lines[1])
            stdout_bytes, stderr_bytes = raw_sizes
        finished = time.monotonic()
        resource_limited = False
        memory_peak_bytes = None
        try:
            unit_result = subprocess.run(
                [
                    "systemctl",
                    "--user",
                    "show",
                    f"{unit}.scope",
                    "--property=Result,MemoryPeak",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=5,
                check=False,
            )
            properties = dict(
                line.split("=", 1)
                for line in unit_result.stdout.splitlines()
                if "=" in line
            )
            peak = properties.get("MemoryPeak", "")
            if peak.isdigit():
                memory_peak_bytes = int(peak)
            resource_limited = (
                not timed_out
                and proc.returncode != 0
                and properties.get("Result") == "oom-kill"
            )
        except subprocess.TimeoutExpired:
            pass
        seconds = finished - started
        result = RunResult(
            stdout,
            stderr,
            124 if timed_out else proc.returncode,
            seconds,
            timed_out,
            stdout_bytes,
            stderr_bytes,
            resource_limited,
            memory_peak_bytes,
        )
        return result
    finally:
        # systemd-run can finish after the command's group leader while other
        # processes remain in the scope.  Always stop the named scope, then
        # clean up any descendants still attached to the client's process
        # group.
        _stop_user_scope(unit)
        _terminate_process_group(proc)
        for handled_signal, previous in previous_handlers.items():
            signal.signal(handled_signal, previous)


def verdict_from_output(text: str | None, *, on_conflict: str = "last") -> str | None:
    """Parse line-anchored verdicts so a diagnostic containing the word cannot flip one.

    Accept bare verdicts and utils::vout-prefixed lines. Conflicting verdicts
    return None by default, or raise ValueError when on_conflict is "raise".
    """
    if not text:
        return None
    matches = VERDICT_RE.findall(text)
    verdicts = set(matches)
    if len(verdicts) > 1:
        if on_conflict == "raise":
            raise ValueError(f"conflicting printed verdicts: {sorted(verdicts)}")
        return None
    return matches[-1] if matches else None


def parse_acacia_result(stdout_stderr: str) -> str:
    verdict = verdict_from_output(stdout_stderr)
    if verdict is not None:
        return verdict
    if re.search(r"^\s*TIMEOUT\s*$", stdout_stderr, re.MULTILINE | re.IGNORECASE):
        # Preserve classify_run mapping this to ERROR: TOOL_EXIT_CODES has no TIMEOUT key.
        return "TIMEOUT"
    return "UNKNOWN"


TOOL_EXIT_CODES = {
    "acacia": {"REALIZABLE": 0, "UNREALIZABLE": 1, "UNKNOWN": 2},
    # Acacia v1 reports UNKNOWN as 3, not 2.
    "acacia1x": {"REALIZABLE": 0, "UNREALIZABLE": 1, "UNKNOWN": 3},
    # Verified behaviorally identical to Acacia: ltlsynt previously expressed
    # UNKNOWN=2 as a trailing special case instead of including it in its table.
    "ltlsynt": {"REALIZABLE": 0, "UNREALIZABLE": 1, "UNKNOWN": 2},
}


def classify_run(run: RunResult, tool: str = "acacia") -> str:
    """Classify a bounded tool run, requiring output/exit-code agreement."""
    try:
        expected_exit = TOOL_EXIT_CODES[tool]
    except KeyError:
        raise ValueError(f"unknown tool: {tool!r}") from None
    if run.timed_out:
        return "TIMEOUT"
    if run.resource_limited:
        return "RESOURCE_LIMIT"
    # Join rather than concatenate: a stdout without its trailing newline would
    # otherwise fuse its last line onto the first line of stderr, and the verdict
    # parse is line-anchored.
    result = parse_acacia_result("\n".join((run.stdout, run.stderr)))
    if run.returncode == expected_exit.get(result):
        return result
    return "ERROR"


def classify_acacia_run(run: RunResult) -> str:
    return classify_run(run, "acacia")


def classify_acacia1x_run(run: RunResult) -> str:
    return classify_run(run, "acacia1x")


def classify_ltlsynt_run(run: RunResult) -> str:
    return classify_run(run, "ltlsynt")


def read_part(path: str | pathlib.Path) -> tuple[str, str]:
    """Read a TLSF-style .part file as input/output proposition lists."""
    inputs: list[str] = []
    outputs: list[str] = []
    target: list[str] | None = None
    for raw in pathlib.Path(path).read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        lower = line.lower()
        if lower.startswith(".inputs"):
            target = inputs
            line = line[len(".inputs") :].strip()
        elif lower.startswith(".outputs"):
            target = outputs
            line = line[len(".outputs") :].strip()
        if target is None:
            continue
        target.extend(p for p in line.replace(",", " ").split() if p)
    return ",".join(inputs), ",".join(outputs)


def load_meson_jsonl(path: str | pathlib.Path) -> list[dict]:
    rows: list[dict] = []
    with pathlib.Path(path).open() as handle:
        for line in handle:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def instance_from_meson_name(name: str) -> str:
    """Return the concrete instance part of a Meson benchmark/test name."""
    return name.rsplit(":", 1)[-1]


def realizability_from_output(text: str | None) -> str | None:
    return verdict_from_output(text)


def write_csv(path: str | pathlib.Path, rows: list[dict], fieldnames: list[str]) -> None:
    with pathlib.Path(path).open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
