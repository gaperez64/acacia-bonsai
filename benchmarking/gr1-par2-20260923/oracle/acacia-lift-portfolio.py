#!/usr/bin/env python3
"""Run checked online GR(1) lifting and direct solving before Acacia fallback.

Wrapper options precede ``--``.  Everything after it is the fallback argv and
is passed to :func:`os.execv` unchanged.  An outer runner should provide
``ACACIA_OUTER_DEADLINE_MONOTONIC``.  Without it, ``--cap`` starts a less exact
deadline at wrapper entry and therefore cannot account for launcher startup.
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import hashlib
import json
import math
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass
from typing import Any

from acacia_lift.lifting import settings
from acacia_lift.tools import configuration_defaults


ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_LIFT_ENTRY = "python -m acacia_lift.runner"
DEADLINE_ENV = "ACACIA_OUTER_DEADLINE_MONOTONIC"
ROUTE_RECORD_ENV = "ACACIA_ROUTE_RECORD"
DECISIVE_EXITS = {"REALIZABLE": 0, "UNREALIZABLE": 1}
ERROR_EXIT = 3
PR_SET_CHILD_SUBREAPER = 36
PR_GET_CHILD_SUBREAPER = 37


class UsageError(ValueError):
    """An invocation cannot preserve the wrapper/fallback contract."""


class WrapperParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise UsageError(message)


def positive_finite(value: str) -> float:
    try:
        result = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a number") from None
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return result


def fraction(value: str) -> float:
    result = positive_finite(value)
    if result >= 1:
        raise argparse.ArgumentTypeError("must be less than one")
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = WrapperParser(
        description=__doc__,
        usage="%(prog)s [wrapper options] -- B_EXECUTABLE [B arguments] -T TLSF",
    )
    budget = parser.add_mutually_exclusive_group()
    budget.add_argument(
        "--lift-budget-fraction",
        type=fraction,
        default=settings.DEFAULT_LIFT_FRACTION,
        metavar="F",
        help="fraction of the outer cap assigned to lifting (default: 1/3)",
    )
    budget.add_argument(
        "--lift-budget-seconds",
        type=positive_finite,
        metavar="S",
        help="fixed lifting budget; the rest of the cap is reserved for fallback",
    )
    parser.add_argument(
        "--cap",
        type=positive_finite,
        help=f"outer cap used only when {DEADLINE_ENV} is absent",
    )
    parser.add_argument("--lift-entry", default=DEFAULT_LIFT_ENTRY,
                        help="lifting CLI (default: python -m acacia_lift.runner)")
    parser.add_argument("--tlsf-tools-build", type=pathlib.Path)
    parser.add_argument("--bindings-python", type=pathlib.Path)
    parser.add_argument("--bindings-site", type=pathlib.Path)
    parser.add_argument("--buddy-adapter", type=pathlib.Path)
    parser.add_argument(
        "--eligibility-budget-seconds", type=positive_finite,
        default=settings.DEFAULT_ELIGIBILITY_BUDGET_SECONDS,
        metavar="S", help="maximum source-binding time (default: 1 s, capped at 5%% of cap)",
    )
    parser.add_argument(
        "--route-record",
        type=pathlib.Path,
        default=os.environ.get(ROUTE_RECORD_ENV),
        metavar="PATH",
    )
    return parser


def split_invocation(argv: list[str]) -> tuple[list[str], list[str]]:
    try:
        separator = argv.index("--")
    except ValueError:
        raise UsageError("missing -- before the fallback command") from None
    wrapper_argv = argv[:separator]
    fallback_argv = argv[separator + 1 :]
    if not fallback_argv:
        raise UsageError("the fallback command after -- is empty")
    return wrapper_argv, fallback_argv


def tlsf_argument(argv: list[str]) -> str:
    found: list[str] = []
    index = 1
    while index < len(argv):
        argument = argv[index]
        if argument in {"-T", "--tlsf"}:
            if index + 1 >= len(argv):
                raise UsageError(f"{argument} needs a TLSF path")
            found.append(argv[index + 1])
            index += 2
            continue
        if argument.startswith("--tlsf="):
            found.append(argument.split("=", 1)[1])
        index += 1
    if len(found) != 1 or not found[0]:
        raise UsageError("fallback argv must contain exactly one -T/--tlsf input")
    return found[0]


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def spool_stdin(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("xb") as stream:
        while True:
            chunk = sys.stdin.buffer.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
            stream.write(chunk)
        stream.flush()
        os.fsync(stream.fileno())
    return digest.hexdigest()


def atomic_write_json(path: pathlib.Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def deadline_from_environment(started: float, cap: float | None) -> tuple[float, float]:
    raw_deadline = os.environ.get(DEADLINE_ENV)
    if raw_deadline is None:
        if cap is None:
            raise UsageError(f"--cap is required when {DEADLINE_ENV} is absent")
        return started + cap, cap
    try:
        deadline = float(raw_deadline)
    except ValueError:
        raise UsageError(f"{DEADLINE_ENV} must be a number") from None
    if not math.isfinite(deadline):
        raise UsageError(f"{DEADLINE_ENV} must be finite")
    measured_cap = cap if cap is not None else max(0.0, deadline - started)
    return deadline, measured_cap


def evidence_path(route_record: pathlib.Path | None, scratch: pathlib.Path) -> pathlib.Path:
    if route_record is None:
        return scratch / "lift-evidence.json"
    return route_record.with_name(route_record.name + ".lift-evidence.json")


def lift_command(
    args: argparse.Namespace,
    source: pathlib.Path,
    evidence: pathlib.Path,
    output: pathlib.Path,
    timeout: float,
) -> list[str]:
    entry = ([str(args.bindings_python or configuration_defaults().bindings_python),
              "-s", "-m", "acacia_lift.runner"]
             if args.lift_entry == DEFAULT_LIFT_ENTRY else [str(args.lift_entry)])
    command = [
        *entry,
        "--request-mode",
        "source",
        "-T",
        str(source),
        "--budget",
        f"{timeout:.6f}",
        "--eligibility-budget-seconds",
        repr(args.eligibility_budget_seconds),
        "--output-dir",
        str(output),
        "--evidence-out",
        str(evidence),
    ]
    for option, value in (
        ("--tlsf-tools-build", args.tlsf_tools_build),
        ("--bindings-python", args.bindings_python),
        ("--bindings-site", args.bindings_site),
        ("--buddy-adapter", args.buddy_adapter),
    ):
        if value is not None:
            command.extend((option, str(value)))
    return command


@dataclass(frozen=True)
class LiftOutcome:
    returncode: int | None
    elapsed: float
    timed_out: bool
    spawn_error: str | None = None


def set_child_subreaper(enabled: bool) -> None:
    if sys.platform != "linux":
        raise OSError(errno.ENOSYS, "child subreapers require Linux")
    libc = ctypes.CDLL(None, use_errno=True)
    prctl = libc.prctl
    prctl.restype = ctypes.c_int
    if prctl(PR_SET_CHILD_SUBREAPER, int(enabled), 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))


def enable_child_subreaper() -> bool:
    if sys.platform != "linux":
        raise OSError(errno.ENOSYS, "child subreapers require Linux")
    libc = ctypes.CDLL(None, use_errno=True)
    prctl = libc.prctl
    prctl.restype = ctypes.c_int
    current = ctypes.c_int()
    if prctl(PR_GET_CHILD_SUBREAPER, ctypes.byref(current), 0, 0, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))
    changed = current.value == 0
    if changed:
        set_child_subreaper(True)
    return changed


def reap_group_children(pgid: int) -> None:
    while True:
        try:
            child, _status = os.waitpid(-pgid, os.WNOHANG)
        except ChildProcessError:
            return
        except InterruptedError:
            continue
        if child == 0:
            return


def process_group_exists(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def terminate_group(proc: subprocess.Popen[bytes]) -> None:
    pgid = proc.pid
    deadline = time.monotonic() + settings.PROCESS_GROUP_CLEANUP_SECONDS
    try:
        os.killpg(pgid, signal.SIGKILL)
    except ProcessLookupError:
        pass

    try:
        proc.communicate(timeout=max(0.0, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        # Do not trust pipe closure as a process-tree liveness signal.  Keep
        # driving the group to death and use the subreaper to collect orphans.
        try:
            os.killpg(pgid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    while True:
        proc.poll()
        reap_group_children(pgid)
        if not process_group_exists(pgid):
            return
        if time.monotonic() >= deadline:
            raise TimeoutError(f"lifting process group {pgid} survived SIGKILL")
        try:
            os.killpg(pgid, signal.SIGKILL)
        except ProcessLookupError:
            continue
        time.sleep(settings.PROCESS_POLL_INTERVAL_SECONDS)


def run_lift(command: list[str], timeout: float, *,
             bindings_site: pathlib.Path | None = None) -> LiftOutcome:
    started = time.monotonic()
    restore_subreaper = enable_child_subreaper()
    try:
        try:
            python_path = str(ROOT / "benchmarking/gr1-par2-20260923/oracle")
            if bindings_site is not None:
                python_path = str(bindings_site) + os.pathsep + python_path
            if os.environ.get("PYTHONPATH"):
                python_path += os.pathsep + os.environ["PYTHONPATH"]
            environment = {**os.environ, "PYTHONPATH": python_path}
            if bindings_site is not None:
                environment["PYTHONNOUSERSITE"] = "1"
            proc = subprocess.Popen(
                command,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                start_new_session=True,
            )
        except OSError as error:
            return LiftOutcome(None, time.monotonic() - started, False, str(error))
        timed_out = False
        try:
            proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            terminate_group(proc)
        except BaseException:
            terminate_group(proc)
            raise
        else:
            # Even a successful leader may have left descendants which closed
            # the capture pipes.  Kill, adopt, reap, and prove the group gone.
            terminate_group(proc)
        return LiftOutcome(proc.returncode, time.monotonic() - started, timed_out)
    finally:
        if restore_subreaper:
            set_child_subreaper(False)


def load_evidence(path: pathlib.Path) -> tuple[dict[str, Any] | None, str | None, str | None]:
    if not path.is_file():
        return None, None, "evidence_missing"
    try:
        raw = path.read_bytes()
    except OSError:
        return None, None, "evidence_invalid"
    evidence_hash = hashlib.sha256(raw).hexdigest()
    try:
        payload = json.loads(raw)
    except (UnicodeError, json.JSONDecodeError):
        return None, evidence_hash, "evidence_invalid"
    if not isinstance(payload, dict):
        return None, evidence_hash, "evidence_not_an_object"
    return payload, evidence_hash, None


def nested_value(payload: dict[str, Any], *keys: str) -> Any:
    value: Any = payload
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def verified_verdict(
    evidence: dict[str, Any] | None,
    returncode: int | None,
    input_hash: str,
) -> str | None:
    if evidence is None or evidence.get("target_verified") is not True:
        return None
    result = evidence.get("result")
    if not isinstance(result, dict):
        return None
    verdict = result.get("verdict")
    if verdict not in DECISIVE_EXITS or returncode != DECISIVE_EXITS[verdict]:
        return None

    route = evidence.get("route")
    if route not in {"direct-certified", "lifted-certified"}:
        return None
    if route == "lifted-certified" and verdict != "REALIZABLE":
        return None
    source_binding = evidence.get("source_binding")
    target_certificate = evidence.get("target_certificate")
    if not isinstance(source_binding, dict) or not isinstance(target_certificate, dict):
        return None
    hashes = (nested_value(source_binding, "input", "sha256"),
              target_certificate.get("source_sha256"))
    if any(value != input_hash for value in hashes):
        return None
    expected_side = "system" if verdict == "REALIZABLE" else "environment"
    proof_method = result.get("proof_method")
    checker_verdict = ("REGION_VERIFIED" if proof_method == "gr1-region-v1"
                       else "VERIFIED")
    semantics = target_certificate.get("reduction_semantics")
    if (proof_method not in {"certificate", "gr1-region-v1"} or
            semantics not in {"exact", "strict"} or
            (semantics == "strict" and route != "lifted-certified") or
            target_certificate.get("certificate_side") != expected_side or
            target_certificate.get("checker_verdict") != checker_verdict or
            not all(isinstance(target_certificate.get(key), str) and
                    len(target_certificate[key]) == 64 for key in
                    ("game_sha256", "certificate_sha256"))):
        return None
    policy_hash = target_certificate.get("policy_sha256")
    if ((proof_method == "certificate" and
         (not isinstance(policy_hash, str) or len(policy_hash) != 64)) or
            (proof_method == "gr1-region-v1" and policy_hash is not None)):
        return None
    return verdict


def stage_censoring(evidence: dict[str, Any] | None, timed_out: bool) -> dict[str, Any]:
    result: dict[str, Any] = {"wrapper_timeout": timed_out, "records": []}
    if evidence is None:
        return result
    diagnostics = evidence.get("generalizer_diagnostics")
    if isinstance(diagnostics, dict) and isinstance(diagnostics.get("censored"), list):
        result["records"] = diagnostics["censored"]
    progress = evidence.get("generalizer_progress")
    if isinstance(progress, dict):
        stage = (
            progress.get("active_stage")
            or progress.get("stage")
            or progress.get("current_stage")
        )
        if isinstance(stage, str):
            result["last_stage"] = stage
    final = evidence.get("result")
    if isinstance(final, dict) and isinstance(final.get("stage"), str):
        result.setdefault("last_stage", final["stage"])
    return result


def decline_reason(
    evidence: dict[str, Any] | None, evidence_error: str | None, outcome: LiftOutcome
) -> str:
    if outcome.spawn_error is not None:
        return "lift_spawn_failed"
    if outcome.timed_out:
        reason = "lift_timeout"
    elif evidence_error is not None:
        reason = evidence_error
    else:
        result = evidence.get("result") if evidence is not None else None
        candidate = result.get("reason") if isinstance(result, dict) else None
        reason = candidate if isinstance(candidate, str) and candidate else "lift_not_verified"
    return reason


def record_route(path: pathlib.Path | None, payload: dict[str, Any]) -> None:
    if path is not None:
        atomic_write_json(path, payload)


def exec_fallback(
    fallback_argv: list[str], spool: pathlib.Path | None, scratch: tempfile.TemporaryDirectory[str]
) -> None:
    if spool is not None:
        descriptor = os.open(spool, os.O_RDONLY)
        try:
            os.dup2(descriptor, sys.stdin.fileno())
        finally:
            os.close(descriptor)
    scratch.cleanup()
    os.execv(fallback_argv[0], fallback_argv)


def run(argv: list[str], started: float) -> int:
    scratch: tempfile.TemporaryDirectory[str] | None = None
    try:
        wrapper_argv, fallback_argv = split_invocation(argv)
        args = build_parser().parse_args(wrapper_argv)
        source_argument = tlsf_argument(fallback_argv)
        deadline, cap = deadline_from_environment(started, args.cap)
        lift_budget = (
            args.lift_budget_seconds
            if args.lift_budget_seconds is not None
            else cap * args.lift_budget_fraction
        )
        fallback_reserve = max(0.0, cap - lift_budget)
        args.eligibility_budget_seconds = min(
            args.eligibility_budget_seconds, settings.ELIGIBILITY_CAP_FRACTION * cap
        )
        route_record = args.route_record.resolve() if args.route_record is not None else None

        scratch = tempfile.TemporaryDirectory(prefix="acacia-lift-portfolio-")
        scratch_path = pathlib.Path(scratch.name)
        scratch_path.chmod(0o700)
        spool: pathlib.Path | None = None
        spool = scratch_path / "input.tlsf"
        if source_argument in {"-", "/dev/stdin"}:
            input_hash = spool_stdin(spool)
            lift_source = spool
        else:
            digest = hashlib.sha256()
            with pathlib.Path(source_argument).open("rb") as original, spool.open("wb") as target:
                for chunk in iter(lambda: original.read(1 << 20), b""):
                    digest.update(chunk)
                    target.write(chunk)
            input_hash = digest.hexdigest()
            lift_source = spool
            spool = None

        evidence_file = evidence_path(route_record, scratch_path)
        try:
            evidence_file.unlink()
        except FileNotFoundError:
            pass
        output = scratch_path / "lift-output"
        output.mkdir()
        now = time.monotonic()
        stage_timeout = min(lift_budget, deadline - fallback_reserve - now)
        command = lift_command(args, lift_source, evidence_file, output, max(0.0, stage_timeout))
        if stage_timeout > 0:
            site = ((args.bindings_site or configuration_defaults().bindings_site)
                    if args.lift_entry == DEFAULT_LIFT_ENTRY else None)
            outcome = run_lift(command, stage_timeout, bindings_site=site)
            evidence, evidence_hash, evidence_error = load_evidence(evidence_file)
        else:
            outcome = LiftOutcome(None, 0.0, False)
            evidence, evidence_hash, evidence_error = None, None, "lift_budget_exhausted"

        verdict = verified_verdict(evidence, outcome.returncode, input_hash)
        reason = decline_reason(evidence, evidence_error, outcome)
        base_record: dict[str, Any] = {
            "schema_version": 1,
            "input_sha256": input_hash,
            "binding_reason": reason,
            "eligibility_budget_s": args.eligibility_budget_seconds,
            "route": (evidence.get("route") if verdict is not None
                      else "attempted-declined"),
            "lift_argv": command,
            "lift_exit": outcome.returncode,
            "lift_elapsed": outcome.elapsed,
            "lift_timed_out": outcome.timed_out,
            "evidence_path": str(evidence_file),
            "evidence_sha256": evidence_hash,
            "stage_censoring": stage_censoring(evidence, outcome.timed_out),
            "fallback_start": None,
            "fallback_remaining": None,
            "winner": "lifting" if verdict is not None else "fallback-pending",
        }
        if verdict is not None:
            record_route(route_record, base_record)
            scratch.cleanup()
            scratch = None
            print(verdict, flush=True)
            return DECISIVE_EXITS[verdict]

        fallback_start = time.monotonic()
        base_record["fallback_start"] = fallback_start
        base_record["fallback_remaining"] = max(0.0, deadline - fallback_start)
        record_route(route_record, base_record)
        exec_fallback(fallback_argv, spool, scratch)
    except UsageError:
        raise
    except Exception as error:
        print(f"acacia-lift-portfolio: {error}", file=sys.stderr)
        return ERROR_EXIT
    finally:
        if scratch is not None:
            try:
                scratch.cleanup()
            except Exception:
                pass
    return ERROR_EXIT


def main(argv: list[str] | None = None) -> int:
    try:
        started = time.monotonic()
        arguments = sys.argv[1:] if argv is None else argv
        if "--" not in arguments and any(
            item in {"-h", "--help"} for item in arguments
        ):
            build_parser().print_help()
            return 0
        return run(arguments, started)
    except UsageError as error:
        print(f"acacia-lift-portfolio: {error}", file=sys.stderr)
        return ERROR_EXIT
    except Exception as error:
        print(f"acacia-lift-portfolio: {error}", file=sys.stderr)
        return ERROR_EXIT


if __name__ == "__main__":
    raise SystemExit(main())
