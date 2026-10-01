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
from collections.abc import Mapping
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
    scope_unit: str = ""


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


def user_manager_controllers(uid: int | None = None) -> set[str] | None:
    """Return the cgroup controllers delegated to this user's systemd manager.

    `None` means the answer could not be read, which is not the same as an
    empty set: callers that need a controller should refuse on either.
    """
    uid = os.getuid() if uid is None else uid
    path = pathlib.Path(
        f"/sys/fs/cgroup/user.slice/user-{uid}.slice/user@{uid}.service/cgroup.controllers"
    )
    try:
        return set(path.read_text(encoding="utf-8").split())
    except OSError:
        return None


class ScopeConstraintError(RuntimeError):
    """A scope property was requested that this systemd cannot enforce."""


def run_systemd_scope(
    cmd: list[str],
    timeout: float,
    memory_max: str,
    memory_swap_max: str = "0",
    env: dict[str, str] | None = None,
    unit_prefix: str = "acacia-bench",
    capture_filter: Callable[[str], bool] | None = None,
    capture_consumer: Callable[[str], None] | None = None,
    allowed_cpus: str | None = None,
    cpu_quota: str | None = None,
    scope_env: Mapping[str, str] | None = None,
) -> RunResult:
    """Run cmd in a resource-limited user scope and stop the scope on timeout.

    A process-group timeout alone is insufficient here: systemd migrates the
    solver out of the systemd-run client's process group.  Naming the scope
    lets the timeout path stop the solver and all decomposed children before
    collecting the client's pipes.

    A portfolio invocation races several children inside this one scope, so
    every limit here is a whole-race budget rather than a per-child one.  That
    is what makes `allowed_cpus` and `cpu_quota` necessary for comparing worker
    counts: without them a race of eight children is handed eight times the CPU
    of a race of one, and the comparison measures the extra hardware rather
    than the portfolio.  Both are left unset by default, which keeps existing
    single-invocation campaigns byte-identical to before.
    """
    if not unit_prefix.startswith("acacia-"):
        raise ValueError("unit_prefix must start with 'acacia-' so campaign sweeps can find it")
    if allowed_cpus is not None:
        # systemd accepts AllowedCPUs on a user scope whether or not it can
        # apply it.  Without the cpuset controller delegated to the user
        # manager the property is dropped: the scope gets no cpuset.cpus, its
        # processes keep the full affinity mask, and a campaign labelled as a
        # fixed-core comparison silently runs on every core.  CPUQuota rides
        # the cpu controller, which user managers normally do have.
        controllers = user_manager_controllers()
        if controllers is None or "cpuset" not in controllers:
            found = "unreadable" if controllers is None else " ".join(sorted(controllers))
            raise ScopeConstraintError(
                f"allowed_cpus={allowed_cpus!r} cannot be enforced: the user systemd "
                f"manager's delegated controllers are [{found}], without cpuset, so "
                f"AllowedCPUs would be silently ignored. Use cpu_quota for a fixed CPU "
                f"budget, or delegate cpuset to user@.service."
            )
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
    ]
    # Only appended when asked for: an unset property and a property set to the
    # machine's full width are not the same thing to systemd, and campaigns
    # already recorded were run with neither.
    if allowed_cpus is not None:
        scoped_cmd.append(f"--property=AllowedCPUs={allowed_cpus}")
    if cpu_quota is not None:
        scoped_cmd.append(f"--property=CPUQuota={cpu_quota}")
    # A transient scope receives the user manager's environment, not arbitrary
    # additions made to the systemd-run client's environment.  Keep this
    # opt-in so every existing campaign argv stays unchanged, while callers
    # which need child-only metadata can explicitly propagate it through the
    # manager boundary.
    if scope_env is not None:
        scoped_cmd.extend(
            f"--setenv={name}={value}" for name, value in sorted(scope_env.items())
        )
    scoped_cmd += [*cmd]
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
            if peak.isdigit() and int(peak) < (1 << 64) - 1:
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
            f"{unit}.scope",
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


#: The two verdicts that count as an answer. Every other outcome -- a timeout,
#: a resource limit, UNKNOWN, an error -- is a non-answer, and PAR-2 charges it.
SOLVED = frozenset({"REALIZABLE", "UNREALIZABLE"})


def par2(solved_seconds: float, unsolved: int, timeout: float) -> float:
    """PAR-2: measured time for the instances that answered, and twice the cap
    charged for every instance that did not.

    Callers classify their own rows -- what counts as an answer differs between
    the Meson testlog and the campaign CSVs, and that judgement belongs with
    the reader of each format. What must not differ is the arithmetic, which
    used to be written out at four call sites.
    """
    return solved_seconds + 2.0 * timeout * unsolved


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

# Vocabulary of run-subset.py CSVs and the cactus reporting view. Coverage
# exports normalize failure subtypes to this view without reclassifying runs.
CACTUS_SOLVED_RESULTS = frozenset(("REALIZABLE", "UNREALIZABLE"))
CACTUS_NON_SOLVED_RESULTS = (
    "TIMEOUT", "RESOURCE_LIMIT", "UNKNOWN", "ERROR", "SYFCO-FAIL",
)


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


# --- Opt-in phase records (ACACIA_PHASE_RECORDS) -----------------------------
#
# The solver's writer process appends one JSON object per line to <pid>.jsonl
# below the directory the coverage runner creates for each run (see
# src/phase_records.hh).  Every producer process ends with a record_summary
# event when it exits normally; a process killed by the race winner, the
# runner's timeout or the OOM killer has none.  Declines are encoded as:
#   lift_decline_<stage>     the lifting arm's final tlsf-tools stage
#   reduce_decline_<stage>   the direct GR(1) arm's reduction stage
#   budget_decline           a construction-budget stop, with a "stage" field
# None of these carries the tlsf-tools status: a deadline expiry is recorded
# under whatever stage was running when the clock check fired, so a stage name
# alone cannot show that a decline was, or was not, caused by the deadline.

#: Fields that measure time or memory rather than work or decisions.
PHASE_VOLATILE_FIELDS = frozenset({
    "wall_ns", "cpu_ns", "elapsed_ns", "rss_kb", "peak_rss_kb",
    "peak_rss_bytes", "mallinfo2",
})
PHASE_DECLINE_PREFIXES = ("lift_decline_", "reduce_decline_")


def phase_record_dir(root, solver_label: str, cap, instance: str, run_index) -> pathlib.Path:
    """The per-run directory run-syntcomp26-coverage.py gives ACACIA_PHASE_RECORDS."""
    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "_", solver_label)
    safe_instance = re.sub(r"[^A-Za-z0-9_.-]", "_", instance)
    return pathlib.Path(root) / safe_label / str(cap) / f"{safe_instance}-{run_index}"


@dataclass(frozen=True)
class PhaseProcess:
    """The records one solver process wrote, in file order."""
    name: str
    events: tuple
    finished: bool
    dropped: int

    @property
    def arms(self) -> frozenset:
        return frozenset(event.get("arm", "") for event in self.events
                         if event.get("phase") != "record_summary")


@dataclass(frozen=True)
class PhaseRecords:
    """All processes' records for one run; empty when the run wrote none."""
    directory: pathlib.Path
    processes: tuple

    @property
    def dropped(self) -> int:
        return sum(process.dropped for process in self.processes)

    def arms(self) -> list[str]:
        return sorted({arm for process in self.processes for arm in process.arms})

    def arm_processes(self, arm: str) -> list[PhaseProcess]:
        return [process for process in self.processes if arm in process.arms]

    def arm_finished(self, arm: str) -> bool:
        processes = self.arm_processes(arm)
        return bool(processes) and all(process.finished for process in processes)

    def declines(self) -> list[tuple[str, str, str]]:
        """(arm, kind, stage) for every recorded decline, in record order."""
        found = []
        for process in self.processes:
            for event in process.events:
                phase = str(event.get("phase", ""))
                arm = str(event.get("arm", ""))
                if phase == "budget_decline":
                    found.append((arm, "budget_decline", str(event.get("stage", ""))))
                    continue
                for prefix in PHASE_DECLINE_PREFIXES:
                    if phase.startswith(prefix):
                        found.append((arm, prefix.rstrip("_"), phase[len(prefix):]))
        return found

    def canonical(self, arm: str, volatile=PHASE_VOLATILE_FIELDS) -> tuple:
        """Order-independent, time-free content of one arm's records.

        Each process contributes its event sequence with volatile fields and
        the drop counter removed; processes are sorted, because PIDs (and
        hence file names and order) differ between runs.
        """
        sequences = []
        for process in self.arm_processes(arm):
            sequences.append(tuple(
                json.dumps({key: value for key, value in event.items()
                            if key not in volatile and key != "dropped_records"},
                           sort_keys=True)
                for event in process.events))
        return tuple(sorted(sequences))


def load_phase_records(directory) -> PhaseRecords:
    """Read every <pid>.jsonl below one run's record directory.

    A missing directory yields no processes; malformed JSON raises ValueError,
    because a torn record cannot establish what the run decided.
    """
    directory = pathlib.Path(directory)
    processes = []
    if directory.is_dir():
        for path in sorted(directory.glob("*.jsonl")):
            events = []
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if not line.strip():
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as error:
                    raise ValueError(f"{path}:{number}: malformed phase record") from error
                if not isinstance(event, dict):
                    raise ValueError(f"{path}:{number}: phase record is not an object")
                events.append(event)
            summaries = [event for event in events if event.get("phase") == "record_summary"]
            dropped = sum(int(event.get("dropped_records") or 0) for event in summaries)
            processes.append(PhaseProcess(path.name, tuple(events), bool(summaries), dropped))
    return PhaseRecords(directory, tuple(processes))


# --- Timed-run marker protocol ------------------------------------------------
#
# Sprint scripts share one directory of marker files.  A timed run waits while
# any blocking marker exists (a build, another timed run), then creates its own
# marker atomically and removes it on exit.  Builds and other timed runs wait
# for that marker in turn.

DEFAULT_BLOCKING_MARKERS = ("DRIVER-BUILDING", "TRACKB-BUILDING", "TIMED-RUN-ACTIVE")
TIMED_RUN_MARKER = "TIMED-RUN-ACTIVE"


class MarkerTimeout(RuntimeError):
    """The blocking markers did not clear within the allowed wait."""


@contextmanager
def timed_run_marker(marker_dir, owner: str, *, blocking=DEFAULT_BLOCKING_MARKERS,
                     marker: str = TIMED_RUN_MARKER, poll: float = 5.0,
                     max_wait: float | None = None, sleep=time.sleep,
                     announce=None):
    """Hold the timed-run marker for the duration of the block.

    Creation uses O_EXCL, so two drivers cannot both pass the wait and start
    timing: the loser sees the winner's marker and keeps waiting.  The marker
    is removed on exit only if it still holds this owner's token.
    """
    marker_dir = pathlib.Path(marker_dir)
    if not marker_dir.is_dir():
        raise FileNotFoundError(f"marker directory does not exist: {marker_dir}")
    path = marker_dir / marker
    token = json.dumps({"owner": owner, "pid": os.getpid(), "token": uuid.uuid4().hex,
                        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())},
                       sort_keys=True) + "\n"
    waited = 0.0
    announced = False
    while True:
        present = [name for name in blocking if (marker_dir / name).exists()]
        if not present:
            try:
                descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
            except FileExistsError:
                present = [marker]
            else:
                with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                    stream.write(token)
                break
        if max_wait is not None and waited >= max_wait:
            raise MarkerTimeout(f"blocking markers still present after {waited:g} s: "
                                + ", ".join(present))
        if announce is not None and not announced:
            announce(f"waiting for marker(s) to clear: {', '.join(present)}")
            announced = True
        sleep(poll)
        waited += poll
    try:
        yield path
    finally:
        try:
            if path.read_text(encoding="utf-8") == token:
                path.unlink()
        except FileNotFoundError:
            pass


# --- Host-condition sampling (§9.1) ------------------------------------------
#
# A Python port of the per-arm campaign's thermal sampler: the same eight
# leading columns, so thermal-annotate.py's metrics() and coverage() read the
# output unchanged, plus mean CPU clock and swap in use.  Times are UTC.

SAMPLE_COLUMNS = ["time", "pkg_c", "load1", "mem_avail_mib", "pkg_throttle",
                  "core_throttle_sum", "leg", "rows", "cpu_mhz_mean", "swap_used_mib"]


def _read_text(path: pathlib.Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        return None


def read_host_sample(sys_root="/sys", proc_root="/proc") -> dict[str, str]:
    """One reading of package temperature, load, memory, throttling and clock.

    Every field is best effort: an unreadable source leaves an empty cell
    rather than a guess, and nothing here starts a subprocess.
    """
    sys_root, proc_root = pathlib.Path(sys_root), pathlib.Path(proc_root)
    sample = dict.fromkeys(SAMPLE_COLUMNS, "")
    for hwmon in sorted((sys_root / "class/hwmon").glob("hwmon*")):
        for label in sorted(hwmon.glob("temp*_label")):
            if _read_text(label) == "Package id 0":
                value = _read_text(hwmon / label.name.replace("_label", "_input"))
                if value and value.lstrip("-").isdigit():
                    sample["pkg_c"] = f"{int(value) / 1000:.1f}"
                break
        if sample["pkg_c"]:
            break
    loadavg = _read_text(proc_root / "loadavg")
    if loadavg:
        sample["load1"] = loadavg.split()[0]
    meminfo = {}
    for line in (_read_text(proc_root / "meminfo") or "").splitlines():
        name, _, rest = line.partition(":")
        fields = rest.split()
        if fields and fields[0].isdigit():
            meminfo[name] = int(fields[0])
    if "MemAvailable" in meminfo:
        sample["mem_avail_mib"] = str(meminfo["MemAvailable"] // 1024)
    if "SwapTotal" in meminfo and "SwapFree" in meminfo:
        sample["swap_used_mib"] = str((meminfo["SwapTotal"] - meminfo["SwapFree"]) // 1024)
    cpus = sorted((sys_root / "devices/system/cpu").glob("cpu[0-9]*"))
    package = _read_text(sys_root / "devices/system/cpu/cpu0/thermal_throttle/package_throttle_count")
    if package and package.isdigit():
        sample["pkg_throttle"] = package
    core_counts = [_read_text(cpu / "thermal_throttle/core_throttle_count") for cpu in cpus]
    core_counts = [int(value) for value in core_counts if value and value.isdigit()]
    if core_counts:
        sample["core_throttle_sum"] = str(sum(core_counts))
    clocks = [_read_text(cpu / "cpufreq/scaling_cur_freq") for cpu in cpus]
    clocks = [int(value) for value in clocks if value and value.isdigit()]
    if clocks:
        sample["cpu_mhz_mean"] = f"{sum(clocks) / len(clocks) / 1000:.0f}"
    return sample


def count_data_rows(path) -> str:
    """Rows written so far to a header-first TSV, or empty if it is absent."""
    try:
        with pathlib.Path(path).open(encoding="utf-8") as stream:
            return str(max(0, sum(1 for _ in stream) - 1))
    except OSError:
        return ""


class HostSampler:
    """Append a host sample every `interval` seconds while a campaign runs.

    The current leg label and its output file are set by the driver; the row
    count lets a reader place each sample within the leg.
    """

    def __init__(self, output, interval: float = 30.0, reader=read_host_sample,
                 clock=None):
        self.output = pathlib.Path(output)
        self.interval = interval
        self.reader = reader
        self.clock = clock or (lambda: time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()))
        self._leg = ""
        self._rows_path = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None

    def set_leg(self, label: str, rows_path=None) -> None:
        with self._lock:
            self._leg = label
            self._rows_path = rows_path

    def sample_once(self) -> dict[str, str]:
        with self._lock:
            leg, rows_path = self._leg, self._rows_path
        row = self.reader()
        row.update(time=self.clock(), leg=leg,
                   rows=count_data_rows(rows_path) if rows_path else "")
        self.output.parent.mkdir(parents=True, exist_ok=True)
        new = not self.output.exists() or self.output.stat().st_size == 0
        with self.output.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=SAMPLE_COLUMNS, delimiter="\t",
                                    lineterminator="\n", extrasaction="ignore")
            if new:
                writer.writeheader()
            writer.writerow(row)
        return row

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                self.sample_once()
            except OSError as error:
                print(f"host sampler: {error}", file=sys.stderr)
            self._stop.wait(self.interval)

    def __enter__(self):
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, name="host-sampler", daemon=True)
        self._thread.start()
        return self

    def __exit__(self, *exc) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=5)
        try:
            self.sample_once()
        except OSError:
            pass
