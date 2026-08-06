#!/usr/bin/env python3
"""Small shared helpers for Acacia benchmark scripts."""

from __future__ import annotations

import csv
import json
import os
import pathlib
import signal
import subprocess
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Callable


@dataclass(frozen=True)
class RunResult:
    stdout: str
    stderr: str
    returncode: int
    seconds: float
    timed_out: bool
    stdout_bytes: int = 0
    stderr_bytes: int = 0


def filter_stream(stream, predicate: Callable[[str], bool]) -> tuple[list[str], int]:
    """Drain a text stream, retaining selected lines and counting raw size."""
    retained: list[str] = []
    raw_size = 0
    for line in stream:
        raw_size += len(line)
        if predicate(line):
            retained.append(line)
    return retained, raw_size


def run_process_group(cmd: list[str], timeout: float, env: dict[str, str] | None = None) -> RunResult:
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
    timed_out = False
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            stdout, stderr = proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            stdout, stderr = proc.communicate()
    seconds = time.monotonic() - started
    return RunResult(stdout, stderr, 124 if timed_out else proc.returncode, seconds, timed_out)


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
    timed_out = False
    if capture_filter is None and capture_consumer is None:
        try:
            stdout, stderr = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(
                ["systemctl", "--user", "stop", f"{unit}.scope"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5,
                check=False,
            )
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
            try:
                stdout, stderr = proc.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
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
            subprocess.run(
                ["systemctl", "--user", "stop", f"{unit}.scope"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5,
                check=False,
            )
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        for reader in readers:
            reader.join()
        stdout = "".join(retained_lines[0])
        stderr = "".join(retained_lines[1])
        stdout_bytes, stderr_bytes = raw_sizes
    seconds = time.monotonic() - started
    return RunResult(
        stdout,
        stderr,
        124 if timed_out else proc.returncode,
        seconds,
        timed_out,
        stdout_bytes,
        stderr_bytes,
    )


def parse_acacia_result(stdout_stderr: str) -> str:
    text = stdout_stderr.upper()
    if "REALIZABLE" in text and "UNREALIZABLE" not in text:
        return "REALIZABLE"
    if "UNREALIZABLE" in text:
        return "UNREALIZABLE"
    if "TIMEOUT" in text:
        return "TIMEOUT"
    return "UNKNOWN"


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
    if not text:
        return None
    upper = text.upper()
    if "UNREALIZABLE" in upper:
        return "UNREALIZABLE"
    if "REALIZABLE" in upper:
        return "REALIZABLE"
    return None


def write_csv(path: str | pathlib.Path, rows: list[dict], fieldnames: list[str]) -> None:
    with pathlib.Path(path).open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
