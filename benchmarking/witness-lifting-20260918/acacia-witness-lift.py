#!/usr/bin/env python3
"""Cold, bounded research wrapper for checked witness lifting (W7).

The wrapper is intentionally outside the production solver.  One invocation:

* validates an exact selected-family/target mapping before translation;
* expands two manifest-selected seeds, a sanity instance, and the target;
* synthesizes and independently checks two fresh seed witnesses;
* calls the landed proposer's bounded discovery and instantiation code;
* checks candidate instances sequentially at both seeds, sanity, and target; and
* prints a decisive verdict only after the requested target instance is VERIFIED.

All subprocesses share one monotonic absolute deadline.  They are sequential,
run in their own process groups, and are killed and reaped on timeout or
cancellation.  When this command is launched by the coverage runner's existing
systemd scope, the wrapper and every descendant also inherit that one invocation
memory scope.  Exit codes deliberately match the runner's Acacia convention:
REALIZABLE=0, UNREALIZABLE=1, UNKNOWN=2.

This is cold-path research orchestration only.  The optional warm/family-
amortized cache from the plan is explicitly out of scope: every invocation gets
a new workspace, synthesizes both seeds afresh, and shares no process-global or
on-disk lookup cache with another invocation.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
import traceback
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path
from types import ModuleType
from typing import Any, Callable, TypeVar


ROOT = Path(__file__).resolve().parents[2]
SPRINT_DIR = Path(__file__).resolve().parent
DEFAULT_MANIFEST = SPRINT_DIR / "families" / "selected.json"
PROPOSER_PATH = SPRINT_DIR / "families" / "proposals" / "propose_schema.py"
DEFAULT_ACACIA = ROOT / "build_otf_sparse_formula" / "src" / "acacia-bonsai"
DEFAULT_TLSF2TLSF = ROOT / "subprojects" / "tlsf-tools" / "build_nospot" / "tlsf2tlsf"
DEFAULT_TLSF2LTL = ROOT / "subprojects" / "tlsf-tools" / "build_nospot" / "tlsf2ltl"
DEFAULT_VERIFY_AIGER = ROOT / "subprojects" / "tlsf-tools" / "scripts" / "verify_aiger_ltl.py"
DEFAULT_VERIFY_CONJUNCTS = SPRINT_DIR / "families" / "proposals" / "verify_conjuncts.py"

EXIT_CODES = {"REALIZABLE": 0, "UNREALIZABLE": 1, "UNKNOWN": 2}
SUPPORTED_SCHEMA_ID = "pending-flags+cyclic-index/v1"
SUPPORTED_INPUT_ROLE = "r"
SUPPORTED_OUTPUT_ROLE = "g"
_TARGET_NAME = re.compile(r"^(?P<name>[^\s()]+\.ltl)(?:\s|$)")
_TOKEN = re.compile(r"[^A-Za-z0-9_.-]+")
_T = TypeVar("_T")


@dataclass(frozen=True)
class BudgetProfile:
    """The only stage-budget configuration point.

    The two profiles mirror plan section 10.2.  The target checker receives
    whatever remains of the total invocation budget rather than a fresh cap.
    ``small_checks_s`` is one shared allowance covering fresh seed checks and
    candidate checks at the two seeds plus the third small (sanity) instance.
    """

    name: str
    seed_synthesis_s: float
    small_checks_s: float
    schema_proposal_s: float

    @classmethod
    def for_invocation(cls, total_s: float, requested: str = "auto") -> BudgetProfile:
        profiles = {
            "17": cls("17s", 1.0, 1.0, 1.0),
            "120": cls("120s", 5.0, 5.0, 5.0),
        }
        if requested == "auto":
            return profiles["17" if total_s <= 17.0 else "120"]
        return profiles[requested]


@dataclass
class SharedAllowance:
    total_s: float
    used_s: float = 0.0

    @property
    def remaining_s(self) -> float:
        return max(0.0, self.total_s - self.used_s)

    def charge(self, elapsed_s: float) -> None:
        self.used_s += max(0.0, elapsed_s)


class BudgetExhausted(RuntimeError):
    def __init__(self, stage: str, when: str = "before"):
        super().__init__(f"budget exhausted {when} {stage}")
        self.stage = stage
        self.when = when


class StageTimedOut(RuntimeError):
    def __init__(self, stage: str):
        super().__init__(f"stage timed out: {stage}")
        self.stage = stage


class InvocationCancelled(RuntimeError):
    pass


class Ineligible(RuntimeError):
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
    clock: Callable[[], float] = field(compare=False, repr=False)

    @classmethod
    def start(cls, total_s: float, clock: Callable[[], float] = time.monotonic) -> Deadline:
        if not math.isfinite(total_s) or not total_s > 0:
            raise ValueError("total budget must be positive")
        started = clock()
        return cls(started, started + total_s, clock)

    def elapsed_s(self) -> float:
        return max(0.0, self.clock() - self.started)

    def remaining_s(self) -> float:
        return max(0.0, self.expires - self.clock())

    def timeout_for(self, stage: str, stage_limit_s: float | None = None) -> tuple[float, str]:
        remaining = self.remaining_s()
        if remaining <= 0.0:
            raise BudgetExhausted(stage, "before")
        if stage_limit_s is not None:
            if stage_limit_s <= 0.0:
                raise StageTimedOut(stage)
            if stage_limit_s < remaining:
                return stage_limit_s, "stage"
        return remaining, "invocation"


@dataclass(frozen=True)
class ProcessOutcome:
    command: tuple[str, ...]
    returncode: int | None
    stdout: str
    stderr: str
    elapsed_s: float
    timed_out: bool
    timeout_kind: str | None
    pid: int


@dataclass(frozen=True)
class TargetCertificate:
    logical_instance: str
    parameters: dict[str, int]
    artifact_path: Path
    artifact_sha256: str
    checker: str
    checker_mode: str
    verdict: str
    deciding_stage: str


@dataclass(frozen=True)
class PipelineResult:
    verdict: str
    stage: str
    reason: str
    witness_path: Path | None = None
    target_certificate: TargetCertificate | None = None

    def __post_init__(self) -> None:
        if self.verdict not in EXIT_CODES:
            raise ValueError(f"unsupported verdict {self.verdict!r}")
        if self.verdict == "UNKNOWN":
            if self.witness_path is not None or self.target_certificate is not None:
                raise ValueError("UNKNOWN may not carry a deciding witness/certificate")
            return
        certificate = self.target_certificate
        if certificate is None or certificate.verdict != "VERIFIED":
            raise ValueError("a decisive verdict requires a VERIFIED target certificate")
        if self.witness_path is None or self.witness_path != certificate.artifact_path:
            raise ValueError("decisive witness must be the independently checked target artifact")

    @classmethod
    def unknown(cls, stage: str, reason: str) -> PipelineResult:
        return cls("UNKNOWN", _token(stage), _token(reason))

    @classmethod
    def decisive(
        cls, verdict: str, certificate: TargetCertificate
    ) -> PipelineResult:
        return cls(
            verdict,
            certificate.deciding_stage,
            "target_verified",
            certificate.artifact_path,
            certificate,
        )

    def stdout_line(self) -> str:
        if self.verdict == "UNKNOWN":
            return f"UNKNOWN {self.stage} {self.reason}"
        return f"{self.verdict} {self.witness_path}"


@dataclass(frozen=True)
class FamilyRequest:
    manifest_path: Path
    entry: dict[str, Any]
    family_id: str
    target_logical_instance: str
    template_path: Path
    seeds: tuple[int, int]
    sanity: int
    target: int
    designated_verdict: str
    input_role: str = SUPPORTED_INPUT_ROLE
    output_role: str = SUPPORTED_OUTPUT_ROLE


@dataclass(frozen=True)
class ToolPaths:
    acacia: Path
    tlsf2tlsf: Path
    tlsf2ltl: Path
    verify_aiger_ltl: Path
    verify_conjuncts: Path
    checker_python: Path
    checker_library_path: str | None = None


@dataclass(frozen=True)
class PipelineConfig:
    request: FamilyRequest
    tools: ToolPaths
    workspace: Path
    deadline: Deadline
    profile: BudgetProfile
    max_candidates: int


class Evidence:
    def __init__(self, invocation_id: str, budget_s: float, profile: BudgetProfile):
        self.data: dict[str, Any] = {
            "schema_version": 1,
            "tool": "acacia-witness-lift",
            "invocation_id": invocation_id,
            "cache_mode": "cold",
            "warm_cache_supported": False,
            "budget_s": budget_s,
            "budget_profile": asdict(profile),
            "stages": [],
            "target_check_ran": False,
            "target_verified": False,
        }

    def stage(self, name: str, **values: Any) -> None:
        self.data["stages"].append({"stage": name, **_jsonable(values)})


def _jsonable(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, tuple):
        return [_jsonable(item) for item in value]
    if isinstance(value, list):
        return [_jsonable(item) for item in value]
    if isinstance(value, dict):
        return {str(key): _jsonable(item) for key, item in value.items()}
    return value


def _token(value: str) -> str:
    token = _TOKEN.sub("_", value.strip()).strip("_")
    return token or "unspecified"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_proposer() -> ModuleType:
    module_name = "witness_lifting_propose_schema_for_wrapper"
    if module_name in sys.modules:
        return sys.modules[module_name]
    spec = importlib.util.spec_from_file_location(module_name, PROPOSER_PATH)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load proposer from {PROPOSER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def _terminate_process_group(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        proc.communicate()
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        proc.communicate(timeout=0.2)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.communicate()


def run_process(
    command: list[str] | tuple[str, ...],
    deadline: Deadline,
    stage: str,
    stage_limit_s: float | None = None,
    env: dict[str, str] | None = None,
) -> ProcessOutcome:
    """Run one bounded command, killing and reaping its entire process group."""
    timeout_s, timeout_kind = deadline.timeout_for(stage, stage_limit_s)
    started = deadline.clock()
    proc = subprocess.Popen(
        [str(part) for part in command],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
        env=env,
    )
    # Process creation and interpreter startup are part of the same absolute
    # budget.  Do not hand communicate() the pre-spawn allowance afresh.
    timeout_s = min(timeout_s - (deadline.clock() - started), deadline.remaining_s())
    if timeout_s <= 0.0:
        _terminate_process_group(proc)
        return ProcessOutcome(
            tuple(str(part) for part in command),
            proc.returncode,
            "",
            "",
            deadline.clock() - started,
            True,
            timeout_kind,
            proc.pid,
        )
    try:
        stdout, stderr = proc.communicate(timeout=timeout_s)
        return ProcessOutcome(
            tuple(str(part) for part in command),
            proc.returncode,
            stdout,
            stderr,
            deadline.clock() - started,
            False,
            None,
            proc.pid,
        )
    except subprocess.TimeoutExpired as error:
        _terminate_process_group(proc)
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        return ProcessOutcome(
            tuple(str(part) for part in command),
            proc.returncode,
            stdout,
            stderr,
            deadline.clock() - started,
            True,
            timeout_kind,
            proc.pid,
        )
    except BaseException:
        _terminate_process_group(proc)
        raise


def run_callable(
    function: Callable[[], _T],
    deadline: Deadline,
    stage: str,
    stage_limit_s: float | None = None,
) -> tuple[_T, float]:
    """Bound an in-process proposer/instantiator with the same deadline."""
    timeout_s, timeout_kind = deadline.timeout_for(stage, stage_limit_s)
    started = deadline.clock()
    timeout_s = min(timeout_s - (deadline.clock() - started), deadline.remaining_s())
    if timeout_s <= 0.0:
        raise BudgetExhausted(stage, "before")
    if not hasattr(signal, "setitimer"):
        value = function()
        elapsed = deadline.clock() - started
        if elapsed >= timeout_s:
            if timeout_kind == "invocation":
                raise BudgetExhausted(stage, "during")
            raise StageTimedOut(stage)
        return value, elapsed

    previous_handler = signal.getsignal(signal.SIGALRM)

    def alarm_handler(_signum: int, _frame: Any) -> None:
        if timeout_kind == "invocation":
            raise BudgetExhausted(stage, "during")
        raise StageTimedOut(stage)

    signal.signal(signal.SIGALRM, alarm_handler)
    signal.setitimer(signal.ITIMER_REAL, timeout_s)
    try:
        value = function()
        return value, deadline.clock() - started
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0.0)
        signal.signal(signal.SIGALRM, previous_handler)


def _target_name(entry: dict[str, Any]) -> str:
    raw = entry.get("target_logical_instance")
    if not isinstance(raw, str):
        raise Ineligible("manifest target_logical_instance is absent")
    match = _TARGET_NAME.match(raw)
    if match is None:
        raise Ineligible("manifest target_logical_instance is ambiguous")
    return match.group("name")


def _single_n(mapping: Any, label: str) -> int:
    if not isinstance(mapping, dict) or set(mapping) != {"n"}:
        raise Ineligible(f"{label} must contain exactly the supported n parameter")
    value = mapping["n"]
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise Ineligible(f"{label}.n must be a positive integer")
    return value


def _read_selected_entries(manifest_path: Path) -> list[dict[str, Any]]:
    try:
        payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PipelineFailure("configuration", "manifest_unreadable") from error
    if payload.get("schema_version") != 1 or not isinstance(payload.get("families"), list):
        raise PipelineFailure("configuration", "manifest_schema_invalid")
    return payload["families"]


def resolve_family_request(
    manifest_path: Path,
    family_id: str | None,
    target: str | None,
    target_tlsf: Path | None,
) -> FamilyRequest:
    entries = _read_selected_entries(manifest_path)
    inferred_target = target
    if target_tlsf is not None:
        if not target_tlsf.is_file():
            raise PipelineFailure("configuration", "target_tlsf_missing")
        from_path = target_tlsf.with_suffix(".ltl").name
        if inferred_target is not None and inferred_target != from_path:
            raise PipelineFailure("eligibility", "target_identity_mismatch")
        inferred_target = from_path

    matches = []
    for entry in entries:
        if family_id is not None and entry.get("family_id") != family_id:
            continue
        try:
            entry_target = _target_name(entry)
        except Ineligible:
            continue
        if inferred_target is not None and entry_target != inferred_target:
            continue
        matches.append(entry)
    if len(matches) != 1:
        reason = "family_target_not_selected" if not matches else "family_target_ambiguous"
        raise PipelineFailure("eligibility", reason)
    entry = matches[0]
    selected_family_id = entry.get("family_id")
    if not isinstance(selected_family_id, str):
        raise PipelineFailure("eligibility", "family_id_invalid")
    selected_target = _target_name(entry)

    varying = entry.get("varying_parameters")
    if varying != ["n"] or entry.get("fixed_parameters") != {}:
        raise Ineligible("only one varying n parameter with no hidden fixed parameters is supported")
    seeds_raw = entry.get("seeds")
    if not isinstance(seeds_raw, list) or len(seeds_raw) < 2:
        raise Ineligible("two manifest seeds are required")
    seeds = tuple(_single_n(value, "seed") for value in seeds_raw[:2])
    if seeds[0] == seeds[1]:
        raise Ineligible("seed parameters must be distinct")
    sanity = _single_n(entry.get("sanity_parameter"), "sanity_parameter")
    target_n = _single_n(entry.get("target_parameters"), "target_parameters")

    role = entry.get("role")
    if not isinstance(role, str) or "UNREAL" in role.upper():
        # The landed schema emits a system controller.  The existing controller
        # checker cannot certify an environment witness, so guessing here would
        # violate the wrapper's decisive-verdict invariant.
        raise Ineligible("supported schema class is a REAL-controller grammar")

    template_source = entry.get("template_source")
    if not isinstance(template_source, str):
        raise Ineligible("template_source is missing")
    sprint_root = manifest_path.resolve().parent.parent
    template_path = (sprint_root / template_source).resolve()
    if not template_path.is_relative_to(sprint_root) or not template_path.is_file():
        raise Ineligible("template_source does not resolve inside the sprint package")
    expected_digest = entry.get("template_sha256")
    if not isinstance(expected_digest, str) or _sha256(template_path) != expected_digest:
        raise Ineligible("template digest does not match the exact selected source")

    text = template_path.read_text(encoding="utf-8")
    semantics = re.search(r"\bSEMANTICS\s*:\s*(Mealy|Moore)\b", text, re.IGNORECASE)
    target_semantics = re.search(r"\bTARGET\s*:\s*(Mealy|Moore)\b", text, re.IGNORECASE)
    original = entry.get("original_semantics")
    effective = entry.get("effective_move_order")
    if semantics is None or target_semantics is None or original not in {"Mealy", "Moore"}:
        raise Ineligible("only declared Mealy/Moore infinite-word semantics are supported")
    if semantics.group(1).lower() != str(original).lower():
        raise Ineligible("manifest/source semantic mismatch")
    if not isinstance(effective, str) or not effective.lower().startswith(str(original).lower()):
        raise Ineligible("effective move order is not the declared supported semantics")
    if target_semantics.group(1).lower() != str(original).lower():
        raise Ineligible("SEMANTICS/TARGET move orders differ")

    input_decl = re.search(r"\bINPUTS\s*\{(?P<body>[^}]*)\}", text, re.DOTALL)
    output_decl = re.search(r"\bOUTPUTS\s*\{(?P<body>[^}]*)\}", text, re.DOTALL)
    if input_decl is None or output_decl is None:
        raise Ineligible("template has no unambiguous input/output declarations")
    if re.search(r"\br\s*\[\s*n\s*\]", input_decl.group("body")) is None:
        raise Ineligible("supported indexed input role r[n] was not found")
    if re.search(r"\bg\s*\[\s*n\s*\]", output_decl.group("body")) is None:
        raise Ineligible("supported indexed output role g[n] was not found")
    provenance = entry.get("signal_origin_map")
    if not isinstance(provenance, str) or not all(
        token in provenance for token in ("r_0", "g_0")
    ):
        raise Ineligible("exact indexed signal provenance is not declared")

    return FamilyRequest(
        manifest_path=manifest_path.resolve(),
        entry=entry,
        family_id=selected_family_id,
        target_logical_instance=selected_target,
        template_path=template_path,
        seeds=(seeds[0], seeds[1]),
        sanity=sanity,
        target=target_n,
        designated_verdict="REALIZABLE",
    )


def create_cold_workspace(output_root: Path, family_id: str) -> tuple[str, Path]:
    output_root.mkdir(parents=True, exist_ok=True)
    invocation_id = uuid.uuid4().hex
    family_slug = _token(family_id.split("/")[-1].removesuffix(".tlsf"))
    workspace = Path(
        tempfile.mkdtemp(
            prefix=f"{family_slug}-{invocation_id[:12]}-",
            dir=output_root,
        )
    ).resolve()
    return invocation_id, workspace


def _record_process(evidence: Evidence, stage: str, outcome: ProcessOutcome, **extra: Any) -> None:
    evidence.stage(
        stage,
        command=outcome.command,
        returncode=outcome.returncode,
        elapsed_s=outcome.elapsed_s,
        timed_out=outcome.timed_out,
        timeout_kind=outcome.timeout_kind,
        stdout=outcome.stdout,
        stderr=outcome.stderr,
        **extra,
    )


def _timeout_failure(stage: str, outcome: ProcessOutcome) -> PipelineFailure:
    if outcome.timeout_kind == "invocation":
        return PipelineFailure(stage, f"budget_exhausted_during_{stage}")
    return PipelineFailure(stage, f"stage_timeout_{stage}")


def _instantiate_tlsf(
    parameter: int,
    role: str,
    config: PipelineConfig,
    evidence: Evidence,
) -> Path:
    stage = f"instantiate_{role}_n{parameter}"
    output = config.workspace / "instances" / f"{role}_n{parameter}.tlsf"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(config.tools.tlsf2tlsf),
        "--param",
        f"n={parameter}",
        "--basic",
        str(config.request.template_path),
    ]
    outcome = run_process(command, config.deadline, stage)
    _record_process(evidence, stage, outcome, parameter=parameter, role=role)
    if outcome.timed_out:
        raise _timeout_failure(stage, outcome)
    if outcome.returncode != 0 or not outcome.stdout.strip():
        raise PipelineFailure(stage, "tlsf_instantiation_failed")
    output.write_text(outcome.stdout, encoding="utf-8")
    evidence.data.setdefault("instances", {})[role] = {
        "parameter": parameter,
        "path": str(output),
        "sha256": _sha256(output),
    }
    return output


def _synthesize_seed(
    parameter: int,
    tlsf_path: Path,
    config: PipelineConfig,
    evidence: Evidence,
) -> Path:
    stage = f"seed_synthesis_n{parameter}"
    output = config.workspace / "fresh-seeds" / f"seed_n{parameter}.aag"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(config.tools.acacia), "-T", str(tlsf_path), "-s", str(output)]
    outcome = run_process(
        command,
        config.deadline,
        stage,
        config.profile.seed_synthesis_s,
    )
    _record_process(evidence, stage, outcome, parameter=parameter, artifact=output)
    if outcome.timed_out:
        raise _timeout_failure(stage, outcome)
    lines = (outcome.stdout + "\n" + outcome.stderr).splitlines()
    printed_realizable = any(line.strip() == "REALIZABLE" for line in lines)
    if outcome.returncode != 0 or not printed_realizable or not output.is_file():
        raise PipelineFailure(stage, "seed_synthesis_not_realizable")
    evidence.data.setdefault("fresh_seeds", []).append(
        {
            "parameter": parameter,
            "aiger_path": str(output),
            "aiger_sha256": _sha256(output),
            "source_tlsf": str(tlsf_path),
        }
    )
    return output


def _checker_command(
    proposer: ModuleType,
    config: PipelineConfig,
    parameter: int,
    role: str,
    tlsf_path: Path,
    artifact_path: Path,
    mode: str,
) -> tuple[str, ...]:
    family = proposer.Family(
        family_id=config.request.family_id,
        input_role=config.request.input_role,
        output_role=config.request.output_role,
        checks=(),
        output_dir=config.workspace / "candidates",
        verify_aiger_ltl=config.tools.verify_aiger_ltl,
        verify_conjuncts=config.tools.verify_conjuncts,
        tlsf2ltl=config.tools.tlsf2ltl,
    )
    case = proposer.CheckCase(parameter, role, tlsf_path, mode)
    # This is the proposer's existing checker-selection logic, including its
    # exact monolithic/decomposed modes; the wrapper only supplies the deadline.
    selected = proposer._checker_command(family, case, artifact_path)
    return (str(config.tools.checker_python), *selected[1:])


def _check_artifact(
    proposer: ModuleType,
    artifact_path: Path,
    parameter: int,
    role: str,
    tlsf_path: Path,
    mode: str,
    config: PipelineConfig,
    evidence: Evidence,
    stage: str,
    allowance: SharedAllowance | None,
) -> tuple[str, ProcessOutcome]:
    command = _checker_command(
        proposer, config, parameter, role, tlsf_path, artifact_path, mode
    )
    stage_limit = allowance.remaining_s if allowance is not None else None
    checker_env = None
    if config.tools.checker_library_path is not None:
        checker_env = dict(os.environ)
        checker_env["LD_LIBRARY_PATH"] = config.tools.checker_library_path
    outcome = run_process(
        command,
        config.deadline,
        stage,
        stage_limit,
        env=checker_env,
    )
    if allowance is not None:
        allowance.charge(outcome.elapsed_s)
    verdict = (
        "VERIFIED"
        if not outcome.timed_out and outcome.returncode == 0
        else "REFUTED"
        if not outcome.timed_out and outcome.returncode == 1
        else "UNKNOWN"
    )
    _record_process(
        evidence,
        stage,
        outcome,
        parameter=parameter,
        role=role,
        checker_mode=mode,
        artifact=artifact_path,
        artifact_sha256=_sha256(artifact_path),
        verdict=verdict,
        shared_small_check_remaining_s=(allowance.remaining_s if allowance else None),
    )
    return verdict, outcome


def run_pipeline(config: PipelineConfig, evidence: Evidence) -> PipelineResult:
    request = config.request
    proposer = _load_proposer()
    tools = asdict(config.tools)
    required_tools = {
        name: path for name, path in tools.items() if name != "checker_library_path"
    }
    missing = [name for name, path in required_tools.items() if not Path(path).is_file()]
    if missing:
        return PipelineResult.unknown("configuration", "tool_missing_" + "_".join(missing))

    evidence.data["tools"] = {
        name: (
            str(path)
            if name == "checker_library_path"
            else {"path": str(path), "sha256": _sha256(Path(path))}
        )
        for name, path in tools.items()
        if path is not None
    }

    evidence.data["family"] = {
        "family_id": request.family_id,
        "manifest": str(request.manifest_path),
        "template": str(request.template_path),
        "template_sha256": _sha256(request.template_path),
        "seeds": list(request.seeds),
        "sanity": request.sanity,
        "target_parameters": {"n": request.target},
        "target_logical_instance": request.target_logical_instance,
        "designated_verdict": request.designated_verdict,
        "supported_schema_id": SUPPORTED_SCHEMA_ID,
    }
    evidence.stage("eligibility", verdict="ELIGIBLE", elapsed_s=config.deadline.elapsed_s())

    instance_paths: dict[int, Path] = {}
    roles = [
        (request.seeds[0], "seed_1"),
        (request.seeds[1], "seed_2"),
        (request.sanity, "sanity"),
        (request.target, "target"),
    ]
    for parameter, role in roles:
        # Reuse only inside this invocation when two roles name the exact same
        # complete parameter environment.
        if parameter not in instance_paths:
            instance_paths[parameter] = _instantiate_tlsf(
                parameter, role, config, evidence
            )

    fresh_seed_paths = []
    for parameter in request.seeds:
        fresh_seed_paths.append(
            _synthesize_seed(parameter, instance_paths[parameter], config, evidence)
        )

    small_allowance = SharedAllowance(config.profile.small_checks_s)
    checked_seeds = []
    for parameter, aiger_path in zip(request.seeds, fresh_seed_paths, strict=True):
        stage = f"seed_verification_n{parameter}"
        verdict, outcome = _check_artifact(
            proposer,
            aiger_path,
            parameter,
            "fresh seed verification",
            instance_paths[parameter],
            "monolithic",
            config,
            evidence,
            stage,
            small_allowance,
        )
        if outcome.timed_out:
            raise _timeout_failure(stage, outcome)
        if verdict != "VERIFIED":
            raise PipelineFailure(stage, "fresh_seed_not_verified")
        checked_seeds.append(
            proposer.CheckedSeed(parameter, aiger_path, instance_paths[parameter], True)
        )

        if parameter == request.target:
            certificate = TargetCertificate(
                request.target_logical_instance,
                {"n": request.target},
                aiger_path,
                _sha256(aiger_path),
                Path(outcome.command[1]).name,
                "monolithic",
                "VERIFIED",
                stage,
            )
            evidence.data["target_check_ran"] = True
            evidence.data["target_verified"] = True
            evidence.data["target_certificate"] = _jsonable(asdict(certificate))
            return PipelineResult.decisive(request.designated_verdict, certificate)

    limits = proposer.ProposerLimits(max_candidates=config.max_candidates)
    discovered, proposal_elapsed = run_callable(
        lambda: proposer.discover_candidate_schemas(
            checked_seeds[0],
            checked_seeds[1],
            request.input_role,
            request.output_role,
            limits,
        ),
        config.deadline,
        "schema_proposal",
        config.profile.schema_proposal_s,
    )
    evidence.stage(
        "schema_proposal",
        elapsed_s=proposal_elapsed,
        candidate_count=len(discovered),
        candidates=[
            {
                "schema_id": candidate.schema_id,
                "proposal_origin": candidate.proposal_origin,
                "description": candidate.description(),
                "influenced_by": list(candidate.influenced_by),
            }
            for candidate in discovered
        ],
    )
    if not discovered:
        return PipelineResult.unknown("schema_proposal", "no_recognized_candidate")

    candidate_unknown: PipelineResult | None = None
    for candidate_index, candidate in enumerate(discovered, start=1):
        if candidate.schema_id != SUPPORTED_SCHEMA_ID:
            continue
        small_ok = True
        checked_parameters: set[int] = set()
        target_from_small: TargetCertificate | None = None
        for parameter, role in (
            (request.seeds[0], "candidate_seed_1"),
            (request.seeds[1], "candidate_seed_2"),
            (request.sanity, "candidate_sanity"),
        ):
            if parameter in checked_parameters:
                continue
            checked_parameters.add(parameter)
            instantiate_stage = f"candidate_{candidate_index}_instantiate_n{parameter}"
            artifact = (
                config.workspace
                / "candidates"
                / f"candidate_{candidate_index}_n{parameter}.aag"
            )
            artifact.parent.mkdir(parents=True, exist_ok=True)
            artifact_text, elapsed = run_callable(
                lambda candidate=candidate, parameter=parameter: proposer.instantiate(
                    candidate, parameter
                ),
                config.deadline,
                instantiate_stage,
            )
            artifact.write_text(artifact_text, encoding="utf-8")
            evidence.stage(
                instantiate_stage,
                elapsed_s=elapsed,
                artifact=artifact,
                artifact_sha256=_sha256(artifact),
            )
            check_stage = f"candidate_{candidate_index}_{role}_check_n{parameter}"
            verdict, outcome = _check_artifact(
                proposer,
                artifact,
                parameter,
                role,
                instance_paths[parameter],
                "monolithic",
                config,
                evidence,
                check_stage,
                small_allowance,
            )
            if outcome.timed_out:
                candidate_unknown = PipelineResult.unknown(
                    check_stage,
                    (
                        f"budget_exhausted_during_{check_stage}"
                        if outcome.timeout_kind == "invocation"
                        else f"stage_timeout_{check_stage}"
                    ),
                )
                small_ok = False
                break
            if verdict != "VERIFIED":
                small_ok = False
                break
            if parameter == request.target:
                target_from_small = TargetCertificate(
                    request.target_logical_instance,
                    {"n": request.target},
                    artifact,
                    _sha256(artifact),
                    Path(outcome.command[1]).name,
                    "monolithic",
                    "VERIFIED",
                    check_stage,
                )
        if candidate_unknown is not None:
            return candidate_unknown
        if not small_ok:
            continue
        if target_from_small is not None:
            evidence.data["target_check_ran"] = True
            evidence.data["target_verified"] = True
            evidence.data["target_certificate"] = _jsonable(asdict(target_from_small))
            return PipelineResult.decisive(request.designated_verdict, target_from_small)

        target_stage = f"candidate_{candidate_index}_target_verification"
        target_artifact = (
            config.workspace
            / "candidates"
            / f"candidate_{candidate_index}_target_n{request.target}.aag"
        )
        target_text, elapsed = run_callable(
            lambda candidate=candidate: proposer.instantiate(candidate, request.target),
            config.deadline,
            f"candidate_{candidate_index}_target_instantiation",
        )
        target_artifact.write_text(target_text, encoding="utf-8")
        evidence.stage(
            f"candidate_{candidate_index}_target_instantiation",
            elapsed_s=elapsed,
            artifact=target_artifact,
            artifact_sha256=_sha256(target_artifact),
        )
        evidence.data["target_check_ran"] = True
        verdict, outcome = _check_artifact(
            proposer,
            target_artifact,
            request.target,
            f"target {request.target_logical_instance}",
            instance_paths[request.target],
            "exact conjunct decomposition",
            config,
            evidence,
            target_stage,
            None,
        )
        if verdict == "VERIFIED":
            certificate = TargetCertificate(
                request.target_logical_instance,
                {"n": request.target},
                target_artifact,
                _sha256(target_artifact),
                Path(outcome.command[1]).name,
                "exact conjunct decomposition",
                "VERIFIED",
                target_stage,
            )
            evidence.data["target_verified"] = True
            evidence.data["target_certificate"] = _jsonable(asdict(certificate))
            return PipelineResult.decisive(request.designated_verdict, certificate)
        if outcome.timed_out:
            reason = (
                f"budget_exhausted_during_{target_stage}"
                if outcome.timeout_kind == "invocation"
                else f"stage_timeout_{target_stage}"
            )
            return PipelineResult.unknown(target_stage, reason)
        if verdict == "UNKNOWN":
            return PipelineResult.unknown(target_stage, "target_checker_error")
        # A refuted candidate is discarded; another bounded candidate may still
        # be checked, but no refutation is ever promoted to an UNREAL verdict.

    return PipelineResult.unknown("candidate_checks", "no_candidate_verified_at_target")


def _write_evidence(path: Path, evidence: Evidence) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    temporary.write_text(
        json.dumps(evidence.data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--family-id", help="exact family_id from selected.json")
    parser.add_argument("--target", help="exact target_logical_instance (.ltl)")
    parser.add_argument(
        "-T",
        "--tlsf",
        type=Path,
        help=(
            "coverage-runner adapter: identify the selected target from the original "
            "target TLSF basename; may replace --family-id/--target"
        ),
    )
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--budget", type=float, default=120.0)
    parser.add_argument("--profile", choices=("auto", "17", "120"), default="auto")
    parser.add_argument("--max-candidates", type=int, default=32)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--evidence-out", type=Path)
    parser.add_argument("--acacia", type=Path, default=DEFAULT_ACACIA)
    parser.add_argument("--tlsf2tlsf", type=Path, default=DEFAULT_TLSF2TLSF)
    parser.add_argument("--tlsf2ltl", type=Path, default=DEFAULT_TLSF2LTL)
    parser.add_argument("--verify-aiger-ltl", type=Path, default=DEFAULT_VERIFY_AIGER)
    parser.add_argument("--verify-conjuncts", type=Path, default=DEFAULT_VERIFY_CONJUNCTS)
    parser.add_argument(
        "--checker-python",
        type=Path,
        default=Path(sys.executable),
        help="Python interpreter with compatible Spot bindings (default: this interpreter)",
    )
    parser.add_argument(
        "--checker-library-path",
        help=(
            "optional LD_LIBRARY_PATH applied only to checker subprocesses; "
            "never applied to Acacia or tlsf2tlsf"
        ),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    invocation_started = time.monotonic()
    args = _parser().parse_args(argv)
    try:
        profile = BudgetProfile.for_invocation(args.budget, args.profile)
        if not math.isfinite(args.budget) or not args.budget > 0:
            raise ValueError("total budget must be positive")
        deadline = Deadline(
            invocation_started,
            invocation_started + args.budget,
            time.monotonic,
        )
    except (KeyError, ValueError):
        result = PipelineResult.unknown("configuration", "invalid_budget")
        print(result.stdout_line())
        return EXIT_CODES[result.verdict]

    output_root = args.output_dir
    if output_root is None:
        output_root = Path(tempfile.mkdtemp(prefix="acacia-witness-lift-output-"))
    output_root = output_root.resolve()
    try:
        output_root.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        result = PipelineResult.unknown("configuration", "output_directory_unavailable")
        print(f"output directory unavailable: {error}", file=sys.stderr)
        print(result.stdout_line())
        return EXIT_CODES[result.verdict]

    provisional_id = uuid.uuid4().hex
    evidence = Evidence(provisional_id, args.budget, profile)
    workspace = output_root.resolve()
    evidence_path = args.evidence_out.resolve() if args.evidence_out else None
    result: PipelineResult

    previous_handlers: dict[int, Any] = {}

    def cancellation_handler(signum: int, _frame: Any) -> None:
        raise InvocationCancelled(signal.Signals(signum).name)

    for handled in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[handled] = signal.getsignal(handled)
        signal.signal(handled, cancellation_handler)

    try:
        request = resolve_family_request(
            args.manifest.resolve(), args.family_id, args.target, args.tlsf
        )
        invocation_id, workspace = create_cold_workspace(output_root, request.family_id)
        evidence.data["invocation_id"] = invocation_id
        evidence.data["workspace"] = str(workspace)
        evidence_path = evidence_path or (workspace / "evidence.json")
        config = PipelineConfig(
            request=request,
            tools=ToolPaths(
                args.acacia.resolve(),
                args.tlsf2tlsf.resolve(),
                args.tlsf2ltl.resolve(),
                args.verify_aiger_ltl.resolve(),
                args.verify_conjuncts.resolve(),
                args.checker_python.resolve(),
                args.checker_library_path,
            ),
            workspace=workspace,
            deadline=deadline,
            profile=profile,
            max_candidates=args.max_candidates,
        )
        result = run_pipeline(config, evidence)
    except Ineligible as error:
        evidence.stage("eligibility", verdict="INELIGIBLE", detail=str(error))
        result = PipelineResult.unknown("eligibility", "ineligible")
    except BudgetExhausted as error:
        reason = f"budget_exhausted_{error.when}_{error.stage}"
        result = PipelineResult.unknown(error.stage, reason)
    except StageTimedOut as error:
        result = PipelineResult.unknown(error.stage, f"stage_timeout_{error.stage}")
    except PipelineFailure as error:
        result = PipelineResult.unknown(error.stage, error.reason)
    except InvocationCancelled as error:
        evidence.data["cancellation"] = str(error)
        result = PipelineResult.unknown("cancellation", "interrupted")
    except (OSError, ValueError, ImportError) as error:
        evidence.data["error"] = {
            "type": type(error).__name__,
            "message": str(error),
            "traceback": traceback.format_exc(),
        }
        result = PipelineResult.unknown("internal_error", type(error).__name__.lower())
    finally:
        for handled, previous in previous_handlers.items():
            signal.signal(handled, previous)

    if result.verdict != "UNKNOWN" and deadline.remaining_s() <= 0.0:
        result = PipelineResult.unknown(
            "finalization", "budget_exhausted_before_verdict"
        )
    evidence.data["elapsed_s"] = deadline.elapsed_s()
    evidence.data["result"] = {
        "verdict": result.verdict,
        "stage": result.stage,
        "reason": result.reason,
        "witness_path": str(result.witness_path) if result.witness_path else None,
        "stdout_line": result.stdout_line(),
    }
    if result.target_certificate is not None:
        evidence.data["target_certificate"] = _jsonable(asdict(result.target_certificate))

    if evidence_path is None:
        # Resolution can fail before a family workspace exists; still persist a
        # sidecar in a fresh cold directory under the caller-selected root.
        fallback_id = evidence.data["invocation_id"]
        workspace = Path(
            tempfile.mkdtemp(prefix=f"unresolved-{fallback_id[:12]}-", dir=output_root)
        ).resolve()
        evidence.data["workspace"] = str(workspace)
        evidence_path = workspace / "evidence.json"
    evidence.data["evidence_path"] = str(evidence_path)
    try:
        _write_evidence(evidence_path, evidence)
        if result.verdict != "UNKNOWN" and deadline.remaining_s() <= 0.0:
            # Sidecar serialization is part of the invocation too.  Preserve
            # the verified target evidence, but do not claim an over-budget
            # decisive result on stdout.
            result = PipelineResult.unknown(
                "finalization", "budget_exhausted_before_verdict"
            )
            evidence.data["elapsed_s"] = deadline.elapsed_s()
            evidence.data["result"] = {
                "verdict": result.verdict,
                "stage": result.stage,
                "reason": result.reason,
                "witness_path": None,
                "stdout_line": result.stdout_line(),
            }
            _write_evidence(evidence_path, evidence)
    except OSError as error:
        # A requested evidence sidecar is part of this research protocol.  Do
        # not emit a decisive answer if its evidence could not be persisted.
        result = PipelineResult.unknown("evidence", "evidence_write_failed")
        print(f"evidence write failed: {error}", file=sys.stderr)

    print(result.stdout_line())
    return EXIT_CODES[result.verdict]


if __name__ == "__main__":
    raise SystemExit(main())
