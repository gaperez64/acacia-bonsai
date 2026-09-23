#!/usr/bin/env python3
"""Cold, bounded campaign wrapper for checked parametric GR(1) certificates.

One invocation handles one exact ``(family, n)`` target.  REAL families are
sent to ``generalize_gr1.py``, which constructs and solves every seed afresh,
grounds the generalized certificate at the target, and checks it with
``tlsfcertcheck``.  The four M5 UNREAL families use the landed exact-semantics
environment-certificate path directly; these rows are labelled
``direct-certified`` and are never presented as lifted rows.

All subprocesses share one monotonic absolute deadline.  Children are
sequential, each is placed in its own process group, and every group is killed
and reaped on timeout or cancellation.  Every invocation has a new workspace
and no cross-invocation cache.  A decisive result is possible only after the
requested target's certificate checker says VERIFIED.  Exit codes match the
coverage runner's Acacia convention: REALIZABLE=0, UNREALIZABLE=1, UNKNOWN=2.

The default ``reproducer`` request mode retains the historical M0
basename-to-row lookup.  Production ``source`` mode instead binds the actual
TLSF bytes to a pinned capability through the existing SYFCO lowering and
never reads experiment outcome or census columns.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import resource
import signal
import subprocess
import sys
import tempfile
import time
import traceback
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from request import (
    CAPABILITIES,
    EXACT_GAME,
    REAL_PROPOSAL,
    SOUND_ONE_SIDED,
    BindingDeclined,
    Capability,
    LoweringTools,
    SourceRequest,
    bind_source_request,
)
from tool_config import (
    add_configuration_arguments,
    configuration_from_args,
    print_probe,
)


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
GENERALIZER = HERE / "generalize_gr1.py"
M0_INSTANCES = HERE / "m0-instances.tsv"
M0_CENSUS = HERE / "m0-census.tsv"

EXIT_CODES = {"REALIZABLE": 0, "UNREALIZABLE": 1, "UNKNOWN": 2}
SOLVED = frozenset(("REALIZABLE", "UNREALIZABLE"))
TOKEN = re.compile(r"[^A-Za-z0-9_.-]+")


class InvocationCancelled(RuntimeError):
    pass


class PipelineFailure(RuntimeError):
    def __init__(self, stage: str, reason: str):
        super().__init__(f"{stage}: {reason}")
        self.stage = stage
        self.reason = reason


@dataclass(frozen=True)
class Deadline:
    started: float
    expires: float

    @classmethod
    def start(cls, seconds: float, started: float | None = None) -> "Deadline":
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError("budget must be finite and positive")
        now = time.monotonic() if started is None else started
        return cls(now, now + seconds)

    def elapsed_s(self) -> float:
        return max(0.0, time.monotonic() - self.started)

    def remaining_s(self) -> float:
        return max(0.0, self.expires - time.monotonic())


@dataclass(frozen=True)
class ProcessOutcome:
    command: tuple[str, ...]
    returncode: int | None
    stdout: str
    stderr: str
    elapsed_s: float
    timed_out: bool
    pid: int


@dataclass(frozen=True)
class Request:
    family: str
    target: int
    logical_instance: str | None
    status_120s: str | None
    census_class: str | None
    spec: Capability
    seeds: tuple[int, ...]
    request_mode: str
    source_request: SourceRequest | None = None


@dataclass(frozen=True)
class PipelineResult:
    verdict: str
    stage: str
    reason: str
    certificate: Path | None = None
    policy: Path | None = None

    def __post_init__(self) -> None:
        if self.verdict not in EXIT_CODES:
            raise ValueError(f"unsupported verdict {self.verdict}")
        if self.verdict in SOLVED and (self.certificate is None or self.policy is None):
            raise ValueError("a decisive result requires checked target artifacts")

    @classmethod
    def unknown(cls, stage: str, reason: str) -> "PipelineResult":
        return cls("UNKNOWN", _token(stage), _token(reason))

    def stdout_line(self) -> str:
        if self.verdict == "UNKNOWN":
            return f"UNKNOWN {self.stage} {self.reason}"
        return f"{self.verdict} {self.certificate}"


def _token(value: str) -> str:
    return TOKEN.sub("_", value.strip()).strip("_") or "unspecified"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def _terminate_group(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        proc.communicate()
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        proc.communicate(timeout=0.5)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.communicate()


def run_process(command: list[str], deadline: Deadline, env: dict[str, str] | None = None) -> ProcessOutcome:
    remaining = deadline.remaining_s()
    if remaining <= 0:
        raise PipelineFailure("deadline", "budget_exhausted_before_stage")
    started = time.monotonic()
    proc = subprocess.Popen(
        command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, start_new_session=True,
    )
    try:
        remaining = min(remaining - (time.monotonic() - started), deadline.remaining_s())
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, 0)
        stdout, stderr = proc.communicate(timeout=remaining)
        return ProcessOutcome(tuple(command), proc.returncode, stdout, stderr,
                              time.monotonic() - started, False, proc.pid)
    except subprocess.TimeoutExpired as error:
        _terminate_group(proc)
        stdout, stderr = error.stdout or "", error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        return ProcessOutcome(tuple(command), proc.returncode, stdout, stderr,
                              time.monotonic() - started, True, proc.pid)
    except BaseException:
        _terminate_group(proc)
        raise


def _record_process(evidence: dict[str, Any], stage: str, outcome: ProcessOutcome) -> None:
    evidence["stages"].append({
        "stage": stage, "command": list(outcome.command), "pid": outcome.pid,
        "returncode": outcome.returncode, "elapsed_s": outcome.elapsed_s,
        "timed_out": outcome.timed_out, "stdout": outcome.stdout,
        "stderr": outcome.stderr,
    })


def _seeds(args: argparse.Namespace, spec: Capability) -> tuple[int, ...]:
    return (
        tuple(int(item) for item in args.seeds.split(",") if item)
        if args.seeds is not None
        else spec.default_seeds
    )


def _validate_route(
    spec: Capability, target: int, seeds: tuple[int, ...], semantics: str
) -> None:
    if spec.route_kind in (REAL_PROPOSAL, SOUND_ONE_SIDED) and (
        not seeds or target <= max(seeds)
    ):
        raise PipelineFailure("eligibility", "target_must_exceed_every_seed")
    if spec.route_kind == EXACT_GAME and semantics != "exact":
        raise PipelineFailure("eligibility", "exact_game_requires_exact_reduction")


def _resolve_reproducer_request(args: argparse.Namespace) -> Request:
    """Historical dataset reproducer: basename/row lookup is intentional here."""
    instance_rows = _read_tsv(args.instances.resolve())
    selected: list[dict[str, str]] = []
    tlsf_name = args.tlsf.name if args.tlsf else None
    for row in instance_rows:
        params = json.loads(row["parameters"])
        if set(params) != {"n"}:
            continue
        if args.family is not None and row["family_display"] != args.family:
            continue
        if args.target is not None and params["n"] != args.target:
            continue
        if tlsf_name is not None and Path(row["tlsf"]).name != tlsf_name:
            continue
        selected.append(row)
    if len(selected) != 1:
        raise PipelineFailure("eligibility", "target_not_unique_in_m0_instances")
    row = selected[0]
    family = row["family_display"]
    spec = CAPABILITIES.get(family)
    if spec is None:
        raise PipelineFailure("eligibility", "family_outside_measured_or_m5_scope")
    target = int(json.loads(row["parameters"])["n"])
    seeds = _seeds(args, spec)
    _validate_route(spec, target, seeds, args.semantics)
    census = {item["family"]: item for item in _read_tsv(args.census.resolve())}
    if family not in census:
        raise PipelineFailure("eligibility", "census_family_missing")
    census_class = f"{census[family]['max_assume']}/{census[family]['max_guarantee']}"
    return Request(
        family, target, row["logical_instance"], row["status_120s"],
        census_class, spec, seeds, "reproducer",
    )


def _resolve_source_request(
    args: argparse.Namespace, config: Any
) -> Request:
    if args.tlsf is None:
        raise PipelineFailure("source_binding", "source_mode_requires_tlsf")
    tools = LoweringTools(config.tlsf2tlsf, config.tlsf2ltl, config.tlsfinfo)
    try:
        source = bind_source_request(
            args.tlsf,
            tools,
            args.semantics,
            family_hint=args.family,
            target_hint=args.target,
        )
    except BindingDeclined as error:
        raise PipelineFailure("source_binding", error.code) from error
    target = source.target_size
    seeds = _seeds(args, source.capability)
    _validate_route(source.capability, target, seeds, args.semantics)
    return Request(
        source.family,
        target,
        None,
        None,
        None,
        source.capability,
        seeds,
        "source",
        source,
    )


def _resolve_request(args: argparse.Namespace, config: Any | None = None) -> Request:
    if getattr(args, "request_mode", "reproducer") == "source":
        if config is None:
            config = configuration_from_args(args)
        return _resolve_source_request(args, config)
    return _resolve_reproducer_request(args)


def _create_workspace(output_root: Path, family: str, target: int) -> tuple[str, Path]:
    output_root.mkdir(parents=True, exist_ok=True)
    invocation = uuid.uuid4().hex
    workspace = Path(tempfile.mkdtemp(
        prefix=f"{_token(family)}-n{target}-{invocation[:12]}-", dir=output_root,
    )).resolve()
    return invocation, workspace


def _load_json(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"JSON object expected in {path}")
    return payload


def _progress_stage(evidence: dict[str, Any], fallback: str) -> str:
    progress = evidence.get("generalizer_progress") or {}
    if progress.get("active_stage"):
        return str(progress["active_stage"])
    costs = progress.get("cost_times") or {}
    if costs.get("target_check") or costs.get("target_solve"):
        return "target_check"
    if costs.get("probe_check") or costs.get("stage_instantiate"):
        # A completed candidate/probe followed by an absolute timeout means
        # the unreported subprocess is the target checker.
        return "target_check"
    if costs.get("stage_ranks"):
        return "instantiate"
    if costs.get("stage_anti_unify"):
        return "ranks"
    return fallback


def _scaled_solver_nodes(n: int) -> int:
    return 1 << min(27, 25 + max(0, (n - 1) // 4))


def _scaled_checker_nodes(n: int) -> int:
    return 1 << min(26, 24 + max(0, (n - 1) // 5))


def _certified_answer(
    capability: Capability,
    cert_meta: dict[str, Any],
    policy_meta: dict[str, Any],
) -> tuple[str, str]:
    """Derive an answer from checked artifacts, never from the capability."""
    answers = {
        "realizable": ("REALIZABLE", "system"),
        "unrealizable": ("UNREALIZABLE", "environment"),
    }
    answer_side = answers.get(cert_meta.get("status"))
    if answer_side is None:
        raise PipelineFailure("target_check", "certificate_status_invalid")
    answer, side = answer_side
    if capability.route_kind in (REAL_PROPOSAL, SOUND_ONE_SIDED) and (
        answer != "REALIZABLE"
    ):
        raise PipelineFailure("target_check", "one_sided_route_returned_unreal")

    if side == "system":
        metadata_ok = (
            cert_meta.get("side") in (None, side)
            and policy_meta.get("side") in (None, side)
        )
    else:
        metadata_ok = (
            cert_meta.get("side") == side
            and cert_meta.get("reduction_semantics") == "exact"
            and cert_meta.get("environment_counter_strategy_exported") is True
            and policy_meta.get("side") == side
            and policy_meta.get("reduction_semantics") == "exact"
        )
    if not metadata_ok:
        raise PipelineFailure("target_check", f"{side}_certificate_metadata_invalid")
    return answer, side


def _generalizer_pipeline(request: Request, workspace: Path, deadline: Deadline,
                          evidence: dict[str, Any], args: argparse.Namespace) -> PipelineResult:
    result_tsv = workspace / "generalizer-result.tsv"
    progress_json = workspace / "generalizer-progress.json"
    output = workspace / "generalizer"
    internal_timeout = max(0.1, deadline.remaining_s() - 0.5)
    command = [
        str(args.generalizer.resolve()), "--family", request.family,
        "--target", str(request.target), "--seeds", ",".join(map(str, request.seeds)),
        "--timeout", f"{internal_timeout:.6f}", "--check-method", "auto",
        "--reduction-semantics", args.semantics,
        "--out", str(output),
        "--tlsf-tools-build", str(args.tlsf_tools_build.resolve()),
        "--bindings-python", str(args.bindings_python.resolve()),
        "--bindings-site", str(args.bindings_site.resolve()),
        "--monitor", str(args.monitor.resolve()),
        "--solver", str(args.solver.resolve()),
        "--checker", str(args.checker.resolve()),
    ]
    if request.source_request is not None:
        try:
            request.source_request.validate_current()
            bound_source = request.source_request.materialize(
                workspace / "requested-source.tlsf"
            )
            bound_template = request.source_request.materialize_template(
                workspace / "capability-template.tlsf"
            )
        except BindingDeclined as error:
            raise PipelineFailure("source_binding", error.code) from error
        command.extend((
            "--target-source", str(bound_source),
            "--family-source", str(bound_template),
        ))
    env = dict(os.environ)
    env["GENERALIZE_GR1_RESULTS"] = str(result_tsv)
    env["GENERALIZE_GR1_PROGRESS"] = str(progress_json)
    env["GENERALIZE_GR1_TRACE"] = "1"
    try:
        outcome = run_process(command, deadline, env)
    finally:
        # A cgroup OOM stop sends SIGTERM to the wrapper while its child is
        # active.  Preserve the child's last atomic progress record before
        # propagating that cancellation.
        if progress_json.is_file():
            evidence["generalizer_progress"] = _load_json(progress_json)
    _record_process(evidence, "generalize_gr1", outcome)
    if outcome.timed_out:
        stage = _progress_stage(evidence, "generalize_gr1")
        evidence["target_check_ran"] = stage == "target_check"
        raise PipelineFailure(stage, "absolute_deadline_exhausted")
    rows = _read_tsv(result_tsv) if result_tsv.is_file() else []
    evidence["generalizer_result"] = rows[0] if len(rows) == 1 else None
    generalizer_evidence = output / "evidence.json"
    if generalizer_evidence.is_file():
        evidence["generalizer_evidence"] = _load_json(generalizer_evidence)
    if outcome.returncode != 0 or len(rows) != 1 or rows[0].get("verdict") != "VERIFIED":
        stage = rows[0].get("stage_reached", "generalize_gr1") if rows else "generalize_gr1"
        reason = rows[0].get("reason", f"exit_{outcome.returncode}") if rows else f"exit_{outcome.returncode}"
        if stage == "CEGIS" and "tlsfcertcheck" in reason:
            stage = "target_check"
        evidence["target_check_ran"] = stage == "target_check"
        raise PipelineFailure(stage, reason)

    match_cert = re.search(r"^certificate:\s*(.+)$", outcome.stdout, re.MULTILINE)
    match_policy = re.search(r"^policy:\s*(.+)$", outcome.stdout, re.MULTILINE)
    if match_cert is None or match_policy is None:
        raise PipelineFailure("target_check", "verified_output_missing_artifacts")
    certificate = Path(match_cert.group(1)).resolve()
    policy = Path(match_policy.group(1)).resolve()
    check_path = output / f"check-target-{request.target}.json"
    if not all(path.is_file() for path in (certificate, policy, check_path,
                                           Path(str(certificate) + ".json"),
                                           Path(str(policy) + ".json"))):
        raise PipelineFailure("target_check", "verified_artifact_missing")
    check = _load_json(check_path)
    cert_meta = _load_json(Path(str(certificate) + ".json"))
    policy_meta = _load_json(Path(str(policy) + ".json"))
    certificate_method = check.get("methods", {}).get("certificate", {})
    if (check.get("verdict") != "VERIFIED" or check.get("exit_code") != 0 or
            certificate_method.get("verdict") != "VERIFIED"):
        raise PipelineFailure("target_check", "tlsfcertcheck_not_verified")
    # Generalized M4 system artifacts predate M5's explicit ``side`` field;
    # environment artifacts must carry the complete exact-reduction metadata.
    answer, expected_side = _certified_answer(request.spec, cert_meta, policy_meta)
    evidence["target_check"] = check
    evidence["target_certificate"] = {
        "path": str(certificate), "sha256": _sha256(certificate),
        "side": expected_side, "verdict": "VERIFIED", "checker": str(args.checker),
    }
    if request.source_request is not None:
        evidence["target_certificate"]["source_binding"] = (
            request.source_request.artifact_binding()
        )
    return PipelineResult(answer, "target_check",
                          "target_verified", certificate, policy)


def _cost_accounting(evidence: dict[str, Any], elapsed_s: float) -> dict[str, float]:
    processes = {item["stage"]: float(item["elapsed_s"]) for item in evidence["stages"]}
    row = evidence.get("generalizer_result") or {}
    if row:
        seed_monitor = float(row.get("seed_monitor_s") or 0)
        seed_solve = float(row.get("seed_solve_s") or 0)
        target_monitor = float(row.get("target_monitor_s") or 0)
        target_solve = float(row.get("target_solve_s") or 0)
        target_check = float(row.get("target_check_s") or 0)
        probe_check = float(row.get("probe_check_s") or 0)
        instantiation = float(row.get("instantiate_s") or 0)
        canonicalize = float(row.get("canonicalize_s") or 0)
        generalization = max(0.0, canonicalize - target_monitor) + sum(
            float(row.get(name) or 0)
            for name in ("anti_unify_s", "bus_schemas_s", "ranks_s")
        )
    else:
        costs = (evidence.get("generalizer_progress") or {}).get("cost_times", {})
        seed_monitor = float(costs.get("seed_monitor_game", 0))
        seed_solve = float(costs.get("seed_solve", 0))
        target_monitor = float(costs.get("target_monitor_game", 0))
        target_solve = float(costs.get("target_solve", 0))
        target_check = float(costs.get("target_check", 0))
        probe_check = float(costs.get("probe_check", 0))
        instantiation = float(costs.get("stage_instantiate", 0))
        canonicalize = float(costs.get("stage_canonicalize", 0))
        generalization = max(0.0, canonicalize - target_monitor) + sum(
            float(costs.get(name, 0))
            for name in ("stage_anti_unify", "stage_bus_schemas", "stage_ranks")
        )
    child_wall = processes.get("generalize_gr1", 0.0)
    accounted_child = (seed_monitor + seed_solve + target_monitor + target_solve +
                       target_check + probe_check + instantiation + generalization)
    driver_overhead = max(0.0, child_wall - accounted_child)
    charged = (seed_monitor + seed_solve + generalization + instantiation +
               target_monitor + target_solve + target_check + probe_check +
               driver_overhead)
    return {
        "seed_monitor_s": seed_monitor, "seed_solve_s": seed_solve,
        "generalization_s": generalization, "instantiation_s": instantiation,
        "target_monitor_s": target_monitor, "target_solve_s": target_solve,
        "target_check_s": target_check, "probe_check_s": probe_check,
        "driver_overhead_s": driver_overhead,
        "wrapper_overhead_s": max(0.0, elapsed_s - charged),
        "cold_total_s": elapsed_s,
    }


def _atomic_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--request-mode", choices=("reproducer", "source"), default="reproducer",
        help=("reproducer uses historical M0 row lookup; source content-binds "
              "the actual -T input without experiment tables"),
    )
    parser.add_argument("--family")
    parser.add_argument("--target", type=int)
    parser.add_argument("-T", "--tlsf", type=Path)
    parser.add_argument("--seeds", help="comma-separated seed sizes (REAL path only)")
    parser.add_argument("--semantics", choices=("exact", "strict"), default="exact")
    parser.add_argument("--budget", type=float, default=120.0)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--evidence-out", type=Path)
    parser.add_argument("--instances", type=Path, default=M0_INSTANCES)
    parser.add_argument("--census", type=Path, default=M0_CENSUS)
    parser.add_argument("--generalizer", type=Path, default=GENERALIZER)
    parser.add_argument("--monitor", type=Path)
    parser.add_argument("--solver", type=Path)
    parser.add_argument("--checker", type=Path)
    add_configuration_arguments(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    invocation_started = time.monotonic()
    parser = _parser()
    args = parser.parse_args(argv)
    config = configuration_from_args(args)
    args.monitor = (args.monitor or config.monitor).resolve()
    args.solver = (args.solver or config.solver).resolve()
    args.checker = (args.checker or config.checker).resolve()
    if args.probe:
        return print_probe(
            config,
            monitor=args.monitor,
            solver=args.solver,
            checker=args.checker,
        )
    try:
        deadline = Deadline.start(args.budget, invocation_started)
    except ValueError:
        print("UNKNOWN configuration invalid_budget")
        return EXIT_CODES["UNKNOWN"]
    output_root = (args.output_dir or Path(tempfile.mkdtemp(prefix="param-lift-campaign-"))).resolve()
    provisional = uuid.uuid4().hex
    workspace = output_root
    evidence_path = args.evidence_out.resolve() if args.evidence_out else None
    evidence: dict[str, Any] = {
        "schema_version": 1, "tool": "param-lift-campaign",
        "invocation_id": provisional, "cache_mode": "cold",
        "warm_cache_supported": False, "budget_s": args.budget,
        "semantics": args.semantics, "stages": [],
        "request_mode": args.request_mode,
        "target_check_ran": False, "target_verified": False,
    }
    result = PipelineResult.unknown("configuration", "uninitialized")
    previous: dict[int, Any] = {}

    def cancel(signum: int, _frame: Any) -> None:
        raise InvocationCancelled(signal.Signals(signum).name)

    for handled in (signal.SIGINT, signal.SIGTERM):
        previous[handled] = signal.getsignal(handled)
        signal.signal(handled, cancel)
    try:
        request = _resolve_request(args, config)
        invocation, workspace = _create_workspace(output_root, request.family, request.target)
        evidence["invocation_id"] = invocation
        evidence["workspace"] = str(workspace)
        evidence_path = evidence_path or workspace / "evidence.json"
        evidence["request"] = {
            "family": request.family, "target": request.target,
            "measured_arity": request.spec.arity,
            "seeds": list(request.seeds),
            "route_kind": request.spec.route_kind,
            "request_mode": request.request_mode,
        }
        if request.source_request is not None:
            evidence["source_binding"] = request.source_request.evidence()
        else:
            evidence["request"].update({
                "logical_instance": request.logical_instance,
                "status_120s": request.status_120s,
                "census_class": request.census_class,
            })
            evidence["source_binding"] = {
                "kind": "historical-dataset-reproducer",
                "match": "basename-to-m0-instances-row",
                "instances": str(args.instances.resolve()),
                "census": str(args.census.resolve()),
            }
        required = [args.checker, args.generalizer]
        evidence["standalone_command"] = [
            str(args.generalizer.resolve()), "--family", request.family,
            "--target", str(request.target), "--seeds", ",".join(map(str, request.seeds)),
            "--reduction-semantics", args.semantics,
            "--timeout", str(args.budget), "--out", "FRESH_OUTPUT_DIRECTORY",
            "--tlsf-tools-build", str(args.tlsf_tools_build.resolve()),
            "--bindings-python", str(args.bindings_python.resolve()),
            "--bindings-site", str(args.bindings_site.resolve()),
            "--monitor", str(args.monitor), "--solver", str(args.solver),
            "--checker", str(args.checker),
        ]
        if request.source_request is not None:
            evidence["standalone_command"].extend(
                (
                    "--target-source", "CONTENT_BOUND_REQUEST_SOURCE.tlsf",
                    "--family-source", "CONTENT_BOUND_CAPABILITY_TEMPLATE.tlsf",
                )
            )
        evidence["tool_configuration"] = {
            "tlsf_tools_build": str(args.tlsf_tools_build.resolve()),
            "bindings_python": str(args.bindings_python.resolve()),
            "bindings_site": str(args.bindings_site.resolve()),
            "monitor": str(args.monitor), "solver": str(args.solver),
            "checker": str(args.checker),
        }
        if request.spec.route_kind == REAL_PROPOSAL:
            evidence["path_kind"] = "lifted"
        elif request.spec.route_kind == EXACT_GAME:
            evidence["path_kind"] = "direct-certified"
        else:
            evidence["path_kind"] = "sound-one-sided"
        missing = [str(path) for path in required if not path.resolve().is_file()]
        if missing:
            raise PipelineFailure("configuration", "missing_tool_" + "_".join(map(_token, missing)))
        result = _generalizer_pipeline(request, workspace, deadline, evidence, args)
        evidence["target_check_ran"] = True
        evidence["target_verified"] = True
    except PipelineFailure as error:
        result = PipelineResult.unknown(error.stage, error.reason)
    except InvocationCancelled as error:
        evidence["cancellation"] = str(error)
        stage = _progress_stage(evidence, "cancellation")
        evidence["target_check_ran"] = stage == "target_check"
        result = PipelineResult.unknown(stage, "interrupted")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        evidence["error"] = {"type": type(error).__name__, "message": str(error),
                             "traceback": traceback.format_exc()}
        result = PipelineResult.unknown("internal_error", type(error).__name__.lower())
    finally:
        for handled, handler in previous.items():
            signal.signal(handled, handler)

    elapsed = deadline.elapsed_s()
    if result.verdict in SOLVED and deadline.remaining_s() <= 0:
        result = PipelineResult.unknown("finalization", "budget_exhausted_before_verdict")
    usage = resource.getrusage(resource.RUSAGE_SELF)
    children = resource.getrusage(resource.RUSAGE_CHILDREN)
    evidence["elapsed_s"] = elapsed
    evidence["peak_rss_kib"] = max(usage.ru_maxrss, children.ru_maxrss)
    evidence["cost_accounting"] = _cost_accounting(evidence, elapsed)
    evidence["result"] = {
        "verdict": result.verdict, "stage": result.stage, "reason": result.reason,
        "certificate": str(result.certificate) if result.certificate else None,
        "policy": str(result.policy) if result.policy else None,
        "stdout_line": result.stdout_line(),
    }
    if evidence_path is None:
        workspace.mkdir(parents=True, exist_ok=True)
        evidence_path = workspace / "evidence.json"
    evidence["evidence_path"] = str(evidence_path)
    try:
        _atomic_json(evidence_path, evidence)
    except OSError as error:
        print(f"evidence write failed: {error}", file=sys.stderr)
        result = PipelineResult.unknown("evidence", "evidence_write_failed")
    print(result.stdout_line())
    return EXIT_CODES[result.verdict]


if __name__ == "__main__":
    raise SystemExit(main())
