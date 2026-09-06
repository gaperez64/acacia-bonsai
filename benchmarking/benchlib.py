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
import sys
import threading
import time
import uuid
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Callable


VERDICT_RE = re.compile(r"(?:^|\]\s)(UNREALIZABLE|REALIZABLE)\s*$", re.MULTILINE)
ROOT = pathlib.Path(__file__).resolve().parents[1]


def build_option(build_dir, name):
    """Read a Meson option, returning None for missing or unreadable metadata."""
    try:
        options_path = pathlib.Path(build_dir) / "meson-info" / "intro-buildoptions.json"
        options = json.loads(options_path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return None
    if not isinstance(options, list):
        return None
    return next(
        (
            option.get("value")
            for option in options
            if isinstance(option, dict) and option.get("name") == name
        ),
        None,
    )


def build_preset(build_dir) -> str | None:
    """Return the recorded configuration name, or None for an unnamed build."""
    value = build_option(build_dir, "acacia_preset")
    return value if isinstance(value, str) and value else None


def _corpus_path(value) -> pathlib.Path | None:
    if isinstance(value, (str, pathlib.Path)) and str(value).strip():
        return pathlib.Path(value).expanduser().resolve()
    return None


def _recorded_corpus() -> str | None:
    try:
        return (ROOT / ".acacia-tlsf-corpus-path").read_text(encoding="utf-8").strip()
    except OSError:
        return None


CORPUS_MARKER = ".acacia-tlsf-corpus"


def tlsf_corpus_candidates(explicit=None, build_dir=None, env=None) -> list[tuple[str, object, bool]]:
    """The (mechanism, raw value, needs_marker) triples consulted, in order.

    Only the recorded pointer needs the marker: the other three were named by
    the operator for this run, and second-guessing them would be unhelpful,
    while the pointer is a leftover record that must be shown to still describe
    a corpus materialize actually produced.
    """
    if env is None:
        env = os.environ
    return [
        ("--tlsf-corpus", explicit, False),
        ("ACACIA_TLSF_CORPUS", env.get("ACACIA_TLSF_CORPUS"), False),
        ("acacia_tlsf_corpus_dir", build_option(build_dir, "acacia_tlsf_corpus_dir"), False),
        (f"{CORPUS_MARKER}-path", _recorded_corpus(), True),
    ]


def _corpus_rejection(corpus: pathlib.Path | None, needs_marker: bool) -> str | None:
    """Why this candidate cannot be used, or None when it can."""
    if corpus is None:
        return "is unset"
    if not corpus.is_dir():
        return f"names {corpus}, which is not a directory"
    if needs_marker and not (corpus / CORPUS_MARKER).is_file():
        return f"names {corpus}, which carries no {CORPUS_MARKER} marker"
    return None


def tlsf_corpus_dir(explicit=None, build_dir=None, env=None) -> pathlib.Path | None:
    """Resolve flag, environment, Meson option, then the recorded corpus.

    A mechanism naming a directory that is not there is skipped rather than
    returned, so a stale setting cannot mask a live one, and cannot turn into a
    per-instance "TLSF source is absent" mystery further down.
    """
    for _, value, needs_marker in tlsf_corpus_candidates(explicit, build_dir, env):
        corpus = _corpus_path(value)
        if _corpus_rejection(corpus, needs_marker) is None:
            return corpus
    return None


def tlsf_corpus_diagnosis(explicit=None, build_dir=None, env=None) -> str:
    """Say what was consulted and why none of it produced a corpus directory."""
    tried = []
    for mechanism, value, needs_marker in tlsf_corpus_candidates(explicit, build_dir, env):
        rejection = _corpus_rejection(_corpus_path(value), needs_marker)
        if rejection is None:
            return ""  # something resolved; there is nothing to explain
        tried.append(f"{mechanism} {rejection}")
    return "; ".join(tried)


def tlsf_failure(key, detail) -> str:
    """Explain a missing TLSF source and the shared ways to locate its corpus."""
    suite, instance = key
    return (
        f"GATE FAIL: {suite}/{instance} needs its TLSF source, but {detail}; "
        "run `python3 benchmarking/syntcomp-corpus.py materialize --out DIR` "
        "and export ACACIA_TLSF_CORPUS=DIR or configure the build with "
        "-Dacacia_tlsf_corpus_dir=DIR"
    )


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


def _user_scope_running(unit: str) -> bool | None:
    """Return None when the manager cannot confirm the scope's state."""
    try:
        result = subprocess.run(
            ["systemctl", "--user", "show", unit,
             "--property=LoadState,ActiveState,SubState"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=1,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    properties = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    if properties.get("LoadState") == "not-found":
        return False
    if result.returncode != 0:
        return None
    # Failed/dead scopes are residue, with no processes left to kill.  In
    # particular, deactivating scopes are still live and need escalation.
    active = properties.get("ActiveState")
    if active in {"inactive", "failed"} and properties.get("SubState") in {"dead", "failed"}:
        return False
    if active:
        return True
    return None


def _stop_user_scope(unit: str) -> None:
    """Best-effort, bounded teardown, including verification and escalation.

    Called from finally blocks and signal handlers: manager failures must not
    mask the campaign's original exception or prevent the remaining cleanup.
    Accept both the stem used by run_systemd_scope and a listed unit name.
    """
    if not unit.endswith(".scope"):
        unit += ".scope"
    problem = "stop returned success"
    try:
        result = subprocess.run(
            ["systemctl", "--user", "stop", unit],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=2, check=False,
        )
        if result.returncode != 0:
            problem = f"stop exited {result.returncode}"
    except (OSError, subprocess.SubprocessError) as error:
        problem = f"stop failed: {error}"
    running = _user_scope_running(unit)
    if running is False:
        return
    state = "scope is still running" if running else "scope state could not be verified"
    print(f"scope cleanup: {unit}: {problem}; {state}; escalating to SIGKILL", file=sys.stderr)
    try:
        result = subprocess.run(
            ["systemctl", "--user", "kill", "--signal=SIGKILL", unit],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=2, check=False,
        )
        if result.returncode != 0:
            print(f"scope cleanup: {unit}: SIGKILL exited {result.returncode}", file=sys.stderr)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"scope cleanup: {unit}: SIGKILL failed: {error}", file=sys.stderr)
    running = _user_scope_running(unit)
    outcome = {
        False: "scope is no longer running",
        True: "WARNING: scope is still running after SIGKILL",
        None: "WARNING: could not verify scope teardown after SIGKILL",
    }[running]
    print(f"scope cleanup: {unit}: {outcome}", file=sys.stderr)


CAMPAIGN_SCOPE_GUARD_ENV = "ACACIA_CAMPAIGN_SCOPE_GUARD"


@dataclass(frozen=True)
class AcaciaScope:
    unit: str
    active: str
    sub: str

    @property
    def state(self) -> str:
        return "failed" if self.active == "failed" else "running"


def _clean_acacia_scope(scope: AcaciaScope, name: str) -> None:
    if scope.state == "running":
        print(f"{name}: found surviving running scope {scope.unit} "
              f"({scope.active}/{scope.sub}); stopping", file=sys.stderr)
        _stop_user_scope(scope.unit)
        return
    print(f"{name}: resetting failed scope residue {scope.unit}", file=sys.stderr)
    try:
        result = subprocess.run(
            ["systemctl", "--user", "reset-failed", scope.unit],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=2, check=False,
        )
        if result.returncode != 0:
            print(f"{name}: reset-failed {scope.unit} exited {result.returncode}", file=sys.stderr)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"{name}: reset-failed {scope.unit} failed: {error}", file=sys.stderr)


def sweep_acacia_scopes(*, stop: bool = False, name: str = "scope sweep") -> list[AcaciaScope]:
    """List running/failed user acacia-* scopes, optionally cleaning them up.

    Return the discovered scopes (the attempted actions in stop mode).  Dead,
    inactive units and malformed rows are ignored.  An unavailable user
    manager is treated as an empty listing so non-systemd drivers still work.
    """
    try:
        result = subprocess.run(
            ["systemctl", "--user", "list-units", "--type=scope", "--all",
             "--plain", "--no-legend", "acacia-*"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, timeout=2, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    if result.returncode != 0:
        return []
    scopes = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        unit, _load, active, sub = fields[:4]
        if not (unit.startswith("acacia-") and unit.endswith(".scope")):
            continue
        if active not in {"active", "activating", "deactivating", "reloading", "refreshing", "failed"}:
            continue
        scope = AcaciaScope(unit, active, sub)
        scopes.append(scope)
        if stop:
            _clean_acacia_scope(scope, name)
    return scopes


def check_campaign_scopes(name: str, *, allow_strays: bool = False, scopes=None) -> bool:
    """Report/reset failed residue and refuse contention unless explicitly allowed.

    A caller that has already listed the scopes passes them in, so the check
    and whatever it does next agree on one observation.
    """
    if scopes is None:
        scopes = sweep_acacia_scopes()
    running = [scope for scope in scopes if scope.state == "running"]
    for scope in scopes:
        if scope.state == "failed":
            _clean_acacia_scope(scope, name)
    if not running:
        return True
    for scope in running:
        print(f"{name}: running scope {scope.unit} ({scope.active}/{scope.sub})", file=sys.stderr)
    if allow_strays:
        print(f"{name}: continuing with ACACIA_ALLOW_STRAY_SCOPES=1; "
              "measurements may be under contention", file=sys.stderr)
        return True
    print(f"{name}: refusing to start with running acacia-* scopes. "
          "Clear them with `python3 benchmarking/sweep-acacia-scopes.py --stop`, "
          "or deliberately override with ACACIA_ALLOW_STRAY_SCOPES=1.", file=sys.stderr)
    return False


@contextmanager
def campaign_scope_guard(name: str):
    """Check campaign isolation on entry and report/clean survivors on exit.

    The marker is inherited by subprocess campaigns; only the outer owner
    checks and sweeps.  It is distinct from ACACIA_OUTER_CGROUP, which controls
    solver resource limits, and is restored even on exception or SystemExit.
    """
    if os.environ.get(CAMPAIGN_SCOPE_GUARD_ENV):
        yield
        return
    entry_scopes = sweep_acacia_scopes()
    if not check_campaign_scopes(
        name,
        allow_strays=os.environ.get("ACACIA_ALLOW_STRAY_SCOPES") == "1",
        scopes=entry_scopes,
    ):
        raise SystemExit(1)
    # The question this guard exists to ask is "did anything *I* started
    # outlive me?".  Anything already running is somebody else's -- only
    # reachable with the stray override -- and stopping a concurrent
    # campaign's solver would be a worse bug than the one being fixed.
    inherited = {scope.unit for scope in entry_scopes if scope.state == "running"}
    previous = os.environ.get(CAMPAIGN_SCOPE_GUARD_ENV)
    os.environ[CAMPAIGN_SCOPE_GUARD_ENV] = f"{name}:{os.getpid()}"
    try:
        yield
    finally:
        try:
            survivors = [
                scope for scope in sweep_acacia_scopes()
                if scope.unit not in inherited
            ]
            for scope in survivors:
                _clean_acacia_scope(scope, f"{name} exit")
            if survivors:
                # Reported, not cleaned silently: something surviving the
                # campaign IS the finding.
                print(f"{name} exit: cleaned up {len(survivors)} scope(s) that "
                      "outlived this campaign", file=sys.stderr)
        finally:
            if previous is None:
                os.environ.pop(CAMPAIGN_SCOPE_GUARD_ENV, None)
            else:
                os.environ[CAMPAIGN_SCOPE_GUARD_ENV] = previous


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
    if not unit_prefix.startswith("acacia-"):
        raise ValueError("unit_prefix must start with 'acacia-' so campaign sweeps can find it")
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
