#!/usr/bin/env python3
"""Small shared helpers for Acacia benchmark scripts."""

from __future__ import annotations

import csv
import json
import os
import pathlib
import signal
import subprocess
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class RunResult:
    stdout: str
    stderr: str
    returncode: int
    seconds: float
    timed_out: bool


def run_process_group(cmd: list[str], timeout: float) -> RunResult:
    """Run cmd in a new process group and kill the whole group on timeout."""
    started = time.monotonic()
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
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
    return RunResult(stdout, stderr, proc.returncode, seconds, timed_out)


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
