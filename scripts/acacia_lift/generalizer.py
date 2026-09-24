#!/usr/bin/env python3
"""Index-aware, bounded GR(1) certificate generalizer.

This is a research driver, not part of tlsf-tools.  It deliberately treats the
monitor provenance as the cross-instance ABI: AIG variable numbers are never
used to align two sizes.

Lifted REAL targets use the synthesis-capable policy proof route by default.
``--real-check region`` is decision-only: it emits the complete target
certificate but no target policy and accepts only a ``REGION_VERIFIED`` proof.
"""

from __future__ import annotations

if __package__ in (None, ""):
    import pathlib as _pathlib
    import sys as _sys
    _sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parents[1]))

import argparse
import csv
import dataclasses
import itertools
import json
import math
import os
import pathlib
import re
import resource
import signal
import subprocess
import sys
import time
from collections import Counter, OrderedDict, defaultdict
from collections.abc import Iterable

from acacia_lift.buddy_veccompose import (
    BUDDY_MAX_VARIABLE_COUNT,
    BuddyVariableAdapter,
    BuddyVeccomposeAdapter,
)
from acacia_lift.capabilities import CAPABILITIES, EXACT_GAME, REAL_PROPOSAL
from acacia_lift.diagnostics import Diagnostics, sha256
from acacia_lift.artifact import (Aag, AagBuilder, _certificate_sidecar,
                                  _policy_sidecar, _aag_vector_evaluate)
from acacia_lift.bdd_kernel import (VarInfo, OwnerIndex,
                                    VariableBlock as VariableBlock, VariableLayout)
from acacia_lift.instantiate import (
    _normal_key, _ordered_subset, _structured_move_literals as _structured_move_literals,
    _structured_arbiter_policy_literals, _structured_arbiter_certificate_moves,
)
from acacia_lift.tools import (
    ProbeError,
    ToolConfiguration,
    add_configuration_arguments,
    bindings_environment,
    configuration_defaults,
    configuration_from_args,
    load_buddy_bindings,
    print_probe,
    require_checker_method,
)


ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = ROOT / "benchmarking" / "param-lift-20260922"
TOOL_CONFIG = configuration_defaults()
TT = TOOL_CONFIG.tlsf_tools_root
MONITOR = TOOL_CONFIG.monitor
SOLVER = TOOL_CONFIG.solver
CHECKER = TOOL_CONFIG.checker
BINDINGS_PYTHON = TOOL_CONFIG.bindings_python
BINDINGS_SITE = TOOL_CONFIG.bindings_site
ALIGNMENT = HERE / "m4-alignment.tsv"
SEPARABILITY = HERE / "m4-invariant-separability.tsv"
MOVE_SEPARABILITY = HERE / "m4-move-separability.tsv"
RESULTS = pathlib.Path(os.environ.get(
    "GENERALIZE_GR1_RESULTS", HERE / "m4-results.tsv"))
MAX_CANDIDATES_PER_TARGET = 32
MAX_CEGIS_ROUNDS = 3
# These schemas are an in-invocation parent/child handoff, not a persisted
# public interface.  They were deliberately bumped for the region route:
# request identity now binds the REAL check route, and a region bundle omits
# the policy records that remain mandatory in a default policy bundle.
CANDIDATE_BUNDLE_SCHEMA = "acacia-gr1-candidate-bundle-v3"
CANDIDATE_REQUEST_SCHEMA = "acacia-gr1-candidate-request-v2"
PROVENANCE_SCHEMA = "tlsf-tools.gr1-monitor-game.provenance.v2"
MAX_PREDICATE_ARITY = 4
TEMPLATE_CACHE_MAX_ENTRIES = 64
SUBSET_METADATA_MAX_ENTRIES = 256
SUPPORT_CACHE_MAX_ENTRIES = 256
SUPPORT_CACHE_MAX_BYTES = 64 << 20
PROJECTION_METADATA_MAX_BYTES = 64 << 20
CUBE_CACHE_MAX_BYTES = 8 << 20
# sizeof(BddNode) for the identity-validated pinned x86-64 libbddx.  Cache
# accounting is deliberately conservative across roots that share nodes.
BUDDY_NODE_ACCOUNTING_BYTES = 16
# Maximum variable count for the exact pinned libbddx image, validated by the
# binding/native adapter before the manager is initialized.  Valid semantic
# coordinates are therefore [0, BDD_COORDINATE_LIMIT).
BDD_COORDINATE_LIMIT = BUDDY_MAX_VARIABLE_COUNT
HINT_VALIDITY_GROUP = (("global-schema",), ("hint-one-hot",), ())
VERDICTS = {0: "VERIFIED", 1: "REFUTED", 2: "ERROR",
            3: "UNKNOWN", 4: "INVALID", 5: "INTERNAL_ERROR",
            6: "CERT_FAILED"}

_DIAGNOSTICS_ENABLED = False
_DIAGNOSTICS: Diagnostics | None = None
_DIAGNOSTIC_STAGE_TOKENS: dict[str, int | None] = {}
_DIAGNOSTIC_BUDDY: object | None = None
_DIAGNOSTIC_BUDDY_SAMPLES: list[dict[str, object]] = []
_DIAGNOSTIC_BDD_OPERATIONS: dict[str, Counter[str]] = {}
_DIAGNOSTIC_INSTANCES: set[str] = set()
_DIAGNOSTIC_CLIENT_COUNTS: Counter[int] = Counter()
_DIAGNOSTIC_OWNER_ARITIES: Counter[int] = Counter()
_DIAGNOSTIC_SUBSETS: dict[tuple[int, tuple[int, ...]], Counter[str]] = {}
_DIAGNOSTIC_SUPPORTS: list[dict[str, object]] = []
_DIAGNOSTIC_AAG_CONES: list[dict[str, object]] = []
_DIAGNOSTIC_MASK_WORDS: Counter[int] = Counter()
_DIAGNOSTIC_MODES: Counter[int] = Counter()
_CHECKER_STATS_MODE: str | None = None
_BUDDY_MANAGER_LIFETIME: object | None = None
_BUDDY_VARIABLE_COUNT: int | None = None

_DIAGNOSTIC_PHASES = (
    "seed_monitor_construction",
    "seed_solves",
    "target_monitor_construction",
    "target_solve",
    "projection_metadata",
    "bdd_projection_relabel",
    "bdd_existential_quantification",
    "bdd_relabel_rename",
    "support_extraction",
    "variable_cube_construction",
    "substitute_variables",
    "from_aag",
    "instantiate_templates",
    "mode_specialization",
    "policy_construction_skolemization",
    "export",
    "target_check",
)


def _reference_compose_enabled() -> bool:
    return bool(os.environ.get("GENERALIZE_GR1_REFERENCE_COMPOSE"))


def _compose_route() -> str:
    if _reference_compose_enabled() or TOOL_CONFIG.buddy_adapter is None:
        return "two_pass"
    return "native_veccompose"


def _reference_aag_context_enabled() -> bool:
    return bool(os.environ.get("GENERALIZE_GR1_REFERENCE_AAG_CONTEXT"))


class DiagnosticCancelled(RuntimeError):
    """A signal interrupted a diagnostic invocation."""


def _diagnostic_begin(name: str, kind: str = "phase") -> int | None:
    if _DIAGNOSTICS is None:
        return None
    try:
        return _DIAGNOSTICS.begin(name, kind)
    except Exception as error:
        _DIAGNOSTICS.record_error(f"begin:{name}", error)
        return None


def _diagnostic_end(token: int | None) -> float:
    if _DIAGNOSTICS is None or token is None:
        return 0.0
    try:
        return _DIAGNOSTICS.end(token)
    except Exception as error:
        _DIAGNOSTICS.record_error("end", error)
        return 0.0


def _diagnostic_buddy_boundary(boundary: str) -> None:
    if _DIAGNOSTIC_BUDDY is None:
        return
    buddy = _DIAGNOSTIC_BUDDY
    try:
        allocated = int(buddy.bdd_getallocnum())
        used = int(buddy.bdd_getnodenum())
        _DIAGNOSTIC_BUDDY_SAMPLES.append({
            "boundary": boundary,
            "allocated_nodes": allocated,
            "used_nodes": used,
            "gbc_count": None,
            "gbc_count_exposed": False,
        })
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error(f"buddy_boundary:{boundary}", error)


def _diagnostic_bdd_snapshot(bdds: "Bdds") -> dict[str, int] | None:
    if not _DIAGNOSTICS_ENABLED:
        return None
    try:
        if bdds._veccompose is None:
            return None
        stats = bdds._veccompose.stats()
        return {
            "allocated_nodes": stats["nodenum"],
            "used_nodes": stats["nodenum"] - stats["freenodes"],
            "produced_nodes": stats["produced"],
            "gbc_count": stats["gbcnum"],
        }
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error("bdd_operation_snapshot", error)
        return None


def _diagnostic_record_bdd_operation(
    name: str,
    before: dict[str, int] | None,
    after: dict[str, int] | None,
) -> None:
    if before is None or after is None:
        return
    try:
        totals = _DIAGNOSTIC_BDD_OPERATIONS.setdefault(name, Counter())
        totals["calls"] += 1
        for field in (
            "allocated_nodes", "used_nodes", "produced_nodes", "gbc_count"
        ):
            delta = after[field] - before[field]
            totals[f"{field}_net"] += delta
            if delta > 0:
                totals[f"{field}_growth"] += delta
            totals[f"{field}_max_delta"] = max(
                totals[f"{field}_max_delta"], delta
            )
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error(f"bdd_operation_delta:{name}", error)


def _diagnostic_register_instance(instance: "Instance", role: str) -> None:
    try:
        key = str(instance.game_path.resolve())
        if key in _DIAGNOSTIC_INSTANCES:
            return
        _DIAGNOSTIC_INSTANCES.add(key)
        _DIAGNOSTIC_CLIENT_COUNTS[instance.n] += 1
        for variable in instance.variables:
            _DIAGNOSTIC_OWNER_ARITIES[len(variable.owners)] += 1
        if _DIAGNOSTICS is None:
            return
        records = _DIAGNOSTICS.extra.setdefault("instances", [])
        if not isinstance(records, list):
            raise TypeError("diagnostic instances field is not a list")
        records.append({
            "role": role,
            "family": instance.family,
            "clients": instance.n,
            "game": str(instance.game_path),
        })
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error("register_instance", error)


def _diagnostic_record_checker_attempt(
    instance: "Instance", side: str, label: str, node_cap: int
) -> int:
    """Record modes for one checker attempt, after its certificate side is known."""
    try:
        if side == "system":
            modes = len(instance.goals) + 1
        elif side == "environment":
            modes = max(instance.fairness, 1) + 1
        else:
            raise ValueError(f"unsupported certificate side {side!r}")
        _DIAGNOSTIC_MODES[modes] += 1
        if _DIAGNOSTICS is not None:
            attempts = _DIAGNOSTICS.extra.setdefault("checker_mode_attempts", [])
            if not isinstance(attempts, list):
                raise TypeError("checker_mode_attempts field is not a list")
            attempts.append({
                "label": label,
                "side": side,
                "node_cap": node_cap,
                "mode_count": modes,
            })
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error("record_checker_mode", error)
        return 0
    return modes


def _diagnostic_record_subset(
    client_count: int, subset: tuple[int, ...], operation: str
) -> None:
    try:
        key = (client_count, tuple(sorted(subset)))
        uses = _DIAGNOSTIC_SUBSETS.setdefault(key, Counter())
        uses[operation] += 1
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error("record_subset", error)


def _diagnostic_record_support(
    predicate: str, client_count: int, subset: tuple[int, ...], support: set[int]
) -> None:
    try:
        words = 0 if not support else (max(support) // 64) + 1
        _DIAGNOSTIC_SUPPORTS.append({
            "predicate": predicate,
            "clients": client_count,
            "subset": list(subset),
            "width": len(support),
            "uint64_words": words,
            "uint64_mask_bytes": words * 8,
        })
        _DIAGNOSTIC_MASK_WORDS[words] += 1
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error("record_support", error)


def _probe_checker_stats(
    deadline: "AbsoluteDeadline | None" = None,
) -> str | None:
    timeout = 10.0
    if deadline is not None:
        timeout = min(timeout, deadline.remaining_s())
        if timeout <= 0:
            return None
    try:
        probe = subprocess.run(
            [str(CHECKER), "--help"], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False, timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    help_text = f"{probe.stdout}\n{probe.stderr}"
    line = next((item for item in help_text.splitlines()
                 if "--stats" in item), "")
    if not line:
        return None
    if re.search(r"--stats=\S+", line):
        return "file_equals"
    if re.search(r"--stats\s+(FILE|PATH|JSON)\b", line, re.IGNORECASE):
        return "file_separate"
    return "stream"


def _diagnostic_initialize(
    path: pathlib.Path,
    deadline: "AbsoluteDeadline | None" = None,
) -> None:
    global _DIAGNOSTICS_ENABLED, _DIAGNOSTICS, _CHECKER_STATS_MODE
    global _DIAGNOSTIC_BUDDY
    _DIAGNOSTICS_ENABLED = True
    _DIAGNOSTICS = Diagnostics("generalize_gr1", path.resolve())
    _DIAGNOSTIC_STAGE_TOKENS.clear()
    _DIAGNOSTIC_BUDDY = None
    _DIAGNOSTIC_BUDDY_SAMPLES.clear()
    _DIAGNOSTIC_BDD_OPERATIONS.clear()
    _DIAGNOSTIC_INSTANCES.clear()
    _DIAGNOSTIC_CLIENT_COUNTS.clear()
    _DIAGNOSTIC_OWNER_ARITIES.clear()
    _DIAGNOSTIC_SUBSETS.clear()
    _DIAGNOSTIC_SUPPORTS.clear()
    _DIAGNOSTIC_AAG_CONES.clear()
    _DIAGNOSTIC_MASK_WORDS.clear()
    _DIAGNOSTIC_MODES.clear()
    for name in _DIAGNOSTIC_PHASES:
        _DIAGNOSTICS.phases[name] = {"calls": 0, "wall_s": 0.0}
    try:
        _CHECKER_STATS_MODE = _probe_checker_stats(deadline)
        binaries = {}
        for name, path0 in (("tlsfsolve", SOLVER), ("tlsfcertcheck", CHECKER)):
            binaries[name] = {"path": str(path0), "sha256": sha256(path0)}
        binding_modules = sorted(BINDINGS_SITE.glob("buddy.py"))
        extension_modules = sorted(BINDINGS_SITE.glob("_buddy*.so"))
        _DIAGNOSTICS.extra.update({
            "environment": {
                "interpreter": {"executable": sys.executable,
                                "version": sys.version},
                "bindings_site": str(BINDINGS_SITE),
                "compose_route": _compose_route(),
                "buddy_adapter": (str(TOOL_CONFIG.buddy_adapter)
                                  if TOOL_CONFIG.buddy_adapter is not None
                                  else None),
                "binding_module_candidates": [str(path0)
                                              for path0 in binding_modules],
                "extension_module_candidates": [str(path0)
                                                for path0 in extension_modules],
                "tlsf_tools_build": str(TOOL_CONFIG.tlsf_tools_build),
                "binaries": binaries,
            },
            "checker_stats": {"supported": _CHECKER_STATS_MODE is not None,
                              "mode": _CHECKER_STATS_MODE, "attempts": []},
        })
    except Exception as error:
        _CHECKER_STATS_MODE = None
        _DIAGNOSTICS.record_error("initialize", error)


def _diagnostic_finish(status: str, result: dict | None = None) -> None:
    if not _DIAGNOSTICS_ENABLED or _DIAGNOSTICS is None:
        return
    try:
        subsets = []
        for (clients, subset), uses in sorted(_DIAGNOSTIC_SUBSETS.items()):
            subsets.append({
                "clients": clients,
                "subset": list(subset),
                "uses": sum(uses.values()),
                "uses_by_operation": dict(sorted(uses.items())),
            })
        _DIAGNOSTICS.extra.update({
            "bdd_stats": {
                "available": bool(_DIAGNOSTIC_BUDDY_SAMPLES),
                "samples": _DIAGNOSTIC_BUDDY_SAMPLES,
                "operations": {
                    name: dict(values)
                    for name, values in sorted(
                        _DIAGNOSTIC_BDD_OPERATIONS.items()
                    )
                },
            },
            "distributions": {
                "projected_root_support_widths": _DIAGNOSTIC_SUPPORTS,
                "selected_aig_cones": _DIAGNOSTIC_AAG_CONES,
                "client_count_histogram": {
                    str(key): value for key, value in sorted(
                        _DIAGNOSTIC_CLIENT_COUNTS.items())
                },
                "ownership_tuple_arity_histogram": {
                    str(key): value for key, value in sorted(
                        _DIAGNOSTIC_OWNER_ARITIES.items())
                },
                "distinct_subset_count": len(subsets),
                "subset_reuse": subsets,
                "uint64_variable_mask_word_length_histogram": {
                    str(key): value for key, value in sorted(
                        _DIAGNOSTIC_MASK_WORDS.items())
                },
                "mode_count_basis": "actual_checker_attempts",
                "mode_count_histogram": {
                    str(key): value for key, value in sorted(
                        _DIAGNOSTIC_MODES.items())
                },
            },
        })
        outcome = None
        if result is not None:
            outcome = {key: result.get(key) for key in (
                "family", "target", "verdict", "answer", "reason")
                if key in result}
        _DIAGNOSTICS.write(status, outcome=outcome)
    except Exception as error:
        _DIAGNOSTICS.record_error("finish", error)
        _DIAGNOSTICS.write(status)


def _diagnostic_merge_candidate_builder(payload: dict[str, object]) -> None:
    """Merge child-owned measurements without importing any live BDD state."""
    if _DIAGNOSTICS is None:
        return
    try:
        phases = payload.get("phases", {})
        if isinstance(phases, dict):
            for name, record in phases.items():
                if not isinstance(record, dict):
                    continue
                _DIAGNOSTICS.add_phase(
                    str(name), float(record.get("wall_s", 0.0)),
                    int(record.get("calls", 0)),
                )
        counters = payload.get("counters", {})
        if isinstance(counters, dict):
            _DIAGNOSTICS.counters.update({
                str(name): int(value) for name, value in counters.items()
            })
        censored = payload.get("censored", [])
        if isinstance(censored, list):
            _DIAGNOSTICS.censored.extend(
                item for item in censored if isinstance(item, dict)
            )
        _DIAGNOSTICS.extra["candidate_builder"] = payload
    except Exception as error:
        _DIAGNOSTICS.record_error("merge_candidate_builder", error)


def _scaled_solver_nodes(n: int) -> int:
    """Use 32M entries through n=4, doubling every four clients to 128M."""
    exponent = min(27, 25 + max(0, (n - 1) // 4))
    return 1 << exponent


def _scaled_checker_nodes(n: int) -> int:
    """Use 16M entries through n=5, doubling every five clients to 64M."""
    exponent = min(26, 24 + max(0, (n - 1) // 5))
    return 1 << exponent


def _trace(message: str) -> None:
    if os.environ.get("GENERALIZE_GR1_TRACE"):
        print(f"generalize_gr1: {message}", file=sys.stderr, flush=True)


@dataclasses.dataclass(frozen=True)
class ProposerLimits:
    max_candidates: int = MAX_CANDIDATES_PER_TARGET
    checker_timeout_s: float = 120.0
    max_cegis_rounds: int = MAX_CEGIS_ROUNDS
    solver_nodes: int | None = None
    solver_cache: int | None = None
    checker_nodes: int | None = None

    def __post_init__(self) -> None:
        if not 1 <= self.max_candidates <= MAX_CANDIDATES_PER_TARGET:
            raise ValueError(
                f"max_candidates must be in [1, {MAX_CANDIDATES_PER_TARGET}]")
        if self.checker_timeout_s <= 0:
            raise ValueError("checker_timeout_s must be positive")
        if not 0 <= self.max_cegis_rounds <= MAX_CEGIS_ROUNDS:
            raise ValueError(f"max_cegis_rounds must be in [0, {MAX_CEGIS_ROUNDS}]")
        for name in ("solver_nodes", "solver_cache", "checker_nodes"):
            value = getattr(self, name)
            if value is not None and value <= 0:
                raise ValueError(f"{name} must be positive")

    def seed_capacity(self, n: int) -> tuple[int, int]:
        nodes = self.solver_nodes or _scaled_solver_nodes(n)
        cache = self.solver_cache or max(1024, nodes // 4)
        return nodes, cache

    def check_capacity(self, n: int) -> int:
        return self.checker_nodes or _scaled_checker_nodes(n)


@dataclasses.dataclass(frozen=True)
class AbsoluteDeadline:
    """One monotonic deadline shared by every orchestration stage."""

    expires_monotonic_s: float

    @classmethod
    def after(cls, seconds: float) -> "AbsoluteDeadline":
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError("deadline duration must be finite and positive")
        return cls(time.monotonic() + seconds)

    def remaining_s(self) -> float:
        return max(0.0, self.expires_monotonic_s - time.monotonic())

    def timeout_s(
        self, cap_s: float, stage: str, *, reserve_s: float = 0.0
    ) -> float:
        remaining = min(cap_s, self.remaining_s() - reserve_s)
        if remaining <= 0:
            raise Decline(
                stage, "absolute deadline", None,
                "budget exhausted before stage",
            )
        return remaining


@dataclasses.dataclass(frozen=True)
class CandidateSchema:
    family: str
    arity: int
    seeds: tuple[int, ...]
    role_classes: tuple[str, ...]
    bus_schemas: tuple[str, ...]
    template_counts: tuple[tuple[str, int], ...]
    predicate_arities: tuple[tuple[str, int], ...] = ()


@dataclasses.dataclass
class SeedBundle:
    family: str
    instances: dict[int, "Instance"]
    newly_solved: tuple[int, ...]
    elapsed_s: float

    @property
    def seeds(self) -> list["Instance"]:
        return [self.instances[n] for n in sorted(self.instances)]


@dataclasses.dataclass(frozen=True)
class SchemaBundle:
    family: str
    seed_bundle: SeedBundle
    arity: int
    measured_roles: int
    seed_bus_shapes: tuple[frozenset[tuple], ...]


@dataclasses.dataclass(frozen=True)
class CandidateBundle:
    schema: CandidateSchema
    target: "Instance"
    certificate: pathlib.Path
    policy: pathlib.Path | None
    detail: dict


@dataclasses.dataclass(frozen=True)
class CandidateRequestIdentity:
    """Parent-owned identity that a candidate child must reproduce exactly."""

    family: str
    target: int
    seeds: tuple[int, ...]
    reduction_semantics: str
    target_source_sha256: str
    expected_arity: int
    game_path: pathlib.Path
    provenance_path: pathlib.Path
    real_check: str = "policy"

    def payload(self) -> dict[str, object]:
        return {
            "schema": CANDIDATE_REQUEST_SCHEMA,
            "family": self.family,
            "target": self.target,
            "seeds": list(self.seeds),
            "reduction_semantics": self.reduction_semantics,
            "target_source_sha256": self.target_source_sha256,
            "expected_arity": self.expected_arity,
            "game_path": str(self.game_path),
            "provenance_path": str(self.provenance_path),
            "provenance_schema": PROVENANCE_SCHEMA,
            "real_check": self.real_check,
        }


@dataclasses.dataclass(frozen=True)
class TargetCheckResult:
    target_verified: bool
    schema_validated_on_probes: bool
    check: dict


REAL_FAMILIES = {
    family: capability
    for family, capability in CAPABILITIES.items()
    if capability.route_kind == REAL_PROPOSAL
}
EXACT_FAMILIES = {
    family: capability
    for family, capability in CAPABILITIES.items()
    if capability.route_kind == EXACT_GAME
}
OUT_OF_SCOPE = frozenset(
    ("round_robin_arbiter", "lift", "amba_decomposed_arbiter"))
STRUCTURED_GRANT_FAMILIES = frozenset(
    ("arbiter", "arbiter_with_cancel", "arbiter_on_inpchange"))


class Decline(RuntimeError):
    """A named, evidence-bearing UNKNOWN result."""

    def __init__(self, stage: str, predicate: str, n: int | None, reason: str):
        self.stage = stage
        self.predicate = predicate
        self.n = n
        self.reason = reason
        self.times: dict[str, float] = {}
        where = "" if n is None else f" at n={n}"
        super().__init__(f"{stage}: predicate {predicate!r}{where}: {reason}")


_LAST_STAGE_TIMES: dict[str, float] = {}
_ACTIVE_STAGE: tuple[str, float] | None = None
_COST_TIMES: Counter[str] = Counter()


def _set_active_progress_stage(name: str, started: float | None = None) -> None:
    global _ACTIVE_STAGE
    _ACTIVE_STAGE = (name, time.monotonic() if started is None else started)
    _write_cost_progress()


def _clear_active_progress_stage(name: str) -> None:
    global _ACTIVE_STAGE
    if _ACTIVE_STAGE is not None and _ACTIVE_STAGE[0] == name:
        _ACTIVE_STAGE = None
    _write_cost_progress()


def _write_cost_progress() -> None:
    """Persist partial cold-cost evidence for an outer absolute deadline."""
    raw = os.environ.get("GENERALIZE_GR1_PROGRESS")
    if not raw:
        return
    try:
        path = pathlib.Path(raw)
        path.parent.mkdir(parents=True, exist_ok=True)
        now = time.monotonic()
        payload = {
            "sampled_monotonic_s": now,
            "cost_times": dict(_COST_TIMES),
            "last_stage_times": dict(_LAST_STAGE_TIMES),
            "active_stage": (
                _ACTIVE_STAGE[0] if _ACTIVE_STAGE is not None else None),
            "active_stage_started_monotonic_s": (
                _ACTIVE_STAGE[1] if _ACTIVE_STAGE is not None else None
            ),
            "active_stage_elapsed_s": (
                max(0.0, now - _ACTIVE_STAGE[1])
                if _ACTIVE_STAGE is not None else None
            ),
        }
        temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
        temporary.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        temporary.replace(path)
    except Exception as error:
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.record_error("write_cost_progress", error)


class StageTimes(dict[str, float]):
    """Keep completed stage timings available when a later stage declines."""

    def begin(self, key: str) -> float:
        global _ACTIVE_STAGE
        started = time.monotonic()
        _ACTIVE_STAGE = (key, started)
        if _DIAGNOSTICS_ENABLED:
            _DIAGNOSTIC_STAGE_TOKENS[key] = _diagnostic_begin(
                key, "stage")
            _diagnostic_buddy_boundary(f"stage:{key}:begin")
        _write_cost_progress()
        return started

    def __setitem__(self, key: str, value: float) -> None:
        global _ACTIVE_STAGE
        super().__setitem__(key, value)
        _LAST_STAGE_TIMES[key] = value
        _COST_TIMES[f"stage_{key}"] += value
        if _ACTIVE_STAGE is not None and _ACTIVE_STAGE[0] == key:
            _ACTIVE_STAGE = None
        if _DIAGNOSTICS_ENABLED:
            token = _DIAGNOSTIC_STAGE_TOKENS.pop(key, None)
            if token is not None:
                _diagnostic_end(token)
            _diagnostic_buddy_boundary(f"stage:{key}:end")
        _write_cost_progress()






def _read_tsv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def stable_from(family: str) -> int:
    for row in _read_tsv(ALIGNMENT):
        if row["family"] == family and row["stable_from"]:
            return int(row["stable_from"])
    # The explicit regression is not in the fixed-arity scope, but seed
    # validation is intentionally available before the scope decline.
    if family == "round_robin_arbiter_unreal2":
        return 3
    raise Decline("seed", "stable_from", None,
                  f"family {family!r} has no measured stable regime")


def measured_arity(family: str) -> int | None:
    invariant_values = {int(row["min_k"]) for row in _read_tsv(SEPARABILITY)
                        if row["family"] == family and row["min_k"]}
    if len(invariant_values) != 1:
        return None
    invariant = next(iter(invariant_values))
    move_values = {int(row["min_k"]) for row in _read_tsv(MOVE_SEPARABILITY)
                   if row["family"] == family and row["min_k"]}
    # collector_v1 has only its n=3 invariant measurement; its move relation
    # is reconstructed semantically below.  Wherever a move measurement is
    # present it must agree with the invariant measurement.
    return invariant if not move_values or move_values == {invariant} else None


def measured_role_class_count(family: str) -> int | None:
    for row in _read_tsv(ALIGNMENT):
        if row["family"] != family:
            continue
        values = {int(value) for value in row["role_classes"].split(",")
                  if value}
        return next(iter(values)) if len(values) == 1 else None
    return None


def _provenance_role_class_count(instance: "Instance") -> int:
    inventories: dict[tuple[int, ...], Counter] = defaultdict(Counter)
    for monitor in instance.prov["monitors"]:
        if monitor["arity_kind"] == "local" and _hint_schema(monitor) is None:
            inventories[tuple(monitor["index_tuple"])][monitor["template"]] += 1
    return len({tuple(sorted(inventory.items()))
                for inventory in inventories.values()})


def _run(command: list[str], timeout: float, cwd: pathlib.Path = ROOT) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, cwd=cwd, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              check=False, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        return subprocess.CompletedProcess(command, 124, exc.stdout or "", exc.stderr or "timeout")


def build_game(family: str, n: int, directory: pathlib.Path,
               timeout: float, stage: str = "seed", *,
               source: pathlib.Path | None = None,
               reduction_semantics: str = "exact",
               deadline: AbsoluteDeadline | None = None) -> tuple[pathlib.Path, pathlib.Path]:
    spec = REAL_FAMILIES.get(family) or EXACT_FAMILIES[family]
    game = directory / f"{family}_{n}.game.aag"
    prov = directory / f"{family}_{n}.prov.json"
    tlsf = source.resolve() if source is not None else ROOT / spec.source
    command = [str(BINDINGS_PYTHON), str(MONITOR), str(tlsf),
               "--param", f"n={n}", "--semantics", reduction_semantics, "--output",
               str(game), "--provenance-out", str(prov)]
    env = bindings_environment(TOOL_CONFIG)
    effective_timeout = (
        deadline.timeout_s(timeout, stage) if deadline is not None else timeout
    )
    started = time.monotonic()
    diagnostic_token = None
    if _DIAGNOSTICS_ENABLED:
        phase = ("seed_monitor_construction" if stage == "seed"
                 else "target_monitor_construction")
        diagnostic_token = _diagnostic_begin(phase)
    try:
        proc = subprocess.run(command, cwd=ROOT, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              check=False, timeout=effective_timeout)
    except subprocess.TimeoutExpired:
        if _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None:
            _DIAGNOSTICS.censor_active("subprocess_timeout")
        raise Decline(
            stage, "monitor_game", n,
            f"timed out after {effective_timeout:g}s",
        )
    finally:
        key = "seed_monitor_game" if stage == "seed" else "target_monitor_game"
        _COST_TIMES[key] += time.monotonic() - started
        if diagnostic_token is not None:
            _diagnostic_end(diagnostic_token)
        _write_cost_progress()
    if proc.returncode != 0:
        if (proc.returncode == 124 and _DIAGNOSTICS_ENABLED and
                _DIAGNOSTICS is not None):
            _DIAGNOSTICS.censor(
                phase, time.monotonic() - started,
                "subprocess_timeout")
        detail = (proc.stderr or proc.stdout).strip()[-500:]
        raise Decline(stage, "monitor_game", n,
                      f"construction failed with exit {proc.returncode}: {detail}")
    return game, prov


def solve_seed(family: str, n: int, directory: pathlib.Path,
               limits: ProposerLimits, *,
               family_source: pathlib.Path | None = None,
               deadline: AbsoluteDeadline | None = None) -> "Instance":
    game, prov = build_game(
        family, n, directory, limits.checker_timeout_s, source=family_source,
        deadline=deadline)
    cert = directory / f"{family}_{n}.cert.aag"
    policy = directory / f"{family}_{n}.policy.aag"
    nodes, cache = limits.seed_capacity(n)
    command = [str(SOLVER), "--certificate", str(cert),
               "--certificate-json", str(cert) + ".json", "--policy", str(policy),
               "--policy-json", str(policy) + ".json",
               "--oxidd-nodes", str(nodes), "--oxidd-cache", str(cache),
               str(game)]
    started = time.monotonic()
    diagnostic_token = (
        _diagnostic_begin("seed_solves") if _DIAGNOSTICS_ENABLED else None
    )
    try:
        timeout = (
            deadline.timeout_s(limits.checker_timeout_s, "seed")
            if deadline is not None else limits.checker_timeout_s
        )
        proc = _run(command, timeout)
    finally:
        _COST_TIMES["seed_solve"] += time.monotonic() - started
        if diagnostic_token is not None:
            _diagnostic_end(diagnostic_token)
        _write_cost_progress()
    if proc.returncode != 0:
        if (proc.returncode == 124 and _DIAGNOSTICS_ENABLED and
                _DIAGNOSTICS is not None):
            _DIAGNOSTICS.censor(
                "seed_solves", time.monotonic() - started,
                "subprocess_timeout")
        detail = (proc.stderr or proc.stdout).strip()[-500:]
        raise Decline("seed", "tlsfsolve", n,
                      f"seed solve failed with exit {proc.returncode}: {detail}")
    instance = Instance.load(family, n, game, prov, cert)
    if _DIAGNOSTICS_ENABLED:
        _diagnostic_register_instance(instance, "seed")
    return instance


def _monitor_state(name: str) -> tuple[int, int] | None:
    match = re.fullmatch(r"monitor_(\d+)_state_(\d+)", name)
    return (int(match.group(1)), int(match.group(2))) if match else None


def _collapse_indices(text: str) -> str:
    return re.sub(r"_i\d+", "_I", text)


def _route_arity_kind(record: dict, n: int) -> str:
    """Correct whole-bus-at-small-n provenance using syntactic index arity.

    A non-symmetric two-index conjunct is pairwise even when n=2 happens to
    make its support equal the entire bus.  Larger genuinely bus-wide records
    must actually cover every index of at least one support bus.
    """
    indices = frozenset(record.get("index_tuple", ()))
    buses = record.get("support", {}).get("buses", {})
    covers_bus = any(frozenset(members) == frozenset(range(n))
                     for members in buses.values())
    if record.get("arity_kind") == "local":
        return "local"
    if (len(indices) == 2 and not record.get("symmetric") and
            _hint_schema(record) is not None):
        return "local"
    return "bus_wide" if covers_bus else "local"


def _hint_schema(record: dict) -> str | None:
    buses = set(record.get("support", {}).get("buses", {}))
    template = record["template"]
    if buses != {"g", "r"}:
        return None
    if (("<-> r_" in template or
         ("(g_i0 & r_i0)" in template and "(!g_i0 & !r_i0)" in template)) and
            "X(" in template):
        return "HintAgreement(g,r)"
    if re.search(r"!\(r_i0 .*\) \| \(g_i0 & X", template):
        return "HintSequence(g,r)"
    return None


def bus_schema(record: dict, n: int) -> str:
    """Semantically name the intentionally small bus schema library."""
    buses = record["support"]["buses"]
    signature = record.get("symmetric_signature")
    if record.get("symmetric") and isinstance(signature, dict):
        parts = []
        for bus, allowed in sorted(signature.items()):
            if allowed == [0]:
                kind = "NoneOf"
            elif allowed == [0, 1]:
                kind = "AtMostOne"
            elif allowed == list(range(1, n + 1)):
                kind = "AtLeastOne"
            else:
                kind = "Counts{" + ",".join(map(str, allowed)) + "}"
            parts.append(f"{kind}({bus})")
        return " & ".join(parts)
    text = _collapse_indices(record["template"])
    scalars = tuple(record["support"]["scalars"])
    bus_names = tuple(sorted(buses))
    if " U " in text and "g_m" in scalars:
        return f"U(NoneOf({','.join(bus_names)}),g_m)"
    if " W " in text:
        return f"W({','.join(bus_names)};{','.join(scalars)})"
    if "XG(" in text and bus_names:
        return f"X(AtMostOne({','.join(bus_names)}))"
    if (text.startswith("G(!idle | X(") and "idle" in scalars and
            bus_names):
        return f"X(AtLeastOne({','.join(bus_names)}))"
    if text.startswith("G(idle | X(") and bus_names:
        return f"X(NoneOf({','.join(bus_names)}))"
    if "G(G!" in text and "FallFinished" in text:
        return f"W(AllSeen({','.join(bus_names)}),allFinished)"
    hint = _hint_schema(record)
    if hint is not None:
        return hint
    raise Decline("bus-wide schemas", record["template"], n,
                  "unmatched bus-wide conjunct")










@dataclasses.dataclass
class GoalInfo:
    goal: int
    monitor: dict
    owner: int | None
    key: tuple


@dataclasses.dataclass
class Instance:
    family: str
    n: int
    game_path: pathlib.Path
    prov_path: pathlib.Path
    cert_path: pathlib.Path | None
    game: Aag
    prov: dict
    cert: Aag | None
    meta: dict | None
    variables: list[VarInfo]
    goals: list[GoalInfo]
    role_by_client: dict[int, str]
    bus_schemas: tuple[str, ...]

    @classmethod
    def load(cls, family: str, n: int, game_path: pathlib.Path,
             prov_path: pathlib.Path, cert_path: pathlib.Path | None = None) -> "Instance":
        game = Aag.read(game_path)
        prov = json.loads(prov_path.read_text(encoding="utf-8"))
        for record in prov["monitors"]:
            record["reported_arity_kind"] = record["arity_kind"]
            record["arity_kind"] = _route_arity_kind(record, n)
        cert = Aag.read(cert_path) if cert_path else None
        meta = json.loads(pathlib.Path(str(cert_path) + ".json").read_text(
            encoding="utf-8")) if cert_path else None
        monitors = {record["monitor"]: record for record in prov["monitors"]}
        schemas = {mid: bus_schema(record, n) for mid, record in monitors.items()
                   if record["arity_kind"] == "bus_wide"}

        def state_key(record: dict, indices: tuple[int, ...], bit: int
                      ) -> tuple[tuple, frozenset[int]]:
            hint = _hint_schema(record)
            if hint is not None:
                # Both hint automata have two size-independent control states,
                # followed by one countdown state per concrete bus index.
                # Parameterize the latter by its owning index instead of
                # treating the n+2 one-hot vector as an opaque bus state.
                if bit < 2:
                    return (("state", "bus", hint, bit, 2), frozenset())
                owner = bit - 2
                if owner >= n:
                    raise Decline("canonicalize", hint, n,
                                  "hint automaton has more than n+2 states")
                return (("state", "monitor", f"{hint}:step", (owner,), 0),
                        frozenset((owner,)))
            if record["arity_kind"] == "bus_wide":
                return (("state", "bus", schemas[record["monitor"]], bit,
                         record["state_count"]), frozenset())
            return (("state", "monitor", record["template"], indices, bit),
                    frozenset(indices))

        # A role is the index-relative monitor-template inventory touching a
        # client.  This detects load_balancer's special client zero without
        # consulting AIG structure.
        raw_roles: dict[int, list[tuple]] = {i: [] for i in range(n)}
        for record in monitors.values():
            indices = tuple(record["index_tuple"])
            if (record["arity_kind"] != "local" or not indices or
                    _hint_schema(record) is not None):
                continue
            for value in set(indices):
                if value < n:
                    pattern = tuple("self" if x == value else "other" for x in indices)
                    raw_roles[value].append((record["template"], pattern))
        signatures = {i: tuple(sorted(items)) for i, items in raw_roles.items()}
        unique = {signature: f"role_{pos}" for pos, signature in enumerate(
            sorted(set(signatures.values()), key=repr))}
        role_by_client = {i: unique[signature] for i, signature in signatures.items()}

        variables: list[VarInfo] = []
        if meta:
            records = sorted(meta["variables"]["state"],
                             key=lambda x: x["certificate_input"])
            for item in records:
                parsed = _monitor_state(item["name"])
                if parsed is None:
                    key = ("state", "solver", item["name"])
                    owners = frozenset()
                else:
                    mid, bit = parsed
                    record = monitors[mid]
                    indices = tuple(record["index_tuple"])
                    key, owners = state_key(record, indices, bit)
                variables.append(VarInfo(item["certificate_input"], key, owners))
            signal_records = {
                ("controllable_" if output else "") + item["name"]:
                ("output" if output else "input", item["base_name"],
                 tuple(item["index_tuple"]))
                for output, records2 in ((False, prov["inputs"]), (True, prov["outputs"]))
                for item in records2
            }
            for kind in ("uncontrollable", "controllable"):
                for item in meta["variables"][kind]:
                    signal = signal_records.get(item["name"])
                    if signal is None:
                        raise Decline("canonicalize", item["name"], n,
                                      "certificate input is absent from provenance")
                    role, base, indices = signal
                    variables.append(VarInfo(item["certificate_input"],
                                             ("letter", role, base, indices),
                                             frozenset(indices)))
            if sorted(v.index for v in variables) != list(range(len(cert.inputs))):
                raise Decline("canonicalize", "certificate_inputs", n,
                              "sidecar does not cover each circuit input exactly once")
        else:
            # Target certificate inputs follow the public ABI: state then all
            # game inputs.  No in-scope exact monitor has sampled acceptance.
            for index, name in enumerate(game.latch_names):
                mid, bit = _monitor_state(name) or (-1, index)
                record = monitors.get(mid)
                if record is None:
                    key, owners = ("state", "solver", name), frozenset()
                else:
                    indices = tuple(record["index_tuple"])
                    key, owners = state_key(record, indices, bit)
                variables.append(VarInfo(index, key, owners))
            p = len(game.latches)
            prov_signals = {item["name"]: ("input", item["base_name"],
                                            tuple(item["index_tuple"]))
                            for item in prov["inputs"]}
            prov_signals.update({"controllable_" + item["name"]:
                                 ("output", item["base_name"],
                                  tuple(item["index_tuple"]))
                                 for item in prov["outputs"]})
            for name in game.input_names:
                role, base, indices = prov_signals[name]
                variables.append(VarInfo(p, ("letter", role, base, indices),
                                         frozenset(indices)))
                p += 1

        guarantees = [record for record in prov["monitors"]
                      if record["role"] == "justice"]
        goals = []
        for goal, record in enumerate(guarantees):
            indices = tuple(record["index_tuple"])
            hint = _hint_schema(record)
            owner = (indices[0] if record["arity_kind"] == "local" and indices and
                     hint is None else None)
            if hint is not None:
                key = ("schema", hint)
            elif record["arity_kind"] == "bus_wide":
                key = ("bus", schemas[record["monitor"]])
            else:
                key = ("local", record["template"],
                       tuple(0 if x == owner else 1 for x in indices))
            goals.append(GoalInfo(goal, record, owner, key))
        if len(goals) != len(game.justice):
            raise Decline("canonicalize", "goals", n,
                          "provenance justice monitors do not match game justice records")
        return cls(family, n, game_path, prov_path, cert_path, game, prov, cert,
                   meta, variables, goals, role_by_client,
                   tuple(sorted(set(schemas.values()))))

    def levels(self, goal: int) -> int:
        assert self.meta is not None
        return int(self.meta["counts"]["levels_per_goal"][goal])

    @property
    def fairness(self) -> int:
        if self.meta:
            return int(self.meta["counts"]["fairness_assumptions"])
        return len(self.game.fairness)


def _instance_identity(instance: Instance) -> tuple:
    cached = getattr(instance, "_p2a_identity", None)
    if cached is not None:
        return cached
    identity = (
        instance.family,
        instance.n,
        sha256(instance.game_path),
        sha256(instance.prov_path),
        sha256(instance.cert_path) if instance.cert_path is not None else None,
    )
    instance._p2a_identity = identity
    return identity


class CompiledAagContext:
    """Manager-local compiled AAG structure with one shared traversal memo."""

    def __init__(self, bdds: "Bdds", aag: Aag, source_identity: tuple,
                 variable_abi: tuple[tuple[str, int, str], ...],
                 variable_map: dict[int, int],
                 public_to_bdd: dict[int, int],
                 selected_roots: Iterable[int] = ()):
        self.bdds = bdds
        self.manager_lifetime = bdds.manager_lifetime
        self.aag = aag
        self.source_identity = source_identity
        self.variable_abi = variable_abi
        self.gates = {
            lhs // 2: (left, right) for lhs, left, right in aag.gates
        }
        self.variable_map = dict(variable_map)
        self.public_to_bdd = dict(public_to_bdd)
        if len(set(self.public_to_bdd.values())) != len(self.public_to_bdd):
            raise ValueError("public-to-BDD variable mapping must be injective")
        self.selected_roots = tuple(dict.fromkeys(selected_roots))
        self.memo: dict[int, object] = {}
        self.roots: dict[int, object] = {}
        self._game_functions: tuple[
            list[object], object, list[object], list[object]
        ] | None = None
        self._released = False
        if self.selected_roots:
            self.decode_roots(self.selected_roots)

    def _check_live(self) -> None:
        if self._released:
            raise RuntimeError("compiled AAG context has been released")
        if self.manager_lifetime is not self.bdds.manager_lifetime:
            raise RuntimeError("compiled AAG context belongs to another BDD manager")

    def decode_roots(self, literals: Iterable[int]) -> list[object]:
        self._check_live()
        requested = tuple(literals)
        diagnostic_token = (
            _diagnostic_begin("from_aag") if _DIAGNOSTICS_ENABLED else None
        )
        memo_size = len(self.memo)

        def visit(literal: int):
            if literal == 0:
                return self.bdds.buddy.bddfalse
            if literal == 1:
                return self.bdds.buddy.bddtrue
            if literal in self.memo:
                return self.memo[literal]
            if literal & 1:
                result = self.bdds.buddy.bdd_not(visit(literal ^ 1))
            elif literal // 2 in self.variable_map:
                result = self.bdds.buddy.bdd_ithvar(
                    self.variable_map[literal // 2])
            else:
                try:
                    left, right = self.gates[literal // 2]
                except KeyError as error:
                    raise ValueError(
                        f"AAG literal {literal} is absent from the public ABI and gate map"
                    ) from error
                result = visit(left) & visit(right)
            self.memo[literal] = result
            return result

        try:
            result = []
            for literal in requested:
                if literal not in self.roots:
                    self.roots[literal] = visit(literal)
                result.append(self.roots[literal])
            return result
        finally:
            if diagnostic_token is not None:
                try:
                    if _DIAGNOSTICS is None:
                        raise RuntimeError("diagnostics enabled without collector")
                    traversed = sum(
                        not (literal & 1) and literal // 2 in self.gates
                        for literal in tuple(self.memo)[memo_size:]
                    )
                    _DIAGNOSTICS.counters["from_aag_calls"] += 1
                    _DIAGNOSTICS.counters["from_aag_roots"] += len(requested)
                    _DIAGNOSTICS.counters[
                        "from_aag_gates_traversed"] += traversed
                    _DIAGNOSTIC_AAG_CONES.append({
                        "aag": str(self.aag.path),
                        "root_literals": list(requested),
                        "gates_traversed": traversed,
                        "shared_memo": True,
                    })
                except Exception as error:
                    if _DIAGNOSTICS is not None:
                        _DIAGNOSTICS.record_error("record_from_aag", error)
                _diagnostic_end(diagnostic_token)

    def root(self, literal: int):
        self._check_live()
        if literal in self.roots:
            return self.roots[literal]
        return self.decode_roots((literal,))[0]

    def game_functions(self) -> tuple[
            list[object], object, list[object], list[object]]:
        self._check_live()
        if self._game_functions is None:
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["game_functions_cache_misses"] += 1
            roots = [row[1] for row in self.aag.latches]
            roots.extend(self.aag.bad[:1])
            roots.extend(record[0] for record in self.aag.justice)
            roots.extend(self.aag.fairness)
            decoded = iter(self.decode_roots(roots))
            next_state = [next(decoded) for _row in self.aag.latches]
            bad = (next(decoded) if self.aag.bad
                   else self.bdds.buddy.bddfalse)
            goals = [next(decoded) for _record in self.aag.justice]
            fairness = [next(decoded) for _literal in self.aag.fairness]
            self._game_functions = (next_state, bad, goals, fairness)
        elif _DIAGNOSTICS is not None:
            _DIAGNOSTICS.counters["game_functions_cache_hits"] += 1
        return self._game_functions

    def release(self) -> None:
        if self._released:
            return
        self._game_functions = None
        self.roots.clear()
        self.memo.clear()
        self.gates.clear()
        self.variable_map.clear()
        self.public_to_bdd.clear()
        self._released = True


class TemplateSet(dict[tuple, object]):
    """Predicate templates plus their semantic-keyed support metadata."""

    def __init__(self, *args, supports: dict[tuple, frozenset[int]] | None = None,
                 cache_key: tuple | None = None, **kwargs):
        super().__init__(*args, **kwargs)
        self.supports = supports or {}
        self.cache_key = cache_key


@dataclasses.dataclass
class SupportCacheEntry:
    """Exact support whose root reference prevents BuDDy handle reuse."""

    root: object
    mapping: tuple[tuple[int, int], ...]
    bdd_variables: frozenset[int]
    public_variables: frozenset[int]
    root_payload_bytes: int


def _retained_python_bytes(*values: object) -> int:
    """Count retained Python container payload without following BDD proxies."""
    seen: set[int] = set()

    def visit(value: object) -> int:
        identity = id(value)
        if identity in seen:
            return 0
        seen.add(identity)
        size = sys.getsizeof(value)
        if isinstance(value, dict):
            return size + sum(
                visit(key) + visit(item) for key, item in value.items()
            )
        if isinstance(value, (tuple, list, set, frozenset)):
            return size + sum(visit(item) for item in value)
        return size

    return sum(visit(value) for value in values)


class GeneralizationAttemptContext:
    """Bounded caches owned by one seed-to-target generalization attempt."""

    def __init__(self, bdds: "Bdds", seeds: list[Instance], target: Instance):
        self.bdds = bdds
        self.manager_lifetime = bdds.manager_lifetime
        self.enabled = not _reference_aag_context_enabled()
        self.seeds = tuple(seeds)
        self.target = target
        self._released = False
        self._compiled: dict[tuple, CompiledAagContext] = {}
        self.template_cache: OrderedDict[
            tuple, tuple[TemplateSet, int]
        ] = OrderedDict()
        self.template_cache_limit = TEMPLATE_CACHE_MAX_ENTRIES
        self._owner_groups: dict[tuple, OwnerIndex] = {}
        self._subset_metadata: OrderedDict[
            tuple, dict[str, object]
        ] = OrderedDict()
        self.subset_metadata_limit = SUBSET_METADATA_MAX_ENTRIES
        self._support_cache: OrderedDict[tuple, SupportCacheEntry] = OrderedDict()
        self.support_cache_limit = SUPPORT_CACHE_MAX_ENTRIES
        self.support_cache_byte_limit = SUPPORT_CACHE_MAX_BYTES
        self._support_cache_weights: dict[tuple, int] = {}
        self._support_cache_bytes = 0
        self._projection_metadata: OrderedDict[
            tuple, tuple[object, frozenset[int], object | None]
        ] = OrderedDict()
        self.projection_metadata_limit = SUBSET_METADATA_MAX_ENTRIES
        self.projection_metadata_byte_limit = PROJECTION_METADATA_MAX_BYTES
        self._projection_metadata_weights: dict[tuple, int] = {}
        self._projection_metadata_bytes = 0
        self._cube_cache: OrderedDict[tuple[int, ...], object] = OrderedDict()
        self.cube_cache_limit = SUBSET_METADATA_MAX_ENTRIES
        self.cube_cache_byte_limit = CUBE_CACHE_MAX_BYTES
        self._cube_cache_weights: dict[tuple[int, ...], int] = {}
        self._cube_cache_bytes = 0
        self.inverse_normal: dict[int, tuple] = {}
        self.normalization_mapping: tuple[tuple[str, int], ...] = ()
        normalization_keys = self._normalization_keys()
        instances = (*self.seeds, self.target)
        public_variables = max(
            (max((item.index for item in instance.variables), default=-1) + 1
             for instance in instances),
            default=0,
        )
        public_states = len(target.game.latches)
        public_letters = len(target.game.inputs)
        public_variables = max(
            public_variables, public_states + public_letters,
            *(len(instance.cert.inputs) for instance in self.seeds
              if instance.cert is not None),
        )
        self.layout = VariableLayout.plan(
            public_variables=public_variables,
            public_states=public_states,
            public_letters=public_letters,
            composition_states=public_states,
            composition_letters=public_letters,
            policy_counters=len(target.goals),
            canonical_templates=len(normalization_keys),
            variable_limit=bdds.variable_limit,
        )
        bdds._activate_layout(self.layout, normalization_keys)
        self._freeze_normalization(normalization_keys)
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.extra["variable_layout"] = self.layout.description()
        if self.enabled:
            for seed in self.seeds:
                if seed.cert is not None:
                    self.certificate_context(seed)

    def _check_live(self) -> None:
        if self._released:
            raise RuntimeError("generalization attempt context has been released")
        if self.manager_lifetime is not self.bdds.manager_lifetime:
            raise RuntimeError("generalization context belongs to another BDD manager")

    def _normalization_keys(self) -> tuple[tuple, ...]:
        keys = set()
        for seed in self.seeds:
            upper = min(MAX_PREDICATE_ARITY, seed.n)
            for arity in range(1, upper + 1):
                for subset0 in itertools.combinations(range(seed.n), arity):
                    orderings = {_ordered_subset(seed, subset0, None)}
                    orderings.update(
                        _ordered_subset(seed, subset0, goal)
                        for goal in seed.goals if goal.owner in subset0
                    )
                    for subset in orderings:
                        slots = {
                            client: position
                            for position, client in enumerate(subset)
                        }
                        keys.update(
                            _normal_key(item.key, slots)
                            for item in self._groups(seed).keep_items(subset)
                        )
        return tuple(sorted(keys, key=repr))

    def _freeze_normalization(self, keys: tuple[tuple, ...]) -> None:
        for key in keys:
            self.bdds.normal_var(key)
        self.inverse_normal = {
            value: key for key, value in self.bdds.normal.items()
        }
        self.normalization_mapping = tuple(sorted(
            ((repr(key), value) for key, value in self.bdds.normal.items())
        ))

    def _certificate_identity(self, instance: Instance) -> tuple:
        assert instance.cert is not None
        return ("seed-certificate", _instance_identity(instance),
                tuple(instance.cert.input_names),
                tuple(instance.cert.output_names))

    def certificate_context(self, instance: Instance) -> CompiledAagContext:
        self._check_live()
        if not self.enabled:
            raise RuntimeError("compiled AAG contexts are disabled for the reference path")
        assert instance.cert is not None
        identity = self._certificate_identity(instance)
        if identity not in self._compiled:
            variable_map = {
                literal // 2: index
                for index, literal in enumerate(instance.cert.inputs)
            }
            abi = tuple(
                ("certificate_input", index, name)
                for index, name in enumerate(instance.cert.input_names)
            )
            required_roots = [
                literal
                for name, literal in zip(
                    instance.cert.output_names, instance.cert.outputs,
                    strict=True)
                if name == "inv" or name.startswith("x_")
            ]
            self._compiled[identity] = CompiledAagContext(
                self.bdds, instance.cert, identity, abi, variable_map,
                {index: variable_map[literal // 2]
                 for index, literal in enumerate(instance.cert.inputs)},
                required_roots,
            )
        return self._compiled[identity]

    def game_context(self, game: Aag) -> CompiledAagContext:
        self._check_live()
        digest = sha256(game.path)
        identity = (
            "game", str(game.path.resolve()), digest,
            tuple(game.latch_names), tuple(game.input_names),
        )
        if identity not in self._compiled:
            nstate = len(game.latches)
            if (nstate != self.layout.public_state_count or
                    len(game.inputs) != self.layout.public_letter_count):
                raise ValueError("game ABI does not match the attempt variable layout")
            variable_map = {
                row[0] // 2: self.layout.public_state(index)
                for index, row in enumerate(game.latches)
            }
            variable_map.update({
                literal // 2: self.layout.public_letter(index)
                for index, literal in enumerate(game.inputs)
            })
            abi = tuple(
                [("state", index, name)
                 for index, name in enumerate(game.latch_names)]
                + [("letter", index, name)
                   for index, name in enumerate(game.input_names)]
            )
            self._compiled[identity] = CompiledAagContext(
                self.bdds, game, identity, abi, variable_map,
                ({index: self.layout.public_state(index)
                  for index in range(nstate)} |
                 {nstate + index: self.layout.public_letter(index)
                  for index in range(len(game.inputs))}))
        return self._compiled[identity]

    def predicate_cache_key(
            self, seeds: list[Instance], name: str, arity: int,
            goals: list[GoalInfo | None], seed_names: list[str]) -> tuple:
        self._check_live()
        relations = []
        for seed, goal in zip(seeds, goals, strict=True):
            relations.append(None if goal is None else (
                goal.key,
                seed.role_by_client.get(goal.owner) if goal.owner is not None else None,
                goal.owner == 0,
            ))
        return (
            self.manager_lifetime,
            tuple(self._certificate_identity(seed) for seed in seeds),
            name,
            tuple(seed_names),
            tuple(relations),
            arity,
            self.normalization_mapping,
        )

    def template_cache_get(
            self, key: tuple) -> tuple[TemplateSet, int] | None:
        self._check_live()
        try:
            value = self.template_cache.pop(key)
        except KeyError:
            return None
        self.template_cache[key] = value
        return value

    def template_cache_put(
            self, key: tuple, value: tuple[TemplateSet, int]) -> None:
        self._check_live()
        self.template_cache.pop(key, None)
        self.template_cache[key] = value
        while len(self.template_cache) > self.template_cache_limit:
            self.template_cache.popitem(last=False)

    def _subset_cache_put(self, key: tuple, value: dict[str, object]) -> None:
        self._subset_metadata.pop(key, None)
        self._subset_metadata[key] = value
        while len(self._subset_metadata) > self.subset_metadata_limit:
            self._subset_metadata.popitem(last=False)

    @staticmethod
    def _cache_get_lru(cache: OrderedDict, key: object) -> object:
        value = cache.pop(key)
        cache[key] = value
        return value

    @staticmethod
    def _cache_put_bounded(
            cache: OrderedDict, weights: dict, key: object, value: object,
            weight: int, current_bytes: int, entry_limit: int,
            byte_limit: int) -> int:
        if entry_limit < 0 or byte_limit < 0:
            raise ValueError("cache limits must be non-negative")
        if key in cache:
            cache.pop(key)
            current_bytes -= weights.pop(key)
        cache[key] = value
        weights[key] = weight
        current_bytes += weight
        while cache and (
                len(cache) > entry_limit or current_bytes > byte_limit):
            evicted_key, _evicted = cache.popitem(last=False)
            current_bytes -= weights.pop(evicted_key)
        return current_bytes

    def _bdd_payload_bytes(self, root: object) -> int:
        nodes = int(self.bdds.buddy.bdd_nodecount(root))
        if nodes < 0:
            raise RuntimeError(f"bdd_nodecount failed with status {nodes}")
        return sys.getsizeof(root) + nodes * BUDDY_NODE_ACCOUNTING_BYTES

    def _groups(self, instance: Instance) -> OwnerIndex:
        identity = _instance_identity(instance)
        if identity not in self._owner_groups:
            self._owner_groups[identity] = OwnerIndex(instance.variables)
        return self._owner_groups[identity]

    def subset_metadata(self, instance: Instance,
                        subset: tuple[int, ...]) -> dict[str, object]:
        self._check_live()
        key = (_instance_identity(instance), subset, self.normalization_mapping,
               self.layout.identity)
        try:
            metadata = self._subset_metadata.pop(key)
        except KeyError:
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["subset_metadata_cache_misses"] += 1
            slots = {client: position for position, client in enumerate(subset)}
            keep_items = self._groups(instance).keep_items(subset)
            keep = frozenset(item.index for item in keep_items)
            mapping = {
                item.index: self.bdds.normal_var(_normal_key(item.key, slots))
                for item in keep_items
            }
            reverse = {normal: concrete for concrete, normal in mapping.items()}
            concrete = {
                _normal_key(item.key, slots): item.index for item in keep_items
            }
            metadata = {
                "keep": keep,
                "mapping": mapping,
                "reverse": reverse,
                "concrete": concrete,
            }
        else:
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["subset_metadata_cache_hits"] += 1
        self._subset_cache_put(key, metadata)
        return metadata

    def exact_public_support(
            self, function: object,
            public_to_bdd: dict[int, int]) -> SupportCacheEntry:
        """Return exact semantic support for one owned root and ABI mapping."""
        self._check_live()
        mapping = tuple(sorted(public_to_bdd.items()))
        if len({bdd_variable for _public, bdd_variable in mapping}) != len(mapping):
            raise ValueError("public-to-BDD variable mapping must be injective")
        # Python object identity is used only to find this owned root; this is
        # never a collectable/reusable BuDDy node ID.
        key = (id(function), mapping)
        try:
            entry = self._cache_get_lru(self._support_cache, key)
        except KeyError:
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["support_cache_misses"] += 1
            bdd_variables = frozenset(_support(self.bdds, function))
            bdd_to_public = {
                bdd_variable: public
                for public, bdd_variable in mapping
            }
            missing = bdd_variables - bdd_to_public.keys()
            if missing:
                raise KeyError(
                    f"root support has unregistered BDD variables {sorted(missing)}"
                )
            entry = SupportCacheEntry(
                function,
                mapping,
                bdd_variables,
                frozenset(bdd_to_public[variable] for variable in bdd_variables),
                self._bdd_payload_bytes(function),
            )
        else:
            if entry.root is not function:
                raise RuntimeError("support-cache Python identity was unexpectedly reused")
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["support_cache_hits"] += 1
            return entry
        weight = sys.getsizeof(entry) + entry.root_payload_bytes + _retained_python_bytes(
            key, entry.mapping, entry.bdd_variables, entry.public_variables)
        self._support_cache_bytes = self._cache_put_bounded(
            self._support_cache, self._support_cache_weights, key, entry,
            weight, self._support_cache_bytes, self.support_cache_limit,
            self.support_cache_byte_limit,
        )
        return entry

    def cached_bdd_support(self, function: object) -> frozenset[int] | None:
        """Use only support retained with this exact root by the attempt cache."""
        self._check_live()
        for (root_id, _mapping), entry in self._support_cache.items():
            if root_id == id(function) and entry.root is function:
                return entry.bdd_variables
        return None

    def projection_drop(
            self, instance: Instance, subset: tuple[int, ...], function: object,
            public_to_bdd: dict[int, int], keep: frozenset[int],
            support: SupportCacheEntry | None = None,
    ) -> tuple[frozenset[int], object | None]:
        """Cache the support-restricted public drop set and its BDD cube."""
        self._check_live()
        entry = support or self.exact_public_support(function, public_to_bdd)
        key = (
            id(function), entry.mapping, _instance_identity(instance), subset,
            self.layout.identity,
        )
        try:
            owned_root, drop, drop_cube = self._cache_get_lru(
                self._projection_metadata, key)
        except KeyError:
            drop = entry.public_variables - keep
            mapping = dict(entry.mapping)
            cube_variables = tuple(sorted(mapping[public] for public in drop))
            if cube_variables:
                try:
                    drop_cube = self._cache_get_lru(
                        self._cube_cache, cube_variables)
                except KeyError:
                    drop_cube = self.bdds.cube(cube_variables)
                    cube_weight = (
                        self._bdd_payload_bytes(drop_cube)
                        + _retained_python_bytes(cube_variables)
                    )
                    self._cube_cache_bytes = self._cache_put_bounded(
                        self._cube_cache, self._cube_cache_weights,
                        cube_variables, drop_cube, cube_weight,
                        self._cube_cache_bytes, self.cube_cache_limit,
                        self.cube_cache_byte_limit,
                    )
            else:
                drop_cube = None
            owned_root = function
        else:
            if owned_root is not function:
                raise RuntimeError("projection-cache Python identity was unexpectedly reused")
            return drop, drop_cube
        projection_value = (owned_root, drop, drop_cube)
        projection_weight = (
            sys.getsizeof(projection_value)
            + entry.root_payload_bytes
            + _retained_python_bytes(key, drop)
        )
        if drop_cube is not None:
            projection_weight += self._bdd_payload_bytes(drop_cube)
        self._projection_metadata_bytes = self._cache_put_bounded(
            self._projection_metadata, self._projection_metadata_weights,
            key, projection_value, projection_weight,
            self._projection_metadata_bytes,
            self.projection_metadata_limit,
            self.projection_metadata_byte_limit,
        )
        return drop, drop_cube

    def release(self) -> None:
        if self._released:
            return
        for context in self._compiled.values():
            context.release()
        self._compiled.clear()
        self.template_cache.clear()
        self._owner_groups.clear()
        self._subset_metadata.clear()
        self._support_cache.clear()
        self._support_cache_weights.clear()
        self._support_cache_bytes = 0
        self._projection_metadata.clear()
        self._projection_metadata_weights.clear()
        self._projection_metadata_bytes = 0
        self._cube_cache.clear()
        self._cube_cache_weights.clear()
        self._cube_cache_bytes = 0
        self.inverse_normal.clear()
        self.normalization_mapping = ()
        self.bdds._deactivate_layout(self.layout)
        self._released = True
        if self.bdds.attempt_context is self:
            self.bdds.attempt_context = None


class Bdds:
    def __init__(self, var_count: int | None = None):
        global _BUDDY_MANAGER_LIFETIME, _BUDDY_VARIABLE_COUNT, _DIAGNOSTIC_BUDDY
        buddy, _extension, binding_path, extension_path = load_buddy_bindings(
            BINDINGS_SITE
        )
        self.buddy = buddy
        # Validate the backend identity and select every potentially failing
        # variable-growth call before initializing or touching the manager.
        self.compose_route = _compose_route()
        self._veccompose = None
        if self.compose_route == "native_veccompose":
            assert TOOL_CONFIG.buddy_adapter is not None
            self._veccompose = BuddyVeccomposeAdapter(
                buddy, extension_path, TOOL_CONFIG.buddy_adapter)
            self._variables = self._veccompose
        else:
            self._variables = BuddyVariableAdapter(buddy, extension_path)
        if self._variables.max_variable_count != BDD_COORDINATE_LIMIT:
            raise RuntimeError(
                "validated BuDDy variable ceiling does not match the layout "
                f"ceiling: {self._variables.max_variable_count} != "
                f"{BDD_COORDINATE_LIMIT}"
            )
        if var_count is not None and not 0 <= var_count <= BDD_COORDINATE_LIMIT:
            raise OverflowError(
                f"BDD variable requirement {var_count} exceeds backend maximum "
                f"{BDD_COORDINATE_LIMIT}"
            )
        if not buddy.bdd_isrunning():
            buddy.bdd_init(8_000_000, 800_000)
            buddy.bdd_setmaxincrease(2_000_000)
            _BUDDY_MANAGER_LIFETIME = object()
            _BUDDY_VARIABLE_COUNT = 0
        elif _BUDDY_MANAGER_LIFETIME is None:
            _BUDDY_MANAGER_LIFETIME = object()
            _BUDDY_VARIABLE_COUNT = self._variables.variable_count()
        self.manager_lifetime = _BUDDY_MANAGER_LIFETIME
        self.variable_limit = var_count
        assert _BUDDY_VARIABLE_COUNT is not None
        self.var_count = _BUDDY_VARIABLE_COUNT
        self.layout: VariableLayout | None = None
        self.normal: dict[tuple, int] = {}
        if var_count is not None:
            self._ensure_variables(var_count)
        self.attempt_context: GeneralizationAttemptContext | None = None
        self.relabel_route_counts: Counter[str] = Counter()
        if _DIAGNOSTICS_ENABLED:
            try:
                _DIAGNOSTIC_BUDDY = buddy
                if _DIAGNOSTICS is None:
                    raise RuntimeError("diagnostics enabled without collector")
                environment = _DIAGNOSTICS.extra.setdefault("environment", {})
                if not isinstance(environment, dict):
                    raise TypeError("diagnostic environment field is not an object")
                loaded = []
                maps = pathlib.Path("/proc/self/maps")
                if maps.is_file():
                    loaded = sorted({
                        fields[-1]
                        for line in maps.read_text(encoding="utf-8").splitlines()
                        if (fields := line.split()) and fields[-1].startswith("/")
                        and "bdd" in fields[-1].lower()
                    })
                environment["buddy_binding"] = {
                    "module": str(binding_path),
                    "extension": str(extension_path),
                    "loaded_shared_objects": loaded,
                    "version_number": buddy.bdd_versionnum(),
                    "version_string": buddy.bdd_versionstr(),
                }
                environment["compose_route"] = self.compose_route
                environment["buddy_adapter"] = (
                    self._veccompose.description()
                    if self._veccompose is not None else None
                )
                _diagnostic_buddy_boundary("bdd_manager:ready")
            except Exception as error:
                if _DIAGNOSTICS is not None:
                    _DIAGNOSTICS.record_error("record_buddy_environment", error)

    def close(self) -> None:
        # The manager is intentionally process-wide; see __init__.
        if self.attempt_context is not None:
            self.attempt_context.release()

    def begin_attempt(self, seeds: list[Instance],
                      target: Instance) -> GeneralizationAttemptContext:
        if self.attempt_context is not None:
            raise RuntimeError("a generalization attempt context is already active")
        try:
            self.attempt_context = GeneralizationAttemptContext(self, seeds, target)
        except Exception:
            self.layout = None
            self.normal.clear()
            raise
        return self.attempt_context

    def _ensure_variables(self, required: int) -> None:
        global _BUDDY_VARIABLE_COUNT
        if required < 0 or required > BDD_COORDINATE_LIMIT:
            raise OverflowError(f"invalid BDD variable requirement {required}")
        if self.variable_limit is not None and required > self.variable_limit:
            raise OverflowError(
                f"BDD variable requirement {required} exceeds limit "
                f"{self.variable_limit}"
            )
        assert _BUDDY_VARIABLE_COUNT is not None
        current = _BUDDY_VARIABLE_COUNT
        if required > current:
            self._variables.set_variable_count(required)
            current = self._variables.variable_count()
            if current < required:
                raise RuntimeError(
                    "checked BDD variable growth returned fewer variables than "
                    f"requested: {current} < {required}"
                )
            _BUDDY_VARIABLE_COUNT = current
        self.var_count = current

    def _activate_layout(
            self, layout: VariableLayout,
            normalization_keys: tuple[tuple, ...]) -> None:
        if self.layout is not None:
            raise RuntimeError("a BDD variable layout is already active")
        if layout.canonical_templates.size != len(normalization_keys):
            raise ValueError("canonical key count does not match its BDD block")
        self._ensure_variables(layout.required_variables)
        self.layout = layout
        self.normal = {
            key: layout.canonical_templates.coordinate(index)
            for index, key in enumerate(normalization_keys)
        }

    def _deactivate_layout(self, layout: VariableLayout) -> None:
        if self.layout is not layout:
            raise RuntimeError("attempt released a different BDD variable layout")
        self.normal.clear()
        self.layout = None

    def cube(self, variables: Iterable[int]):
        stats_before = (
            _diagnostic_bdd_snapshot(self) if _DIAGNOSTICS_ENABLED else None)
        diagnostic_token = (
            _diagnostic_begin("variable_cube_construction")
            if _DIAGNOSTICS_ENABLED else None
        )
        result = self.buddy.bddtrue
        try:
            ordered = sorted(set(variables))
            if ordered:
                self._ensure_variables(ordered[-1] + 1)
            for variable in ordered:
                result &= self.buddy.bdd_ithvar(variable)
            return result
        finally:
            if diagnostic_token is not None:
                _diagnostic_end(diagnostic_token)
                _diagnostic_record_bdd_operation(
                    "variable_cube_construction", stats_before,
                    _diagnostic_bdd_snapshot(self),
                )

    def from_aag_uncached(self, aag: Aag, literal: int):
        """HEAD-compatible single-root importer retained as a test oracle."""
        self._ensure_variables(len(aag.inputs))
        diagnostic_token = (
            _diagnostic_begin("from_aag") if _DIAGNOSTICS_ENABLED else None
        )
        gate = {lhs // 2: (left, right) for lhs, left, right in aag.gates}
        input_var = {lit // 2: i for i, lit in enumerate(aag.inputs)}
        memo = {}

        def visit(lit: int):
            if lit == 0:
                return self.buddy.bddfalse
            if lit == 1:
                return self.buddy.bddtrue
            if lit in memo:
                return memo[lit]
            if lit & 1:
                result = self.buddy.bdd_not(visit(lit ^ 1))
            elif lit // 2 in input_var:
                result = self.buddy.bdd_ithvar(input_var[lit // 2])
            else:
                left, right = gate[lit // 2]
                result = visit(left) & visit(right)
            memo[lit] = result
            return result

        try:
            return visit(literal)
        finally:
            if diagnostic_token is not None:
                try:
                    if _DIAGNOSTICS is None:
                        raise RuntimeError("diagnostics enabled without collector")
                    gates_traversed = sum(
                        not (lit & 1) and lit // 2 in gate for lit in memo)
                    _DIAGNOSTICS.counters["from_aag_calls"] += 1
                    _DIAGNOSTICS.counters[
                        "from_aag_gates_traversed"] += gates_traversed
                    _DIAGNOSTIC_AAG_CONES.append({
                        "aag": str(aag.path),
                        "root_literal": literal,
                        "gates_traversed": gates_traversed,
                    })
                except Exception as error:
                    if _DIAGNOSTICS is not None:
                        _DIAGNOSTICS.record_error("record_from_aag", error)
                _diagnostic_end(diagnostic_token)

    def from_aag(self, aag: Aag, literal: int):
        """Compile one standalone root; attempt code uses shared contexts."""
        if _reference_aag_context_enabled():
            return self.from_aag_uncached(aag, literal)
        variable_map = {
            input_literal // 2: index
            for index, input_literal in enumerate(aag.inputs)
        }
        abi = tuple(
            ("input", index, name)
            for index, name in enumerate(aag.input_names)
        )
        identity = (
            "standalone", str(aag.path.resolve()), sha256(aag.path), abi,
            self.manager_lifetime,
        )
        context = CompiledAagContext(
            self, aag, identity, abi, variable_map,
            {index: index for index in range(len(aag.inputs))}, (literal,))
        try:
            return context.root(literal)
        finally:
            context.release()

    def game_functions(self, game: Aag):
        """Compile game literals over public certificate variable indices."""
        self._ensure_variables(len(game.latches) + len(game.inputs))
        if self.attempt_context is not None and self.attempt_context.enabled:
            return self.attempt_context.game_context(game).game_functions()
        if not _reference_aag_context_enabled():
            nstate = len(game.latches)
            variable_map = {
                row[0] // 2: index for index, row in enumerate(game.latches)
            }
            variable_map.update({
                literal // 2: nstate + index
                for index, literal in enumerate(game.inputs)
            })
            abi = tuple(
                [("state", index, name)
                 for index, name in enumerate(game.latch_names)]
                + [("letter", index, name)
                   for index, name in enumerate(game.input_names)]
            )
            identity = (
                "standalone-game", str(game.path.resolve()), sha256(game.path),
                abi, self.manager_lifetime,
            )
            context = CompiledAagContext(
                self, game, identity, abi, variable_map,
                {index: index for index in range(nstate + len(game.inputs))})
            try:
                return context.game_functions()
            finally:
                context.release()
        nstate = len(game.latches)
        gate = {lhs // 2: (left, right) for lhs, left, right in game.gates}
        input_var = {lit // 2: nstate + i for i, lit in enumerate(game.inputs)}
        latch_var = {row[0] // 2: i for i, row in enumerate(game.latches)}
        memo = {}

        def visit(lit: int):
            if lit == 0:
                return self.buddy.bddfalse
            if lit == 1:
                return self.buddy.bddtrue
            if lit in memo:
                return memo[lit]
            if lit & 1:
                result = self.buddy.bdd_not(visit(lit ^ 1))
            elif lit // 2 in input_var:
                result = self.buddy.bdd_ithvar(input_var[lit // 2])
            elif lit // 2 in latch_var:
                result = self.buddy.bdd_ithvar(latch_var[lit // 2])
            else:
                left, right = gate[lit // 2]
                result = visit(left) & visit(right)
            memo[lit] = result
            return result

        next_state = [visit(row[1]) for row in game.latches]
        bad = visit(game.bad[0]) if game.bad else self.buddy.bddfalse
        goals = [visit(record[0]) for record in game.justice]
        fairness = [visit(lit) for lit in game.fairness]
        return next_state, bad, goals, fairness

    def substitute_state(self, function, next_state: list[object]):
        """Simultaneously substitute the game's next-state functions.

        The native adapter uses ``bdd_veccompose``.  The temporary block stays
        in this API only for the retained two-pass test/reference oracle.
        """
        if self.layout is None:
            raise RuntimeError("state substitution requires an active variable layout")
        if len(next_state) > self.layout.composition_state_count:
            raise OverflowError("next-state functions exceed the composition layout")
        variables = list(range(len(next_state)))
        temporaries = [
            self.layout.composition_temporary(variable)
            for variable in variables
        ]
        return self.substitute_variables(
            function, variables, next_state, temporaries)

    def substitute_variables(self, function, variables: list[int],
                             replacements: list[object],
                             temporaries: list[int]):
        if not (len(variables) == len(replacements) == len(temporaries)):
            raise ValueError("substitution vectors have different lengths")
        if not variables:
            return function
        diagnostic_token = (
            _diagnostic_begin("substitute_variables")
            if _DIAGNOSTICS_ENABLED else None
        )
        try:
            if self.compose_route == "two_pass":
                result = self.substitute_variables_two_pass(
                    function, variables, replacements, temporaries)
                if diagnostic_token is not None and _DIAGNOSTICS is not None:
                    _DIAGNOSTICS.counters["bdd_compose_calls"] += 2 * len(variables)
                return result
            if self._veccompose is None:
                raise RuntimeError("native compose route has no adapter")
            result = self._veccompose.compose(function, variables, replacements)
            if diagnostic_token is not None and _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["bdd_veccompose_calls"] += 1
            return result
        finally:
            if diagnostic_token is not None:
                try:
                    if _DIAGNOSTICS is None:
                        raise RuntimeError("diagnostics enabled without collector")
                    _DIAGNOSTICS.counters["substitute_variables_calls"] += 1
                except Exception as error:
                    if _DIAGNOSTICS is not None:
                        _DIAGNOSTICS.record_error(
                            "record_substitute_variables", error)
                _diagnostic_end(diagnostic_token)

    def substitute_variables_two_pass(
            self, function, variables: list[int], replacements: list[object],
            temporaries: list[int]):
        """Legacy simultaneous-substitution oracle for tiny regression tests."""
        if not (len(variables) == len(replacements) == len(temporaries)):
            raise ValueError("substitution vectors have different lengths")
        result = function
        for variable, temporary in zip(variables, temporaries, strict=True):
            result = self.buddy.bdd_compose(
                result, self.buddy.bdd_ithvar(temporary), variable)
        for temporary, replacement in zip(
                temporaries, replacements, strict=True):
            result = self.buddy.bdd_compose(result, replacement, temporary)
        return result

    def collect_garbage_for_testing(self) -> None:
        """Force native BuDDy collection; the installed SWIG API omits it."""
        if self._veccompose is None:
            raise RuntimeError("native compose adapter is not configured")
        self._veccompose.collect_garbage()

    def cpre(self, target, next_state: list[object], bad,
             controls: list[int], uncontrollable: list[int]):
        safe = self.buddy.bdd_not(bad)
        successor = self.substitute_state(target, next_state)
        if controls:
            step = self.and_exist(safe, successor, self.cube(controls))
        else:
            step = safe & successor
        if uncontrollable:
            step = self.buddy.bdd_forall(step, self.cube(uncontrollable))
        return step

    def and_exist(self, left, right, variables):
        """Compute ``exists variables: left & right`` in one BuDDy pass."""
        if self._veccompose is None:
            return self.buddy.bdd_appex(
                left, right, self.buddy.bddop_and, variables)
        result = self._veccompose.and_exist(left, right, variables)
        if _DIAGNOSTICS is not None:
            _DIAGNOSTICS.counters["bdd_appex_and_calls"] += 1
        return result

    def relabel(
            self, function, mapping: dict[int, int],
            support: Iterable[int] | None = None):
        """Relabel the root; a caller's support hint is never the safety proof."""
        cached = (self.attempt_context.cached_bdd_support(function)
                  if self.attempt_context is not None else None)
        exact_support = set(cached if cached is not None else _support(self, function))
        missing = exact_support - mapping.keys()
        if missing:
            raise KeyError(f"BDD variables {sorted(missing)} have no relabelling")
        variables = sorted(exact_support)
        targets = [mapping[variable] for variable in variables]
        replace = len(targets) == len(set(targets))
        stats_before = (
            _diagnostic_bdd_snapshot(self) if _DIAGNOSTICS_ENABLED else None)
        diagnostic_token = (
            _diagnostic_begin("bdd_projection_relabel")
            if _DIAGNOSTICS_ENABLED else None
        )
        relabel_token = (
            _diagnostic_begin("bdd_relabel_rename")
            if _DIAGNOSTICS_ENABLED else None
        )
        def recursive_relabel(node, memo: dict[int, object]):
            if node == self.buddy.bddtrue or node == self.buddy.bddfalse:
                return node
            key = node.id()
            if key not in memo:
                high = recursive_relabel(self.buddy.bdd_high(node), memo)
                low = recursive_relabel(self.buddy.bdd_low(node), memo)
                old = self.buddy.bdd_var(node)
                memo[key] = self.buddy.bdd_ite(
                    self.buddy.bdd_ithvar(mapping[old]), high, low)
            return memo[key]

        try:
            if not variables:
                result = function
                route = "identity"
            elif self._veccompose is None:
                result = recursive_relabel(function, {})
                route = "recursive_reference"
            else:
                result = self._veccompose.relabel_variables(
                    function, variables, targets, use_replace=replace)
                route = "bdd_replace" if replace else "bdd_veccompose"
            if _DIAGNOSTICS_ENABLED:
                self.relabel_route_counts[route] += 1
            if _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters[f"relabel_{route}_calls"] += 1
            return result
        finally:
            if relabel_token is not None:
                _diagnostic_end(relabel_token)
            if diagnostic_token is not None:
                _diagnostic_end(diagnostic_token)
            if relabel_token is not None:
                _diagnostic_record_bdd_operation(
                    "bdd_relabel_rename", stats_before,
                    _diagnostic_bdd_snapshot(self),
                )

    def normal_var(self, key: tuple) -> int:
        if self.layout is None:
            raise RuntimeError("canonical variables require an active variable layout")
        try:
            return self.normal[key]
        except KeyError as error:
            raise OverflowError(
                f"canonical variable {key!r} was not reserved before BDD work"
            ) from error

    def to_aag(self, builder: AagBuilder, function, variable_map: dict[int, int],
               memo: dict[int, int] | None = None) -> int:
        # One memo may be shared by outputs using the same variable_map.  This
        # is important for rank families: their BDD DAGs have large common
        # tails which should remain one AIG DAG rather than be emitted once
        # per predicate.
        literal_map = {variable: 2 * (aig_input + 1)
                       for variable, aig_input in variable_map.items()}
        return self.to_aag_literals(builder, function, literal_map, memo)

    def to_aag_literals(self, builder: AagBuilder, function,
                        literal_map: dict[int, int],
                        memo: dict[int, int] | None = None) -> int:
        """Emit a BDD with variables mapped to arbitrary AIG literals."""
        if memo is None:
            memo = {}
        memo_size = len(memo) if _DIAGNOSTICS_ENABLED else 0
        diagnostic_token = (
            _diagnostic_begin("export") if _DIAGNOSTICS_ENABLED else None
        )

        def visit(node):
            if node == self.buddy.bddfalse:
                return 0
            if node == self.buddy.bddtrue:
                return 1
            key = node.id()
            if key in memo:
                return memo[key]
            var = self.buddy.bdd_var(node)
            if var not in literal_map:
                raise KeyError(f"cannot emit unmapped BDD variable {var}")
            vlit = literal_map[var]
            high = visit(self.buddy.bdd_high(node))
            low = visit(self.buddy.bdd_low(node))
            # ite(v,h,l) = (v&h) | (!v&l)
            result = builder.lor(builder.land(vlit, high),
                                 builder.land(vlit ^ 1, low))
            memo[key] = result
            return result

        try:
            return visit(function)
        finally:
            if diagnostic_token is not None:
                try:
                    if _DIAGNOSTICS is None:
                        raise RuntimeError("diagnostics enabled without collector")
                    nodes_visited = max(0, len(memo) - memo_size)
                    _DIAGNOSTICS.counters["bdd_to_aig_walks"] += 1
                    _DIAGNOSTICS.counters[
                        "bdd_to_aig_nodes_visited"] += nodes_visited
                except Exception as error:
                    if _DIAGNOSTICS is not None:
                        _DIAGNOSTICS.record_error("record_export", error)
                _diagnostic_end(diagnostic_token)






def projection_templates(bdds: Bdds, instance: Instance, name: str, arity: int,
                         goal: GoalInfo | None = None,
                         context: GeneralizationAttemptContext | None = None,
                         cache_key: tuple | None = None) -> TemplateSet:
    assert instance.cert is not None
    if context is not None and context.enabled:
        certificate_context = context.certificate_context(instance)
        function = certificate_context.root(instance.cert.output(name))
        public_to_bdd = certificate_context.public_to_bdd
        function_support = context.exact_public_support(
            function, public_to_bdd)
    else:
        function = bdds.from_aag_uncached(
            instance.cert, instance.cert.output(name))
        public_to_bdd = {
            public: public for public in range(len(instance.cert.inputs))
        }
        bdd_to_public = {
            bdd_variable: public
            for public, bdd_variable in public_to_bdd.items()
        }
        function_support = frozenset(
            bdd_to_public[variable] for variable in _support(bdds, function)
        )
    metadata_token = (
        _diagnostic_begin("projection_metadata")
        if _DIAGNOSTICS_ENABLED else None
    )
    try:
        templates = TemplateSet(cache_key=cache_key)
        rebuilt = bdds.buddy.bddtrue
    finally:
        if metadata_token is not None:
            _diagnostic_end(metadata_token)
    for subset0 in itertools.combinations(range(instance.n), arity):
        metadata_token = (
            _diagnostic_begin("projection_metadata")
            if _DIAGNOSTICS_ENABLED else None
        )
        try:
            subset = _ordered_subset(instance, subset0, goal)
            if context is not None and context.enabled:
                metadata = context.subset_metadata(instance, subset)
                keep = metadata["keep"]
                mapping = metadata["mapping"]
                reverse = metadata["reverse"]
                drop, drop_cube = context.projection_drop(
                    instance, subset, function, public_to_bdd, keep,
                    function_support)
            else:
                selected = frozenset(subset)
                keep_items = tuple(sorted(
                    (item for item in instance.variables
                     if not item.owners or item.owners <= selected),
                    key=lambda item: item.index,
                ))
                keep = frozenset(item.index for item in keep_items)
                drop = function_support - keep
                slots = {client: pos for pos, client in enumerate(subset)}
                mapping = {
                    item.index: bdds.normal_var(_normal_key(item.key, slots))
                    for item in keep_items
                }
                reverse = {
                    bdds.normal_var(_normal_key(item.key, slots)): item.index
                    for item in keep_items
                }
                drop_cube = bdds.cube(
                    public_to_bdd[public] for public in drop) if drop else None
            if _DIAGNOSTICS_ENABLED:
                _diagnostic_record_subset(instance.n, subset, "projection")
        finally:
            if metadata_token is not None:
                _diagnostic_end(metadata_token)
        if drop:
            stats_before = (
                _diagnostic_bdd_snapshot(bdds)
                if _DIAGNOSTICS_ENABLED else None)
            project_token = (
                _diagnostic_begin("bdd_projection_relabel")
                if _DIAGNOSTICS_ENABLED else None
            )
            exist_token = (
                _diagnostic_begin("bdd_existential_quantification")
                if _DIAGNOSTICS_ENABLED else None
            )
            try:
                projected = bdds.buddy.bdd_exist(function, drop_cube)
            finally:
                if exist_token is not None:
                    _diagnostic_end(exist_token)
                if project_token is not None:
                    _diagnostic_end(project_token)
                if exist_token is not None:
                    _diagnostic_record_bdd_operation(
                        "bdd_existential_quantification", stats_before,
                        _diagnostic_bdd_snapshot(bdds),
                    )
        else:
            projected = function
        projected_support = _support(bdds, projected)
        if _DIAGNOSTICS_ENABLED:
            try:
                _diagnostic_record_support(
                    name, instance.n, subset, projected_support)
            except Exception as error:
                if _DIAGNOSTICS is not None:
                    _DIAGNOSTICS.record_error(
                        "measure_projected_support", error)
        normalized = bdds.relabel(projected, mapping, projected_support)
        metadata_token = (
            _diagnostic_begin("projection_metadata")
            if _DIAGNOSTICS_ENABLED else None
        )
        try:
            roles = tuple(instance.role_by_client[index] for index in subset)
            relation = tuple(
                "goal" if goal and index == goal.owner else "other"
                for index in subset)
            # The GR(1) fixed point enumerates justice records in source order.
            # Its exact rank predicates (and, on invalid multi-hot monitor
            # states, even W*) may retain the stable lowest-index anchor.
            anchor = tuple(index == 0 for index in subset)
            group = (roles, relation, anchor)
            previous = templates.get(group)
            if previous is not None and previous != normalized:
                raise Decline("anti-unify", name, instance.n,
                              f"projections disagree within role class {group}")
            templates[group] = normalized
            normalized_support = frozenset(_support(bdds, normalized))
            templates.supports[group] = normalized_support

            # Rebuild in the seed's concrete variable space for the exact
            # separability check.  Each projection is a conjunct.
        finally:
            if metadata_token is not None:
                _diagnostic_end(metadata_token)
        rebuilt &= bdds.relabel(normalized, reverse, normalized_support)
    if rebuilt != function:
        hint_validity = _hint_one_hot_validity(bdds, instance)
        false = bdds.buddy.bddfalse
        if ((function & bdds.buddy.bdd_not(hint_validity)) == false and
                (hint_validity & rebuilt) == function):
            # The global exactly-one part is a parameterized schema, not a
            # reason to call an otherwise k-local predicate arity n.
            templates[HINT_VALIDITY_GROUP] = None
            return templates
        raise Decline("anti-unify", name, instance.n,
                      f"declared arity k={arity} does not reconstruct predicate")
    return templates


def _hint_one_hot_validity(bdds: Bdds, instance: Instance):
    groups: dict[str, list[int]] = defaultdict(list)
    for item in instance.variables:
        if item.key[:2] == ("state", "bus") and str(item.key[2]).startswith("Hint"):
            groups[str(item.key[2])].append(item.index)
        elif (item.key[:2] == ("state", "monitor") and
              str(item.key[2]).startswith("Hint") and
              str(item.key[2]).endswith(":step")):
            groups[str(item.key[2])[:-5]].append(item.index)
    result = bdds.buddy.bddtrue
    for variables in groups.values():
        result &= _exactly_one(bdds, variables)
    return result


def _one_hot_validity(bdds: Bdds, instance: Instance):
    groups: dict[tuple, list[int]] = defaultdict(list)
    for item in instance.variables:
        if item.key[:2] == ("state", "monitor"):
            groups[item.key[:-1]].append(item.index)
        elif item.key[:2] == ("state", "bus"):
            groups[item.key[:-2] + (item.key[-1],)].append(item.index)
    result = bdds.buddy.bddtrue
    for variables in groups.values():
        atleast = bdds.buddy.bddfalse
        for variable in variables:
            atleast |= bdds.buddy.bdd_ithvar(variable)
        atmost = bdds.buddy.bddtrue
        for left, right in itertools.combinations(variables, 2):
            atmost &= (bdds.buddy.bdd_nithvar(left) |
                       bdds.buddy.bdd_nithvar(right))
        result &= atleast & atmost
    return result


def _instantiate_templates_impl(bdds: Bdds, target: Instance,
                                templates: dict[tuple, object], arity: int,
                                goal: GoalInfo | None = None,
                                context: GeneralizationAttemptContext | None = None):
    result = bdds.buddy.bddtrue
    add_hint_validity = HINT_VALIDITY_GROUP in templates
    for subset0 in itertools.combinations(range(target.n), arity):
        subset = _ordered_subset(target, subset0, goal)
        if _DIAGNOSTICS_ENABLED:
            _diagnostic_record_subset(target.n, subset, "instantiation")
        roles = tuple(target.role_by_client[index] for index in subset)
        relation = tuple("goal" if goal and index == goal.owner else "other"
                         for index in subset)
        anchor = tuple(index == 0 for index in subset)
        group = (roles, relation, anchor)
        if group not in templates:
            raise Decline("instantiate", goal.key if goal else "inv", target.n,
                          f"no template for role class {group}")
        if context is not None and context.enabled:
            metadata = context.subset_metadata(target, subset)
            concrete = metadata["concrete"]
            inverse_normal = context.inverse_normal
        else:
            slots = {client: pos for pos, client in enumerate(subset)}
            concrete = {_normal_key(item.key, slots): item.index
                        for item in target.variables
                        if not item.owners or item.owners <= frozenset(subset)}
            inverse_normal = {value: key for key, value in bdds.normal.items()}
        if isinstance(templates, TemplateSet) and group in templates.supports:
            support = templates.supports[group]
        else:
            support = _support(bdds, templates[group])
        mapping = {}
        for variable in support:
            key = inverse_normal[variable]
            if key not in concrete:
                raise Decline("instantiate", goal.key if goal else "inv", target.n,
                              f"canonical variable {key!r} is absent at target")
            mapping[variable] = concrete[key]
        result &= bdds.relabel(templates[group], mapping, support)
    if add_hint_validity:
        result &= _hint_one_hot_validity(bdds, target)
    return result


def instantiate_templates(bdds: Bdds, target: Instance,
                          templates: dict[tuple, object], arity: int,
                          goal: GoalInfo | None = None,
                          context: GeneralizationAttemptContext | None = None):
    if not _DIAGNOSTICS_ENABLED:
        return _instantiate_templates_impl(
            bdds, target, templates, arity, goal, context)
    token = _diagnostic_begin("instantiate_templates")
    try:
        return _instantiate_templates_impl(
            bdds, target, templates, arity, goal, context)
    finally:
        _diagnostic_end(token)


def _support(bdds: Bdds, function) -> set[int]:
    stats_before = (
        _diagnostic_bdd_snapshot(bdds) if _DIAGNOSTICS_ENABLED else None)
    diagnostic_token = (
        _diagnostic_begin("support_extraction")
        if _DIAGNOSTICS_ENABLED else None
    )
    try:
        node = bdds.buddy.bdd_support(function)
        result = set()
        while node != bdds.buddy.bddtrue and node != bdds.buddy.bddfalse:
            result.add(bdds.buddy.bdd_var(node))
            node = bdds.buddy.bdd_high(node)
        return result
    finally:
        if diagnostic_token is not None:
            _diagnostic_end(diagnostic_token)
            _diagnostic_record_bdd_operation(
                "support_extraction", stats_before,
                _diagnostic_bdd_snapshot(bdds),
            )


def _merge_seed_templates(stage: str, predicate: str,
                          by_seed: list[tuple[int, dict[tuple, object]]],
                          cache_key: tuple | None = None) -> TemplateSet:
    result = TemplateSet(cache_key=cache_key)
    for n, templates in by_seed:
        for key, function in templates.items():
            # Existential projections at a larger seed can strengthen the
            # representative on unreachable encodings while their complete
            # conjunction remains exact.  Stable-regime seeds are processed
            # in increasing n, so retain the most constrained observation;
            # every retained seed is independently reconstructed above and
            # the instantiated candidate remains untrusted until checked.
            result[key] = function
            if isinstance(templates, TemplateSet) and key in templates.supports:
                result.supports[key] = templates.supports[key]
    return result


def _predicate_templates(bdds: Bdds, seeds: list["Instance"], name: str,
                         base_arity: int,
                         goals: list[GoalInfo | None] | None = None,
                         seed_names: list[str] | None = None,
                         context: GeneralizationAttemptContext | None = None,
                         ) -> tuple[dict[tuple, object], int]:
    """Measure one predicate, raising its arity without globalizing the family.

    A fallback arity is learned only from a seed strictly larger than that
    arity.  Thus a predicate whose only exact projection uses all n clients is
    still declined as unbounded rather than being mislabeled fixed-arity.
    """
    goals = goals or [None] * len(seeds)
    seed_names = seed_names or [name] * len(seeds)
    first_failure: Decline | None = None
    upper = min(MAX_PREDICATE_ARITY, max(seed.n for seed in seeds))
    for arity in range(base_arity, upper + 1):
        cache_key = (
            context.predicate_cache_key(
                seeds, name, arity, goals, seed_names)
            if context is not None and context.enabled else None
        )
        cached = (
            context.template_cache_get(cache_key)
            if cache_key is not None else None
        )
        if cached is not None:
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.counters["predicate_template_cache_hits"] += 1
            return cached
        if cache_key is not None and _DIAGNOSTICS is not None:
            _DIAGNOSTICS.counters["predicate_template_cache_misses"] += 1
        eligible = [(seed, goal, seed_name)
                    for seed, goal, seed_name in zip(
                        seeds, goals, seed_names, strict=True)
                    if seed.n >= arity and
                    (arity == base_arity or seed.n > arity)]
        if not eligible:
            continue
        by_seed = []
        failed = None
        for seed, goal, seed_name in eligible:
            try:
                by_seed.append((seed.n, projection_templates(
                    bdds, seed, seed_name, arity, goal, context, cache_key)))
            except Decline as exc:
                failed = exc
                if first_failure is None:
                    first_failure = exc
                break
        if failed is None:
            merged = _merge_seed_templates(
                "anti-unify", name, by_seed, cache_key)
            result = (merged, arity)
            if cache_key is not None:
                context.template_cache_put(cache_key, result)
            return result
    failing_n = first_failure.n if first_failure is not None else max(seed.n for seed in seeds)
    detail = (f"requires arity n={failing_n}; no bounded arity through "
              f"k={upper} reconstructs it")
    raise Decline("anti-unify", name, failing_n, detail)


def _goal_match(goals: list[GoalInfo], key: tuple) -> GoalInfo:
    matches = [goal for goal in goals if goal.key == key]
    if len(matches) != 1:
        raise KeyError(f"goal class {key!r} has {len(matches)} matches")
    return matches[0]












def emit_candidate(bdds: Bdds, target: Instance, out: pathlib.Path,
                   predicates: dict[str, object], levels: list[int], *,
                   real_check: str = "policy") -> tuple[pathlib.Path, pathlib.Path | None]:
    if real_check not in ("policy", "region"):
        raise ValueError(f"unsupported REAL check route {real_check!r}")
    cert_path = out / f"{target.family}_{target.n}.certificate.aag"
    policy_path = out / f"{target.family}_{target.n}.policy.aag"
    cert_inputs = [*target.game.latch_names, *target.game.input_names]
    cert_builder = AagBuilder(cert_inputs)
    cert_outputs = []
    identity = {i: i for i in range(len(cert_inputs))}
    cert_memo: dict[int, int] = {}
    structured_moves = (_structured_arbiter_certificate_moves(
        bdds, target, cert_builder, predicates, levels)
                        if target.family in STRUCTURED_GRANT_FAMILIES else {})
    for name, function in predicates.items():
        if name in structured_moves:
            cert_outputs.append((name, structured_moves[name]))
        else:
            cert_outputs.append((name, bdds.to_aag(
                cert_builder, function, identity, cert_memo)))
    cert_path.write_text(cert_builder.render(
        cert_outputs, "index-aware GR(1) generalized certificate"), encoding="utf-8")
    cert_meta = _certificate_sidecar(target, cert_path, levels,
                                     len(cert_outputs), len(cert_builder.gates))
    if real_check == "region":
        # The policy checker historically inferred the system side from an
        # absent field.  Region-v1 is policy-free and therefore requires the
        # certificate to state its side explicitly.
        cert_meta["side"] = "system"
    pathlib.Path(str(cert_path) + ".json").write_text(
        json.dumps(cert_meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    _trace("certificate emitted")
    if real_check == "region":
        _trace("target policy construction omitted for region checking")
        return cert_path, None
    policy_token = (
        _diagnostic_begin("policy_construction_skolemization")
        if _DIAGNOSTICS_ENABLED else None
    )

    if target.family in STRUCTURED_GRANT_FAMILIES:
        nstate = len(target.game.latches)
        ngoals = len(target.goals)
        uncontrollable = [name for name in target.game.input_names
                          if not name.startswith("controllable_")]
        controls = [name for name in target.game.input_names
                    if name.startswith("controllable_")]
        policy_inputs = [*target.game.latch_names,
                         *(f"curr_{j}" for j in range(ngoals)),
                         *uncontrollable]
        policy_builder = AagBuilder(policy_inputs)
        literals = _structured_arbiter_policy_literals(
            bdds, target, policy_builder, predicates, levels)
        names = [*controls, *(f"curr_next_{j}" for j in range(ngoals))]
        policy_path.write_text(policy_builder.render(
            list(zip(names, literals, strict=True)),
            "canonical lowest-index structured Skolemization"),
            encoding="utf-8")
        policy_meta = _policy_sidecar(
            target, policy_path, len(policy_builder.gates))
        pathlib.Path(str(policy_path) + ".json").write_text(
            json.dumps(policy_meta, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        _trace("structured arbiter policy emitted")
        if policy_token is not None:
            _diagnostic_end(policy_token)
        return cert_path, policy_path

    # Build the relation selected by the effective goal counter.
    nstate = len(target.game.latches)
    ngoals = len(target.goals)
    if bdds.layout is None:
        raise RuntimeError("policy construction requires an active variable layout")
    layout = bdds.layout
    if ngoals != layout.policy_counter.size:
        raise ValueError("policy counter does not match the attempt variable layout")
    # Use a policy-only variable order with the counter before the game.  In
    # the certificate ABI state necessarily comes first, but using that order
    # for the goal mux expands every move relation before inspecting curr.
    public_game_vars = nstate + len(target.game.inputs)
    if public_game_vars != layout.policy_game.size:
        raise ValueError("policy game ABI does not match the attempt variable layout")
    policy_game_map = {i: layout.policy_game.coordinate(i)
                       for i in range(public_game_vars)}
    policy_moves = []
    policy_goals = []
    for j in range(ngoals):
        move_support = _support(bdds, predicates[f"move_{j}"])
        policy_moves.append(bdds.relabel(
            predicates[f"move_{j}"],
            {v: policy_game_map[v] for v in move_support}, move_support))
        goal_support = _support(bdds, predicates[f"goal_{j}"])
        policy_goals.append(bdds.relabel(
            predicates[f"goal_{j}"],
            {v: policy_game_map[v] for v in goal_support}, goal_support))
        _trace(f"policy relation relabelled goal {j + 1}/{ngoals}")
    any_curr = bdds.buddy.bddfalse
    curr = []
    for j in range(ngoals):
        bit = bdds.buddy.bdd_ithvar(layout.policy_counter.coordinate(j))
        curr.append(bit)
        any_curr |= bit
    effective = [curr[0] | bdds.buddy.bdd_not(any_curr), *curr[1:]]
    relation = bdds.buddy.bddfalse
    for j in range(ngoals):
        relation |= effective[j] & policy_moves[j]
    _trace("policy goal relation muxed")

    controls = [layout.policy_game.coordinate(nstate + p)
                for p, name in enumerate(target.game.input_names)
                if name.startswith("controllable_")]
    functions = []
    chosen = bdds.buddy.bddtrue
    control_cube = bdds.cube(controls)
    for pos, control in enumerate(controls):
        allowed = relation & chosen
        function = bdds.and_exist(
            allowed, bdds.buddy.bdd_ithvar(control), control_cube)
        functions.append(function)
        bit = bdds.buddy.bdd_ithvar(control)
        chosen &= ((bit & function) |
                   (bdds.buddy.bdd_not(bit) & bdds.buddy.bdd_not(function)))
        _trace(f"policy control {pos + 1}/{len(controls)} Skolemized")

    # The counter protocol is independent of the Skolem choices.
    next_curr = []
    for j in range(ngoals):
        advance = effective[j] & policy_goals[j]
        prev = (j + ngoals - 1) % ngoals
        prev_advance = effective[prev] & policy_goals[prev]
        next_curr.append((effective[j] & bdds.buddy.bdd_not(advance)) |
                         prev_advance)

    uncontrollable = [(p, name) for p, name in enumerate(target.game.input_names)
                      if not name.startswith("controllable_")]
    policy_inputs = [*target.game.latch_names,
                     *(f"curr_{j}" for j in range(ngoals)),
                     *(name for _p, name in uncontrollable)]
    remap = {layout.policy_game.coordinate(i): i for i in range(nstate)}
    remap.update({layout.policy_counter.coordinate(j): nstate + j
                  for j in range(ngoals)})
    remap.update({layout.policy_game.coordinate(nstate + p): nstate + ngoals + j
                  for j, (p, _name) in enumerate(uncontrollable)})
    policy_builder = AagBuilder(policy_inputs)
    policy_outputs = []
    policy_memo: dict[int, int] | None = (
        None if _reference_aag_context_enabled() else {}
    )
    control_names = [name for name in target.game.input_names
                     if name.startswith("controllable_")]
    for name, function in zip(control_names, functions, strict=True):
        policy_outputs.append((name, bdds.to_aag(
            policy_builder, function, remap, policy_memo)))
    for j, function in enumerate(next_curr):
        policy_outputs.append((f"curr_next_{j}", bdds.to_aag(
            policy_builder, function, remap, policy_memo)))
    policy_path.write_text(policy_builder.render(
        policy_outputs, "canonical lowest-index Skolemization"), encoding="utf-8")
    _trace("policy emitted")
    policy_meta = _policy_sidecar(target, policy_path, len(policy_builder.gates))
    pathlib.Path(str(policy_path) + ".json").write_text(
        json.dumps(policy_meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if policy_token is not None:
        _diagnostic_end(policy_token)
    return cert_path, policy_path




def _exactly_one(bdds: Bdds, variables: list[int]):
    """Linear-size exact-one BDD, avoiding a quadratic pairwise formula."""
    none = bdds.buddy.bddtrue
    one = bdds.buddy.bddfalse | bdds.buddy.bddfalse
    for variable in variables:
        positive = bdds.buddy.bdd_ithvar(variable)
        negative = bdds.buddy.bdd_not(positive)
        one = (one & negative) | (none & positive)
        none &= negative
    return one


def _collector_candidate(
        bdds: Bdds, seeds: list[Instance], target: Instance,
        out: pathlib.Path, stage: dict[str, float], *,
        real_check: str = "policy"
) -> tuple[CandidateSchema, Instance, pathlib.Path, pathlib.Path | None, dict]:
    """Instantiate the collector's bus-wide W schema on semantic states.

    The generated monitor uses one-hot state encodings whose width grows with
    n.  Its stable object is therefore the W automaton semantics, not a bit
    ordinal.  The canonical policy pulses ``allFinished`` exactly when every
    local W monitor is in its reset/accepting state.  Universal predecessor
    layers over the finite semantic product provide an independently checkable
    rank certificate for that policy.
    """
    predicate = "W(AllSeen(finished),allFinished)"
    started = stage.begin("bus_schemas")
    if predicate not in target.bus_schemas:
        raise Decline("bus-wide schemas", predicate, target.n,
                      f"target schemas are {target.bus_schemas!r}")
    if any(predicate not in seed.bus_schemas for seed in seeds):
        failed = next(seed.n for seed in seeds if predicate not in seed.bus_schemas)
        raise Decline("bus-wide schemas", predicate, failed,
                      "semantic schema differs across seeds")
    stage["bus_schemas"] = time.monotonic() - started

    started = stage.begin("anti_unify")
    seed_depths = [tuple(seed.levels(goal.goal) for goal in seed.goals)
                   for seed in seeds]
    if len(set(seed_depths)) != 1:
        failed = next(seed.n for seed, depths in zip(seeds, seed_depths, strict=True)
                      if depths != seed_depths[0])
        raise Decline("ranks", predicate, failed,
                      f"rank depth differs across seeds: {seed_depths}")
    stage["anti_unify"] = time.monotonic() - started

    started = stage.begin("canonicalize")
    monitors = {record["monitor"]: record for record in target.prov["monitors"]}
    bus_records = [record for record in monitors.values()
                   if record["arity_kind"] == "bus_wide"]
    local_records = sorted(
        (record for record in monitors.values()
         if record["arity_kind"] == "local"),
        key=lambda record: tuple(record["index_tuple"]))
    if len(bus_records) != 1 or len(local_records) != target.n:
        raise Decline("canonicalize", predicate, target.n,
                      f"expected one bus and {target.n} local monitors")
    bus = bus_records[0]
    if any(record["state_count"] != 3 for record in local_records):
        raise Decline("canonicalize", "local W monitor", target.n,
                      "expected the measured three-state local ABI")

    def latch_index(record: dict, bit: int) -> int:
        name = f"monitor_{record['monitor']}_state_{bit}"
        try:
            return target.game.latch_names.index(name)
        except ValueError as exc:
            raise Decline("canonicalize", name, target.n,
                          "monitor state is absent from game ABI") from exc

    bus_latches = [latch_index(bus, bit) for bit in range(bus["state_count"])]
    local_latches = [[latch_index(record, bit) for bit in range(3)]
                     for record in local_records]
    input_positions = {name: pos for pos, name in enumerate(target.game.input_names)}
    try:
        finished_positions = [input_positions[f"finished_{i}"]
                              for i in range(target.n)]
        control_position = input_positions["controllable_allFinished"]
    except KeyError as exc:
        raise Decline("canonicalize", str(exc), target.n,
                      "collector letter is absent from game ABI") from exc
    stage["canonicalize"] = (stage.get("canonicalize", 0.0) +
                             time.monotonic() - started)

    started = stage.begin("ranks")
    valuation_count = 1 << target.n
    valuation_mask = (1 << valuation_count) - 1
    env_vectors = [sum(1 << valuation for valuation in range(valuation_count)
                       if valuation & (1 << client))
                   for client in range(target.n)]
    semantic_count = bus["state_count"] * (1 << target.n)
    successors: list[frozenset[int]] = []
    goal_states = [set() for _goal in target.goals]
    latch_values = [False] * len(target.game.latches)
    for bus_state in range(bus["state_count"]):
        for local_mask in range(1 << target.n):
            state_id = bus_state * (1 << target.n) + local_mask
            for index in bus_latches:
                latch_values[index] = False
            latch_values[bus_latches[bus_state]] = True
            for client, group in enumerate(local_latches):
                for index in group:
                    latch_values[index] = False
                local_state = 1 if local_mask & (1 << client) else 0
                latch_values[group[local_state]] = True
            inputs = [0] * len(target.game.inputs)
            for client, position in enumerate(finished_positions):
                inputs[position] = env_vectors[client]
            inputs[control_position] = valuation_mask if local_mask == 0 else 0
            next_vectors, justice_vectors = _aag_vector_evaluate(
                target.game, latch_values, inputs, valuation_mask)
            for goal, vector in enumerate(justice_vectors):
                if vector not in (0, valuation_mask):
                    raise Decline("ranks", f"goal_{goal}", target.n,
                                  "game justice unexpectedly depends on letters")
                if vector:
                    goal_states[goal].add(state_id)
            next_ids = set()
            for valuation in range(valuation_count):
                selected_bus = [bit for bit, index in enumerate(bus_latches)
                                if next_vectors[index] & (1 << valuation)]
                if len(selected_bus) != 1:
                    raise Decline("ranks", predicate, target.n,
                                  "bus successor is not one-hot")
                next_local = 0
                for client, group in enumerate(local_latches):
                    selected = [bit for bit, index in enumerate(group)
                                if next_vectors[index] & (1 << valuation)]
                    if len(selected) != 1 or selected[0] == 2:
                        raise Decline("ranks", "local W invariant", target.n,
                                      "canonical policy leaves the safe local states")
                    if selected[0] == 1:
                        next_local |= 1 << client
                next_ids.add(selected_bus[0] * (1 << target.n) + next_local)
            successors.append(frozenset(next_ids))
    if len(successors) != semantic_count:
        raise AssertionError("collector semantic-state enumeration is incomplete")

    # Greatest generalized-Buechi region under the fixed policy.  Environment
    # choices are the outgoing edges, hence every predecessor test is
    # universal.  This also removes the rejecting one-hot states of the bus
    # monitor without depending on their generated ordinal numbers.
    winning = set(range(semantic_count))
    while True:
        previous_winning = set(winning)
        for accepting0 in goal_states:
            accepting = accepting0 & winning
            reached = set(accepting)
            while True:
                expanded = reached | {
                    state for state in winning
                    if successors[state] <= reached
                }
                if expanded == reached:
                    break
                reached = expanded
            winning &= reached
        if winning == previous_winning:
            break
    reset_bus = next((bit for bit, index in enumerate(bus_latches)
                      if target.game.latches[index][2] != 0), None)
    if reset_bus is None:
        # ASCII AIGER's omitted/zero reset means state bit zero; the monitor
        # encoder has exactly one explicit true reset bit.
        reset_bus = 0
    reset_state = reset_bus * (1 << target.n)
    if reset_state not in winning:
        raise Decline("ranks", predicate, target.n,
                      "canonical policy's generalized-Buechi region excludes reset")
    reachable = {reset_state}
    while True:
        expanded = reachable | set().union(
            *(successors[state] for state in reachable))
        if expanded == reachable:
            break
        reachable = expanded
    if not reachable <= winning:
        raise Decline("ranks", predicate, target.n,
                      "canonical policy reaches a state outside its winning region")
    # A certificate needs an inductive region containing reset, not the
    # maximal winning region.  The reachable closure is both stronger and far
    # cheaper for the independent checker to compose with 2^n bus latches.
    winning = reachable

    rank_sets: list[list[set[int]]] = []
    for goal, accepting0 in enumerate(goal_states):
        accepting = accepting0 & winning
        covered: set[int] = set()
        levels = []
        while covered != winning:
            allowed = accepting | covered
            current = set(accepting)
            current.update(state for state in winning
                           if successors[state] <= allowed)
            if current == covered:
                raise Decline("ranks", f"goal_{goal}", target.n,
                              f"canonical policy rank stalls after {len(levels)} levels")
            levels.append(current)
            covered = current
            if len(levels) > semantic_count:
                raise AssertionError("collector rank iteration did not converge")
        rank_sets.append(levels)
    stage["ranks"] = time.monotonic() - started

    started = stage.begin("instantiate")
    def state_set(members: set[int]):
        # Positive one-hot literals are sufficient because every obligation
        # intersects the invariant; keeping the formula positive also yields
        # compact, monotone BDDs for the 2^n-state bus monitor.
        result = bdds.buddy.bddfalse | bdds.buddy.bddfalse
        local_width = 1 << target.n
        by_bus: dict[int, set[int]] = defaultdict(set)
        for state_id in members:
            bus_state, local_mask = divmod(state_id, local_width)
            by_bus[bus_state].add(local_mask)
        all_local_masks = set(range(local_width))
        for bus_state, local_masks in sorted(by_bus.items()):
            local_predicate = bdds.buddy.bddfalse | bdds.buddy.bddfalse
            if local_masks == all_local_masks:
                local_predicate = bdds.buddy.bddtrue
            else:
                for local_mask in sorted(local_masks):
                    cube = bdds.buddy.bddtrue
                    for client, group in enumerate(local_latches):
                        cube &= bdds.buddy.bdd_ithvar(
                            group[1 if local_mask & (1 << client) else 0])
                    local_predicate |= cube
            result |= (bdds.buddy.bdd_ithvar(bus_latches[bus_state]) &
                       local_predicate)
        return result

    winning_bus_states = sorted({state // (1 << target.n) for state in winning})
    inv = _exactly_one(
        bdds, [bus_latches[state] for state in winning_bus_states])
    for state, variable in enumerate(bus_latches):
        if state not in winning_bus_states:
            inv &= bdds.buddy.bdd_nithvar(variable)
    for group in local_latches:
        inv &= _exactly_one(bdds, group)
        inv &= bdds.buddy.bdd_nithvar(group[2])
    # If reachability couples a bus state to only some local masks, retain the
    # coupling.  (The measured collector instances have the simpler complete
    # product, but this assertion keeps the emitted invariant honest.)
    inv &= state_set(winning)
    policy = bdds.buddy.bddtrue
    for group in local_latches:
        policy &= bdds.buddy.bdd_ithvar(group[0])

    _next, _bad, exact_goals, _fairness = bdds.game_functions(target.game)
    predicates: dict[str, object] = {"inv": inv}
    levels = [len(items) for items in rank_sets]
    for j, goal in enumerate(exact_goals):
        predicates[f"goal_{j}"] = goal
    for j, rows in enumerate(rank_sets):
        previous_rank = bdds.buddy.bddfalse | bdds.buddy.bddfalse
        for k, members in enumerate(rows):
            raw_rank = state_set(members)
            # Values outside inv are don't-cares for every proof obligation.
            # BuDDy's restrict-aware simplifier finds a compact representative
            # over the one-hot semantic region; unioning the previous level
            # preserves the checker's global monotonicity requirement.
            rank = previous_rank | bdds.buddy.bdd_simplify(raw_rank, inv)
            predicates[f"y_{j}_{k}"] = rank
            predicates[f"x_{j}_{k}_0"] = rank
            previous_rank = rank
    control = bdds.buddy.bdd_ithvar(
        len(target.game.latches) + control_position)
    move = inv & ((control & policy) |
                  (bdds.buddy.bdd_not(control) & bdds.buddy.bdd_not(policy)))
    for j in range(len(target.goals)):
        predicates[f"move_{j}"] = move
    if os.environ.get("GENERALIZE_GR1_TRACE"):
        _trace("collector predicate nodes " + ", ".join(
            f"{name}={bdds.buddy.bdd_nodecount(function)}"
            for name, function in predicates.items()))

    ordered = {"inv": predicates["inv"]}
    for j in range(len(target.goals)):
        ordered[f"goal_{j}"] = predicates[f"goal_{j}"]
    for j, depth in enumerate(levels):
        for k in range(depth):
            ordered[f"y_{j}_{k}"] = predicates[f"y_{j}_{k}"]
    for j, depth in enumerate(levels):
        for k in range(depth):
            ordered[f"x_{j}_{k}_0"] = predicates[f"x_{j}_{k}_0"]
    for j in range(len(target.goals)):
        ordered[f"move_{j}"] = predicates[f"move_{j}"]
    cert, policy_path = emit_candidate(
        bdds, target, out, ordered, levels, real_check=real_check)
    stage["instantiate"] = time.monotonic() - started
    candidate = CandidateSchema(
        target.family, REAL_FAMILIES[target.family].arity,
        tuple(seed.n for seed in seeds),
        tuple(f"role_{index}" for index in range(
            measured_role_class_count(target.family) or 0)),
        target.bus_schemas,
        (("bus_semantic_states", bus["state_count"]),
         ("move_reconstructed", len(target.goals)),
         ("rank", sum(levels)),
         ("winning_bus_states", len(winning_bus_states)),
         ("winning_semantic_states", len(winning))))
    evidence = {
        "format": "acacia-param-lift-gr1-evidence-v1",
        "compose_route": _compose_route(),
        "real_check": real_check,
        "family": target.family, "target": target.n,
        "seeds": [seed.n for seed in seeds], "arity": candidate.arity,
        "variable_layout": (
            bdds.layout.description() if bdds.layout is not None else None),
        "role_classes": list(candidate.role_classes),
        "bus_schemas": list(candidate.bus_schemas),
        "template_counts": dict(candidate.template_counts),
        "rank_depths": levels,
        "stages": ["seed", "canonicalize", "anti-unify",
                   "bus-wide schemas", "ranks", "instantiate", "CEGIS"],
        "stage_evidence": {
            "seed": {"sizes": [seed.n for seed in seeds],
                     "exports": ["policy", "certificate", "provenance"]},
            "canonicalize": {"variable_abi":
                             "(template,index_tuple,state_bit)"},
            "anti-unify": {"arity": candidate.arity,
                           "role_classes": list(candidate.role_classes)},
            "bus-wide schemas": {"matched": list(candidate.bus_schemas)},
            "ranks": {"depths": levels,
                      "construction": "universal semantic attractor"},
            "instantiate": {
                "certificate": cert.name,
                "policy": policy_path.name if policy_path is not None else None,
                "exports": (["certificate", "policy"]
                            if policy_path is not None else ["certificate"]),
                "policy_omitted": policy_path is None,
            },
            "CEGIS": {"round_cap": MAX_CEGIS_ROUNDS},
        },
        "canonical_variable": "(template,index_tuple,state_bit)",
        "bus_instantiation": (
            "semantic one-hot W automaton under the canonical allFinished rule"),
        "y_reconstruction": "union_i x_j_k_i",
    }
    if policy_path is not None:
        evidence["skolem_rule"] = (
            "allFinished iff every local W monitor is in state 0")
    (out / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return candidate, target, cert, policy_path, {"times": stage,
                                                   "evidence": evidence}


def acquire_small_instances(
    family: str,
    seed_ns: tuple[int, ...],
    out: pathlib.Path,
    limits: ProposerLimits,
    deadline: AbsoluteDeadline,
    *,
    family_source: pathlib.Path | None = None,
    existing: SeedBundle | None = None,
) -> SeedBundle:
    """Solve only seed sizes absent from an invocation-local artifact bundle."""
    out.mkdir(parents=True, exist_ok=True)
    if existing is not None and existing.family != family:
        raise ValueError("cannot reuse a seed bundle from another family")
    instances = dict(existing.instances) if existing is not None else {}
    requested = tuple(sorted(set(seed_ns)))
    started = time.monotonic()
    _set_active_progress_stage("seed", started)
    solved: list[int] = []
    try:
        for n in requested:
            if n in instances:
                continue
            deadline.timeout_s(limits.checker_timeout_s, "seed")
            instances[n] = solve_seed(
                family, n, out, limits, family_source=family_source,
                deadline=deadline,
            )
            solved.append(n)
    finally:
        _clear_active_progress_stage("seed")
    elapsed = (existing.elapsed_s if existing is not None else 0.0)
    elapsed += time.monotonic() - started
    return SeedBundle(family, instances, tuple(solved), elapsed)


def learn_schema(
    seed_bundle: SeedBundle,
    limits: ProposerLimits,
    deadline: AbsoluteDeadline,
) -> SchemaBundle:
    """Run target-independent eligibility, arity, role, and seed-ABI checks."""
    del limits
    family = seed_bundle.family
    deadline.timeout_s(1.0, "learn_schema")
    seeds = seed_bundle.seeds
    if not seeds:
        raise Decline("seed", "seed set", None,
                      "at least one stable seed is required")
    stable = stable_from(family)
    below = [seed.n for seed in seeds if seed.n < stable]
    if below:
        raise Decline("seed", "stable_from", below[0],
                      f"seed is below measured stable_from={stable}")
    if family == "round_robin_arbiter_unreal2" or family in OUT_OF_SCOPE:
        raise Decline("scope", "arity measurement", None,
                      "family has no fixed-arity lifting schema")
    if family not in REAL_FAMILIES:
        raise Decline("scope", "arity measurement", None,
                      "family has no fixed-arity M4 measurement")
    arity = REAL_FAMILIES[family].arity
    measured = measured_arity(family)
    if measured != arity:
        raise Decline("scope", "arity measurement", None,
                      f"expected k={arity}, measured {measured}")
    measured_roles = measured_role_class_count(family)
    role_counts = [_provenance_role_class_count(seed) for seed in seeds]
    if measured_roles is None or len(set(role_counts)) != 1 or (
            role_counts[0] != measured_roles):
        raise Decline(
            "anti-unify", "role classes", None,
            "role-class count is not constant across seeds and equal to the "
            f"measurement: observed={role_counts}, measured={measured_roles}",
        )
    seed_bus_shapes = tuple(
        frozenset(item.key for item in seed.variables
                  if item.key[:2] == ("state", "bus"))
        for seed in seeds
    )
    return SchemaBundle(
        family, seed_bundle, arity, measured_roles, seed_bus_shapes,
    )


def instantiate(
                    schema: SchemaBundle, target_n: int,
                    out: pathlib.Path, limits: ProposerLimits,
                    deadline: AbsoluteDeadline, *,
                    family_source: pathlib.Path | None = None,
                    target_source: pathlib.Path | None = None,
                    reduction_semantics: str = "exact",
                    real_check: str = "policy") -> tuple[CandidateSchema, Instance,
                                                          pathlib.Path,
                                                          pathlib.Path | None,
                                                          dict]:
    family = schema.family
    seeds = schema.seed_bundle.seeds
    seed_ns = tuple(seed.n for seed in seeds)
    if target_n in schema.seed_bundle.instances:
        raise Decline("seed", "target", target_n,
                      "the requested target must never be used as a seed")
    if target_n <= max(seed_ns):
        raise Decline("seed", "target", target_n,
                      "target must be strictly larger than every seed")
    out.mkdir(parents=True, exist_ok=True)
    stage = StageTimes()
    stage["seed"] = schema.seed_bundle.elapsed_s
    started = stage.begin("canonicalize")
    target_game, target_prov = build_game(
        family, target_n, out, limits.checker_timeout_s, stage="canonicalize",
        source=target_source or family_source,
        reduction_semantics=reduction_semantics, deadline=deadline)
    target = Instance.load(family, target_n, target_game, target_prov)
    if _DIAGNOSTICS_ENABLED:
        _diagnostic_register_instance(target, "target")
    role_counts = [
        *(_provenance_role_class_count(seed) for seed in seeds),
        _provenance_role_class_count(target),
    ]
    measured_roles = schema.measured_roles
    if len(set(role_counts)) != 1 or role_counts[0] != measured_roles:
        raise Decline("anti-unify", "role classes", target_n,
                      "role-class count is not constant across seeds/target "
                      f"and equal to the measurement: observed={role_counts}, "
                      f"measured={measured_roles}")

    if family == "collector_v1":
        stage["canonicalize"] = time.monotonic() - started
        bdds = Bdds()
        context = bdds.begin_attempt([], target)
        try:
            return _collector_candidate(
                bdds, seeds, target, out, stage, real_check=real_check)
        finally:
            context.release()
            bdds.close()

    bdds = Bdds()
    context = bdds.begin_attempt(seeds, target)
    try:
        arity = schema.arity
        stage["canonicalize"] = time.monotonic() - started

        started = stage.begin("bus_schemas")
        # Stable bus schemas must have the same state ABI.  Variable-count
        # changes are semantic, not a license to align by ordinal.
        seed_bus_shapes = [set(shape) for shape in schema.seed_bus_shapes]
        target_bus_shapes = {item.key for item in target.variables
                             if item.key[:2] == ("state", "bus")}
        # At the smallest seed an arity-2 formula can coincidentally span the
        # whole bus.  It is routed pairwise above, so allow a seed to expose a
        # subset of the stable bus ABI; collectively the seeds must still
        # account for exactly the target schemas.
        seed_bus_union = set().union(*seed_bus_shapes) if seed_bus_shapes else set()
        if (any(not shape <= target_bus_shapes for shape in seed_bus_shapes) or
                seed_bus_union != target_bus_shapes):
            raise Decline("bus-wide schemas", "state ABI", target_n,
                          "matched bus schema has a size-dependent monitor-state encoding")
        stage["bus_schemas"] = time.monotonic() - started

        started = stage.begin("anti_unify")
        predicate_arities: dict[str, int] = {}
        inv_templates, inv_arity = _predicate_templates(
            bdds, seeds, "inv", arity, context=context)
        predicate_arities["inv"] = inv_arity
        inv = instantiate_templates(
            bdds, target, inv_templates, inv_arity, context=context)
        stage["anti_unify"] = time.monotonic() - started

        # Each target goal is matched by its monitor template.  Depth and all
        # X/move templates must agree across seeds of the same goal class.
        started = stage.begin("ranks")
        predicates: dict[str, object] = {"inv": inv}
        levels: list[int] = []
        template_tally = Counter({"inv": len(inv_templates)})
        next_state, game_bad, exact_goals, exact_fairness = bdds.game_functions(
            target.game)
        nstate = len(target.game.latches)
        structured_moves = family in STRUCTURED_GRANT_FAMILIES
        for target_goal in target.goals:
            seed_goals = []
            for seed in seeds:
                matches = [goal for goal in seed.goals if goal.key == target_goal.key]
                if target_goal.owner is not None:
                    # Use the corresponding role, not the same concrete index.
                    desired_role = target.role_by_client[target_goal.owner]
                    matches = [goal for goal in matches if goal.owner is not None and
                               seed.role_by_client[goal.owner] == desired_role]
                    anchored = target_goal.owner == 0
                    same_anchor = [goal for goal in matches
                                   if (goal.owner == 0) == anchored]
                    if same_anchor:
                        matches = same_anchor
                if not matches:
                    raise Decline("ranks", repr(target_goal.key), seed.n,
                                  "goal role class absent from seed")
                seed_goals.append(matches[0])
            depths = [seed.levels(goal.goal) for seed, goal in zip(seeds, seed_goals)]
            if len(set(depths)) != 1:
                raise Decline("ranks", repr(target_goal.key),
                              seeds[depths.index(max(depths))].n,
                              f"rank depth differs across seeds: {depths}")
            # Goals are specification predicates, not learned solution
            # predicates.  Copy the target justice literal exactly.
            j = target_goal.goal
            goal_pred = exact_goals[j]
            predicates[f"goal_{j}"] = goal_pred

            # Anti-unify every retained ν-level.  The corresponding μ-level
            # is definitionally the union across fairness assumptions and is
            # deliberately reconstructed rather than learned independently.
            depth = depths[0]
            levels.append(depth)
            for level in range(depth):
                row = []
                for fair in range(max(1, target.fairness)):
                    predicate_name = f"x_{j}_{level}_{fair}"
                    templates, predicate_arity = _predicate_templates(
                        bdds, seeds, predicate_name, arity, seed_goals,
                        [f"x_{seed_goal.goal}_{level}_{fair}"
                         for seed_goal in seed_goals], context)
                    predicate_arities[predicate_name] = predicate_arity
                    x = instantiate_templates(
                        bdds, target, templates, predicate_arity, target_goal,
                        context)
                    predicates[predicate_name] = x
                    row.append(x)
                    template_tally["rank"] += len(templates)
                union = bdds.buddy.bddfalse
                for x in row:
                    union |= x
                predicates[f"y_{j}_{level}"] = union

            _trace(f"target goal {j + 1}/{len(target.goals)} ranks instantiated")

        # Validate every rank predicate before spending time composing moves.
        # This is the same pre-Skolem relation exported by M2 and avoids both
        # transferring seed tie-breaking and doing irrelevant work before a
        # later predicate-specific arity decline.
        if not structured_moves:
            if bdds.layout is None:
                raise RuntimeError("move construction requires an active variable layout")
            layout = bdds.layout
            # Move construction needs T[s:=next].  Use an interleaved internal
            # order (s0,s0',s1,s1',...,letters) so substitution does not
            # create a far-away-auxiliary intermediate.
            internal_map = {
                i: layout.composition_current(i) for i in range(nstate)
            }
            internal_map.update({nstate + p: layout.composition_letter(p)
                                 for p in range(len(target.game.inputs))})
            public_map = {value: key for key, value in internal_map.items()}

            def internal(function):
                support = _support(bdds, function)
                return bdds.relabel(
                    function, {v: internal_map[v] for v in support}, support)

            internal_next = [internal(function) for function in next_state]
            internal_inv = internal(inv)
            internal_not_bad = bdds.buddy.bdd_not(internal(game_bad))
            internal_state = [
                layout.composition_current(i) for i in range(nstate)
            ]
            internal_temp = [
                layout.composition_temporary(i) for i in range(nstate)
            ]
            internal_w_safe = internal_not_bad & bdds.substitute_variables(
                internal_inv, internal_state, internal_next, internal_temp)
            _trace("target invariant transition composed")
        for target_goal, depth in zip(target.goals, levels, strict=True):
            j = target_goal.goal
            if structured_moves:
                # The exact relation is composed as an AIG in emit_candidate;
                # retaining a placeholder here preserves output ordering.
                predicates[f"move_{j}"] = bdds.buddy.bddfalse
            else:
                goal_pred = predicates[f"goal_{j}"]
                at_goal = internal_inv & internal(goal_pred)
                move = at_goal & internal_w_safe
                covered = at_goal
                for level in range(depth):
                    strict = (at_goal if level == 0 else at_goal |
                              internal(predicates[f"y_{j}_{level - 1}"]))
                    for fair in range(max(1, target.fairness)):
                        x = internal(predicates[f"x_{j}_{level}_{fair}"])
                        layer = x & bdds.buddy.bdd_not(covered)
                        fair_pred = (exact_fairness[fair] if exact_fairness
                                     else bdds.buddy.bddtrue)
                        rank_target = strict | (
                            bdds.buddy.bdd_not(internal(fair_pred)) & x)
                        step = internal_not_bad & bdds.substitute_variables(
                            rank_target, internal_state, internal_next,
                            internal_temp)
                        move |= layer & step
                        covered |= x
                move_support = _support(bdds, move)
                predicates[f"move_{j}"] = bdds.relabel(
                    move, {v: public_map[v] for v in move_support},
                    move_support)
            template_tally["move_reconstructed"] += 1
            _trace(f"target goal {j + 1}/{len(target.goals)} move instantiated")
        stage["ranks"] = time.monotonic() - started

        # Checker requires a stable output order.
        ordered = {"inv": predicates["inv"]}
        for j in range(len(target.goals)):
            ordered[f"goal_{j}"] = predicates[f"goal_{j}"]
        for j, depth in enumerate(levels):
            for k in range(depth):
                ordered[f"y_{j}_{k}"] = predicates[f"y_{j}_{k}"]
        for j, depth in enumerate(levels):
            for k in range(depth):
                for fair in range(max(1, target.fairness)):
                    ordered[f"x_{j}_{k}_{fair}"] = predicates[f"x_{j}_{k}_{fair}"]
        for j in range(len(target.goals)):
            ordered[f"move_{j}"] = predicates[f"move_{j}"]
        started = stage.begin("instantiate")
        cert, policy = emit_candidate(
            bdds, target, out, ordered, levels, real_check=real_check)
        stage["instantiate"] = time.monotonic() - started
        candidate = CandidateSchema(
            family, max(predicate_arities.values()), seed_ns,
            tuple(f"role_{index}" for index in range(measured_roles)),
            target.bus_schemas, tuple(sorted(template_tally.items())),
            tuple(sorted(predicate_arities.items())))
        evidence = {"format": "acacia-param-lift-gr1-evidence-v1",
                    "compose_route": _compose_route(),
                    "real_check": real_check,
                    "family": family, "target": target_n,
                    "seeds": list(seed_ns), "arity": candidate.arity,
                    "predicate_arities": dict(candidate.predicate_arities),
                    "variable_layout": context.layout.description(),
                    "role_classes": list(candidate.role_classes),
                    "bus_schemas": list(candidate.bus_schemas),
                    "template_counts": dict(candidate.template_counts),
                    "rank_depths": levels,
                    "stages": ["seed", "canonicalize", "anti-unify",
                               "bus-wide schemas", "ranks", "instantiate",
                               "CEGIS"],
                    "stage_evidence": {
                        "seed": {"sizes": list(seed_ns),
                                 "exports": ["policy", "certificate",
                                             "provenance"]},
                        "canonicalize": {"variable_abi":
                                         "(template,index_tuple,state_bit)"},
                        "anti-unify": {"arity": candidate.arity,
                                       "predicate_arities":
                                           dict(candidate.predicate_arities),
                                       "template_counts":
                                           dict(candidate.template_counts),
                                       "role_classes":
                                           list(candidate.role_classes)},
                        "bus-wide schemas": {
                            "matched": list(candidate.bus_schemas)},
                        "ranks": {"depths": levels,
                                  "y": "union_i x_j_k_i"},
                        "instantiate": {
                            "certificate": cert.name,
                            "policy": policy.name if policy is not None else None,
                            "exports": (["certificate", "policy"]
                                        if policy is not None
                                        else ["certificate"]),
                            "policy_omitted": policy is None,
                        },
                        "CEGIS": {"round_cap": MAX_CEGIS_ROUNDS},
                    },
                    "canonical_variable": "(template,index_tuple,state_bit)",
                    "y_reconstruction": "union_i x_j_k_i"}
        if policy is not None:
            evidence["skolem_rule"] = (
                "game controllable order; true/lowest index first")
        (out / "evidence.json").write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return candidate, target, cert, policy, {"times": stage, "evidence": evidence}
    finally:
        context.release()
        bdds.close()


def generalize_once(
    family: str,
    target_n: int,
    seed_ns: tuple[int, ...],
    out: pathlib.Path,
    limits: ProposerLimits,
    *,
    family_source: pathlib.Path | None = None,
    target_source: pathlib.Path | None = None,
    reduction_semantics: str = "exact",
    deadline: AbsoluteDeadline | None = None,
    seed_bundle: SeedBundle | None = None,
    real_check: str = "policy",
) -> tuple[CandidateSchema, Instance, pathlib.Path, pathlib.Path | None, dict]:
    """Compatibility composition of the four explicit orchestration stages."""
    absolute = deadline or AbsoluteDeadline.after(limits.checker_timeout_s)
    acquired = acquire_small_instances(
        family, seed_ns, out, limits, absolute,
        family_source=family_source, existing=seed_bundle,
    )
    schema = learn_schema(acquired, limits, absolute)
    return instantiate(
        schema, target_n, out, limits, absolute,
        family_source=family_source, target_source=target_source,
        reduction_semantics=reduction_semantics,
        real_check=real_check,
    )


def _checker_stats_options(path: pathlib.Path) -> list[str]:
    if _CHECKER_STATS_MODE == "file_equals":
        return [f"--stats={path}"]
    if _CHECKER_STATS_MODE == "file_separate":
        return ["--stats", str(path)]
    if _CHECKER_STATS_MODE == "stream":
        return ["--stats"]
    return []


def _parse_stream_json(text: str) -> dict | None:
    candidates = [text.strip(), *reversed(text.splitlines())]
    for candidate in candidates:
        try:
            value = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            return value
    line = next((item for item in text.splitlines()
                 if item.startswith("TLSFCERTCHECK_STATS ")), "")
    if line:
        result: dict[str, object] = {}
        for field in line.split()[1:]:
            if "=" not in field:
                continue
            key, raw = field.split("=", 1)
            try:
                value: object = int(raw)
            except ValueError:
                try:
                    value = float(raw)
                except ValueError:
                    value = raw
            result[key] = value
        if result:
            return result
    return None


def _record_checker_stats(
    stats_path: pathlib.Path, proc: subprocess.CompletedProcess, label: str,
    node_cap: int, side: str, mode_count: int,
) -> None:
    if not _DIAGNOSTICS_ENABLED or _DIAGNOSTICS is None:
        return
    try:
        payload = None
        error = None
        if _CHECKER_STATS_MODE in ("file_equals", "file_separate"):
            if stats_path.is_file():
                try:
                    value = json.loads(stats_path.read_text(encoding="utf-8"))
                    payload = value if isinstance(value, dict) else None
                    if payload is None:
                        error = "stats JSON is not an object"
                except (OSError, json.JSONDecodeError) as exc:
                    error = f"{type(exc).__name__}: {exc}"
                finally:
                    try:
                        stats_path.unlink(missing_ok=True)
                    except OSError:
                        pass
            else:
                error = "checker did not create the advertised stats file"
        elif _CHECKER_STATS_MODE == "stream":
            payload = _parse_stream_json(proc.stderr) or _parse_stream_json(proc.stdout)
            if payload is None:
                error = "checker stats stream contained no JSON object"
        checker_stats = _DIAGNOSTICS.extra.setdefault(
            "checker_stats",
            {"supported": False, "mode": None, "attempts": []},
        )
        if not isinstance(checker_stats, dict):
            raise TypeError("checker_stats field is not an object")
        attempts = checker_stats.setdefault("attempts", [])
        if not isinstance(attempts, list):
            raise TypeError("checker_stats attempts field is not a list")
        attempts.append({
            "label": label,
            "certificate_side": side,
            "mode_count": mode_count,
            "node_cap": node_cap,
            "returncode": proc.returncode,
            "stats": payload,
            "error": error,
        })
    except Exception as error:
        _DIAGNOSTICS.record_error("record_checker_stats", error)


def _recoverable_checker_capacity_failure(
    proc: subprocess.CompletedProcess,
    stats: dict | None = None,
    *,
    method: str | None = None,
    requested_node_cap: int | None = None,
) -> bool:
    """Recognize only explicit capacity exhaustion, never generic UNKNOWN."""
    if proc.returncode != 3:
        return False
    if (
        method == "region"
        and isinstance(stats, dict)
        and stats.get("format") == "tlsf-gr1-region-checkresult-v1"
        and stats.get("method") == "gr1-region-v1"
        and stats.get("verdict") == "UNKNOWN"
        and stats.get("exit_code") == 3
        and isinstance(stats.get("peak_bdd_nodes"), int)
        and isinstance(requested_node_cap, int)
        and requested_node_cap > 0
        and stats["peak_bdd_nodes"] >= requested_node_cap
    ):
        # Region-v1 currently has no separate JSON failure-reason field.  Its
        # peak is sampled from the same OxiDD manager governed by --node-cap,
        # so reaching the requested cap is the method-specific capacity
        # signal.  Requiring the exact versioned UNKNOWN envelope prevents a
        # generic checker failure from entering the retry path.
        return True
    if isinstance(stats, dict):
        reason = " ".join(
            str(stats.get(key, ""))
            for key in ("reason", "failure", "status", "error")
        )
    else:
        reason = ""
    detail = f"{reason}\n{proc.stderr}\n{proc.stdout}".lower()
    if "timeout" in detail or "deadline" in detail:
        return False
    return bool(re.search(
        r"(?:oxidd\s+capacity|node(?:-cap| cap| capacity)|out of nodes|"
        r"unique table (?:full|capacity)|capacity while compiling)",
        detail,
    ))


def _memory_headroom_bytes() -> int | None:
    """Return conservative live cgroup/host headroom when it is observable."""
    candidates: list[int] = []
    try:
        relative = ""
        for line in pathlib.Path("/proc/self/cgroup").read_text(
                encoding="utf-8").splitlines():
            fields = line.split(":", 2)
            if len(fields) == 3 and fields[0] == "0":
                relative = fields[2].lstrip("/")
                break
        cgroup = pathlib.Path("/sys/fs/cgroup") / relative
        maximum_raw = (cgroup / "memory.max").read_text(encoding="utf-8").strip()
        current = int((cgroup / "memory.current").read_text(
            encoding="utf-8").strip())
        if maximum_raw != "max":
            candidates.append(max(0, int(maximum_raw) - current))
    except (OSError, ValueError):
        pass
    try:
        available_kib = next(
            int(line.split()[1])
            for line in pathlib.Path("/proc/meminfo").read_text(
                encoding="utf-8").splitlines()
            if line.startswith("MemAvailable:")
        )
        candidates.append(available_kib * 1024)
    except (OSError, ValueError, StopIteration):
        pass
    return min(candidates) if candidates else None


def _checker_retry_allowed(
    deadline: AbsoluteDeadline,
    first_elapsed_s: float,
) -> tuple[bool, str]:
    needed_time = max(1.0, first_elapsed_s * 1.25)
    if deadline.remaining_s() < needed_time:
        return False, "insufficient_absolute_deadline"
    headroom = _memory_headroom_bytes()
    if headroom is None:
        return False, "memory_headroom_unknown"
    # This is a policy floor, not a conversion from OxiDD nodes to bytes.
    if headroom < (1 << 30):
        return False, "insufficient_memory_headroom"
    return True, "diagnosed_capacity_with_time_and_memory_headroom"


def check_candidate(target: Instance, cert: pathlib.Path,
                    policy: pathlib.Path | None,
                    method: str, limits: ProposerLimits, label: str,
                    deadline: AbsoluteDeadline | None = None) -> dict:
    if method != "region" and policy is None:
        raise ValueError(f"checker method {method!r} requires a policy artifact")
    absolute = deadline or AbsoluteDeadline.after(limits.checker_timeout_s)
    json_out = cert.parent / f"check-{label}.json"
    initial_cap = limits.check_capacity(target.n)
    started = time.monotonic()
    attempts = []
    proc = None
    command = []
    progress_stage = (
        "target_check" if label.startswith("target-") else "probe_check")
    _set_active_progress_stage(progress_stage, started)
    diagnostic_token = (
        _diagnostic_begin("target_check")
        if _DIAGNOSTICS_ENABLED and label.startswith("target-") else None
    )
    node_cap = initial_cap
    for attempt_index in range(2):
        attempt_started = time.monotonic()
        json_out.unlink(missing_ok=True)
        checker_timeout = absolute.timeout_s(
            limits.checker_timeout_s, progress_stage, reserve_s=0.05)
        command = [str(CHECKER), "--method", method, "--timeout",
                   str(checker_timeout), "--node-cap", str(node_cap),
                   "--json-out", str(json_out), "--certificate", str(cert),
                   "--certificate-json", str(cert) + ".json"]
        if _DIAGNOSTICS_ENABLED:
            stats_path = cert.parent / f".s0-checker-stats-{label}-{node_cap}.json"
            command.extend(_checker_stats_options(stats_path))
            mode_count = _diagnostic_record_checker_attempt(
                target, "system", label, node_cap)
        command.append(str(target.game_path))
        if method != "region":
            assert policy is not None
            command.append(str(policy))
        proc = _run(command, absolute.timeout_s(
            checker_timeout + 10, progress_stage))
        if _DIAGNOSTICS_ENABLED:
            _record_checker_stats(
                stats_path, proc, label, node_cap, "system", mode_count)
        attempt = {"node_cap": node_cap, "returncode": proc.returncode,
                   "retry": False, "retry_reason": None}
        attempts.append(attempt)
        attempt_payload = None
        if json_out.is_file():
            try:
                value = json.loads(json_out.read_text(encoding="utf-8"))
                attempt_payload = value if isinstance(value, dict) else None
            except json.JSONDecodeError:
                pass
        if attempt_index or not _recoverable_checker_capacity_failure(
                proc, attempt_payload, method=method,
                requested_node_cap=node_cap):
            break
        allowed, reason = _checker_retry_allowed(
            absolute, time.monotonic() - attempt_started)
        attempt["retry"] = allowed
        attempt["retry_reason"] = reason
        if not allowed:
            break
        node_cap *= 2
    if (proc is not None and proc.returncode == 124 and
            _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None):
        _DIAGNOSTICS.censor_active("subprocess_timeout")
    assert proc is not None
    elapsed = time.monotonic() - started
    _COST_TIMES[
        progress_stage
    ] += elapsed
    if diagnostic_token is not None:
        _diagnostic_end(diagnostic_token)
    _clear_active_progress_stage(progress_stage)
    payload = None
    if json_out.exists():
        try:
            payload = json.loads(json_out.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            payload = None
    output_lines = [line for line in proc.stdout.splitlines() if line.strip()]
    result_string = output_lines[-1].strip() if output_lines else None
    region_verified = (
        method == "region"
        and proc.returncode == 0
        and result_string == "REGION_VERIFIED"
        and isinstance(payload, dict)
        and payload.get("format") == "tlsf-gr1-region-checkresult-v1"
        and payload.get("method") == "gr1-region-v1"
        and payload.get("verdict") == "REGION_VERIFIED"
        and payload.get("exit_code") == 0
    )
    proof_verified = region_verified if method == "region" else proc.returncode == 0
    return {"returncode": proc.returncode,
            "verdict": VERDICTS.get(proc.returncode, "UNKNOWN"),
            "requested_method": method,
            "proof_method": ("gr1-region-v1" if method == "region" else method),
            "result_string": result_string,
            "proof_verified": proof_verified,
            "started_monotonic_s": started,
            "elapsed_s": elapsed, "stdout": proc.stdout, "stderr": proc.stderr,
            "json": payload, "command": command, "attempts": attempts,
            "node_caps": [attempt["node_cap"] for attempt in attempts]}


def check_target(
    actual_spec: Instance,
    candidate_bundle: CandidateBundle,
    deadline: AbsoluteDeadline,
    limits: ProposerLimits,
    method: str = "auto",
    real_check: str = "policy",
) -> TargetCheckResult:
    checker_method = "region" if real_check == "region" else method
    check = check_candidate(
        actual_spec,
        candidate_bundle.certificate,
        candidate_bundle.policy,
        checker_method,
        limits,
        f"target-{actual_spec.n}",
        deadline,
    )
    return TargetCheckResult(
        target_verified=bool(check["proof_verified"]),
        schema_validated_on_probes=False,
        check=check,
    )


def _next_small(target: int, seeds: tuple[int, ...], stable: int) -> int:
    first = max(stable, max(seeds, default=stable - 1) + 1)
    unused = [n for n in range(first, target) if n not in seeds]
    return unused[0] if unused else target


def _write_result(row: dict[str, object]) -> None:
    columns = ["family", "target", "seeds", "arity", "role_classes",
               "predicate_arities", "stage_reached", "stages_passed",
               "cegis_rounds", "verdict",
               "seed_s", "canonicalize_s", "anti_unify_s", "bus_schemas_s",
               "ranks_s", "instantiate_s", "cegis_s", "wall_s",
               "seed_monitor_s", "seed_solve_s", "target_monitor_s",
               "target_solve_s", "target_check_s", "probe_check_s",
               "driver_overhead_s",
               "peak_rss_kib", "solver_nodes", "solver_cache",
               "checker_node_caps", "reason"]
    rows = []
    if RESULTS.exists():
        rows = _read_tsv(RESULTS)
    key = (str(row["family"]), str(row["target"]))
    rows = [old for old in rows if (old["family"], old["target"]) != key]
    rows.append({column: row.get(column, "") for column in columns})
    rows.sort(key=lambda item: (item["family"], int(item["target"])))
    with RESULTS.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def _format_predicate_arities(family: str,
                              arities: tuple[tuple[str, int], ...]) -> str:
    base = REAL_FAMILIES[family].arity
    exceptions = [(name, arity) for name, arity in arities if arity != base]
    summary = [f"default={base}"]
    summary.extend(f"{name}={arity}" for name, arity in exceptions)
    return ";".join(summary)


def _validate_lift_parameters(
    family: str, target: int, seeds: tuple[int, ...]
) -> None:
    """Perform table-only fail-closed checks before any child or solver work."""
    stable = stable_from(family)
    bad = [n for n in seeds if n < stable]
    if bad:
        raise Decline("seed", "stable_from", bad[0],
                      f"seed is below measured stable_from={stable}")
    if family == "round_robin_arbiter_unreal2":
        raise Decline("scope", "arity measurement", None,
                      "family is not one of the five fixed-arity M4 families")
    if family in OUT_OF_SCOPE:
        raise Decline("scope", "arity measurement", None,
                      "measured min_k = n; invariant strengthening search is required")
    if family not in REAL_FAMILIES:
        raise Decline("scope", "arity measurement", None,
                      "family has no fixed-arity M4 measurement")
    measured = measured_arity(family)
    if measured != REAL_FAMILIES[family].arity:
        raise Decline("scope", "arity measurement", None,
                      f"expected k={REAL_FAMILIES[family].arity}, measured {measured}")
    if not seeds:
        raise Decline("seed", "seed set", None,
                      "at least one stable seed is required")
    if target <= max(seeds):
        raise Decline("seed", "target", target,
                      "target must be strictly larger than every seed")


def _build_lifted_candidate(
    family: str,
    target: int,
    seeds: tuple[int, ...],
    out: pathlib.Path,
    limits: ProposerLimits,
    deadline: AbsoluteDeadline,
    *,
    family_source: pathlib.Path | None,
    target_source: pathlib.Path | None,
    reduction_semantics: str,
    real_check: str = "policy",
) -> CandidateBundle:
    _validate_lift_parameters(family, target, seeds)
    acquired = acquire_small_instances(
        family, seeds, out, limits, deadline,
        family_source=family_source,
    )
    schema = learn_schema(acquired, limits, deadline)
    candidate, target_instance, cert, policy, detail = instantiate(
        schema, target, out, limits, deadline,
        family_source=family_source, target_source=target_source,
        reduction_semantics=reduction_semantics,
        real_check=real_check,
    )
    return CandidateBundle(candidate, target_instance, cert, policy, detail)


def _artifact_record(path: pathlib.Path) -> dict[str, object]:
    resolved = path.resolve()
    return {"path": str(resolved), "sha256": sha256(resolved)}


def _candidate_request_identity(
    family: str,
    target: int,
    seeds: tuple[int, ...],
    out: pathlib.Path,
    *,
    family_source: pathlib.Path | None,
    target_source: pathlib.Path | None,
    reduction_semantics: str,
    real_check: str = "policy",
) -> CandidateRequestIdentity:
    source = (
        target_source or family_source or ROOT / REAL_FAMILIES[family].source
    ).resolve()
    return CandidateRequestIdentity(
        family=family,
        target=target,
        seeds=tuple(sorted(set(seeds))),
        reduction_semantics=reduction_semantics,
        target_source_sha256=sha256(source),
        expected_arity=int(REAL_FAMILIES[family].arity),
        game_path=(out / f"{family}_{target}.game.aag").resolve(),
        provenance_path=(out / f"{family}_{target}.prov.json").resolve(),
        real_check=real_check,
    )


def _write_candidate_bundle(
    path: pathlib.Path,
    bundle: CandidateBundle,
    out: pathlib.Path,
    request_identity: CandidateRequestIdentity,
) -> None:
    artifacts = {
        "game": _artifact_record(bundle.target.game_path),
        "provenance": _artifact_record(bundle.target.prov_path),
        "certificate": _artifact_record(bundle.certificate),
        "certificate_metadata": _artifact_record(
            pathlib.Path(str(bundle.certificate) + ".json")),
        "evidence": _artifact_record(out / "evidence.json"),
    }
    if bundle.policy is not None:
        artifacts.update({
            "policy": _artifact_record(bundle.policy),
            "policy_metadata": _artifact_record(
                pathlib.Path(str(bundle.policy) + ".json")),
        })
    provenance_payload = json.loads(
        bundle.target.prov_path.read_text(encoding="utf-8"))
    payload = {
        "schema": CANDIDATE_BUNDLE_SCHEMA,
        "builder_pid": os.getpid(),
        "family": bundle.schema.family,
        "target": bundle.target.n,
        "request_identity": request_identity.payload(),
        "target_artifact_identity": {
            "game_sha256": artifacts["game"]["sha256"],
            "provenance_sha256": artifacts["provenance"]["sha256"],
            "provenance_schema": provenance_payload.get("schema"),
            "provenance_semantics": provenance_payload.get("semantics"),
        },
        "schema_bundle": {
            "arity": bundle.schema.arity,
            "seeds": list(bundle.schema.seeds),
            "role_classes": list(bundle.schema.role_classes),
            "bus_schemas": list(bundle.schema.bus_schemas),
            "template_counts": [list(item)
                                for item in bundle.schema.template_counts],
            "predicate_arities": [list(item)
                                  for item in bundle.schema.predicate_arities],
        },
        "artifacts": artifacts,
        "cost_times": dict(_COST_TIMES),
        "last_stage_times": dict(_LAST_STAGE_TIMES),
        "manager_owner": "candidate_builder_child",
        "checker_started": False,
    }
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def _read_candidate_bundle(
    path: pathlib.Path,
    out: pathlib.Path,
    expected: CandidateRequestIdentity,
) -> tuple[CandidateBundle, dict[str, object]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if (not isinstance(payload, dict) or
            payload.get("schema") != CANDIDATE_BUNDLE_SCHEMA):
        raise Decline("candidate_builder", "artifact bundle", None,
                      "invalid candidate bundle schema")
    identity = payload.get("request_identity")
    if identity != expected.payload():
        raise Decline(
            "candidate_builder", "request identity", expected.target,
            "candidate bundle does not match the parent request",
        )
    if (payload.get("family") != expected.family or
            payload.get("target") != expected.target):
        raise Decline(
            "candidate_builder", "request identity", expected.target,
            "candidate bundle family or target does not match the parent request",
        )
    schema_payload = payload.get("schema_bundle")
    if not isinstance(schema_payload, dict):
        raise Decline("candidate_builder", "schema bundle", None,
                      "schema representation is missing")
    try:
        schema_seeds = tuple(int(item) for item in schema_payload["seeds"])
        schema_arity = int(schema_payload["arity"])
    except (KeyError, TypeError, ValueError) as error:
        raise Decline(
            "candidate_builder", "schema bundle", expected.target,
            "schema request identity is malformed",
        ) from error
    if (schema_seeds != expected.seeds or
            schema_arity != expected.expected_arity):
        raise Decline(
            "candidate_builder", "schema bundle", expected.target,
            "schema seeds or arity do not match the parent request",
        )
    records = payload.get("artifacts")
    if not isinstance(records, dict):
        raise Decline("candidate_builder", "artifact bundle", None,
                      "candidate bundle has no artifact map")
    required_artifacts = {
        "game", "provenance", "certificate", "certificate_metadata",
        "evidence",
    }
    if expected.real_check == "policy":
        required_artifacts.update(("policy", "policy_metadata"))
    if not required_artifacts <= records.keys():
        raise Decline(
            "candidate_builder", "artifact bundle", expected.target,
            "candidate bundle is missing required artifacts",
        )
    resolved_out = out.resolve()
    checked: dict[str, pathlib.Path] = {}
    for name, record in records.items():
        if not isinstance(record, dict):
            raise Decline("candidate_builder", name, None,
                          "invalid artifact record")
        artifact = pathlib.Path(str(record.get("path", ""))).resolve()
        try:
            artifact.relative_to(resolved_out)
        except ValueError as error:
            raise Decline("candidate_builder", name, None,
                          "artifact escaped the invocation workspace") from error
        if not artifact.is_file() or sha256(artifact) != record.get("sha256"):
            raise Decline("candidate_builder", name, None,
                          "artifact is missing or hash-mismatched")
        checked[name] = artifact
    if (checked["game"] != expected.game_path or
            checked["provenance"] != expected.provenance_path):
        raise Decline(
            "candidate_builder", "target artifact identity", expected.target,
            "game or provenance path does not match the parent request",
        )
    target_artifact_identity = payload.get("target_artifact_identity")
    provenance_payload = json.loads(
        checked["provenance"].read_text(encoding="utf-8"))
    expected_artifact_identity = {
        "game_sha256": records["game"].get("sha256"),
        "provenance_sha256": records["provenance"].get("sha256"),
        "provenance_schema": PROVENANCE_SCHEMA,
        "provenance_semantics": expected.reduction_semantics,
    }
    if (target_artifact_identity != expected_artifact_identity or
            provenance_payload.get("schema") != PROVENANCE_SCHEMA or
            provenance_payload.get("semantics") != expected.reduction_semantics):
        raise Decline(
            "candidate_builder", "target artifact identity", expected.target,
            "game or provenance identity does not match the parent request",
        )
    target_instance = Instance.load(
        expected.family, expected.target, checked["game"], checked["provenance"])
    schema = CandidateSchema(
        family=expected.family,
        arity=schema_arity,
        seeds=schema_seeds,
        role_classes=tuple(str(item) for item in schema_payload["role_classes"]),
        bus_schemas=tuple(str(item) for item in schema_payload["bus_schemas"]),
        template_counts=tuple(
            (str(name), int(count))
            for name, count in schema_payload["template_counts"]
        ),
        predicate_arities=tuple(
            (str(name), int(arity))
            for name, arity in schema_payload["predicate_arities"]
        ),
    )
    detail = {
        "times": dict(payload.get("last_stage_times", {})),
        "evidence": json.loads(checked["evidence"].read_text(encoding="utf-8")),
        "manager_lifetime": {
            "builder_pid": int(payload["builder_pid"]),
            "manager_owner": payload.get("manager_owner"),
        },
    }
    policy = checked.get("policy")
    if expected.real_check == "region" and (
        "policy" in checked or "policy_metadata" in checked
    ):
        raise Decline(
            "candidate_builder", "artifact bundle", expected.target,
            "region candidate bundle unexpectedly contains a policy",
        )
    return CandidateBundle(
        schema, target_instance, checked["certificate"], policy, detail,
    ), payload


def _candidate_builder_command(
    family: str,
    target: int,
    seeds: tuple[int, ...],
    out: pathlib.Path,
    limits: ProposerLimits,
    deadline: AbsoluteDeadline,
    check_method: str,
    family_source: pathlib.Path | None,
    target_source: pathlib.Path | None,
    reduction_semantics: str,
    real_check: str = "policy",
) -> list[str]:
    command = [
        sys.executable, str(pathlib.Path(__file__).resolve()),
        "--candidate-builder", "--family", family, "--target", str(target),
        "--seeds", ",".join(map(str, seeds)),
        "--timeout", str(limits.checker_timeout_s),
        "--absolute-deadline-monotonic",
        repr(deadline.expires_monotonic_s),
        "--check-method", check_method,
        "--real-check", real_check,
        "--reduction-semantics", reduction_semantics,
        "--out", str(out),
        "--candidate-bundle-out", str(out / "candidate-bundle.json"),
        "--tlsf-tools-build", str(TOOL_CONFIG.tlsf_tools_build),
        "--bindings-python", str(BINDINGS_PYTHON),
        "--bindings-site", str(BINDINGS_SITE),
        "--monitor", str(MONITOR), "--solver", str(SOLVER),
        "--checker", str(CHECKER),
    ]
    if TOOL_CONFIG.buddy_adapter is not None:
        command.extend(("--buddy-adapter", str(TOOL_CONFIG.buddy_adapter)))
    if limits.solver_nodes is not None:
        command.extend(("--solver-nodes", str(limits.solver_nodes)))
    if limits.solver_cache is not None:
        command.extend(("--solver-cache", str(limits.solver_cache)))
    if limits.checker_nodes is not None:
        command.extend(("--node-cap", str(limits.checker_nodes)))
    if family_source is not None:
        command.extend(("--family-source", str(family_source)))
    if target_source is not None:
        command.extend(("--target-source", str(target_source)))
    return command


def _launch_candidate_builder(
    command: list[str],
    bundle_path: pathlib.Path,
    out: pathlib.Path,
    deadline: AbsoluteDeadline,
    expected_identity: CandidateRequestIdentity,
) -> tuple[CandidateBundle, dict[str, object]]:
    started = time.monotonic()
    proc = subprocess.Popen(
        command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, start_new_session=True,
    )
    try:
        stdout, stderr = proc.communicate(
            timeout=deadline.timeout_s(
                deadline.remaining_s(), "candidate_builder", reserve_s=0.05))
    except subprocess.TimeoutExpired as error:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.communicate()
        raise Decline("candidate_builder", "absolute deadline", None,
                      "candidate builder timed out and was reaped") from error
    except BaseException:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.communicate()
        raise
    if proc.returncode != 0:
        detail = (stderr or stdout).strip()[-500:]
        raise Decline("candidate_builder", "child process", None,
                      f"builder exited {proc.returncode}: {detail}")
    bundle, payload = _read_candidate_bundle(
        bundle_path, out, expected_identity)
    payload["builder_process"] = {
        "pid": proc.pid,
        "elapsed_s": time.monotonic() - started,
        "exited_monotonic_s": time.monotonic(),
        "returncode": proc.returncode,
    }
    return bundle, payload


def run(family: str, target: int, seeds: tuple[int, ...], out: pathlib.Path,
        limits: ProposerLimits, check_method: str = "auto",
        real_check: str = "policy", *,
        family_source: pathlib.Path | None = None,
        target_source: pathlib.Path | None = None,
        reduction_semantics: str = "exact",
        deadline: AbsoluteDeadline | None = None,
        prepared_bundle: CandidateBundle | None = None,
        builder_evidence: dict[str, object] | None = None,
        orchestration_started: float | None = None) -> dict:
    global _ACTIVE_STAGE
    _LAST_STAGE_TIMES.clear()
    _COST_TIMES.clear()
    _ACTIVE_STAGE = None
    _write_cost_progress()
    started_all = (time.monotonic() if orchestration_started is None
                   else orchestration_started)
    absolute = deadline or AbsoluteDeadline.after(limits.checker_timeout_s)
    _validate_lift_parameters(family, target, seeds)

    current = tuple(sorted(set(seeds)))
    rounds = 0
    cegis_started = time.monotonic()
    # The integration decision deliberately omits an exploratory probe.  Seed
    # acquisition and feasibility happen once, before the expensive target
    # monitor; the exact target certificate alone decides this invocation.
    bundle = prepared_bundle or _build_lifted_candidate(
        family, target, current, out, limits, absolute,
        family_source=family_source, target_source=target_source,
        reduction_semantics=reduction_semantics,
        real_check=real_check,
    )
    if builder_evidence is not None:
        _COST_TIMES.update(builder_evidence.get("cost_times", {}))
    candidate = bundle.schema
    target_instance = bundle.target
    cert = bundle.certificate
    policy = bundle.policy
    detail = bundle.detail
    target_result = check_target(
        target_instance, bundle, absolute, limits, check_method, real_check)
    final_check = target_result.check
    checks = [(target, final_check)]
    verdict = (
        "VERIFIED" if target_result.target_verified
        else ("ERROR" if final_check["returncode"] == 0
              else VERDICTS.get(final_check["returncode"], "ERROR"))
    )
    times = detail["times"]
    times["cegis"] = time.monotonic() - cegis_started
    evidence = detail["evidence"]
    evidence["stage_evidence"]["CEGIS"].update({
        "rounds": rounds,
        "checks": [
            {"n": n, "exit_code": check["returncode"],
             "verdict": check["verdict"],
             "method": check["proof_method"],
             "result": check["result_string"],
             "node_caps": check["node_caps"],
             "counterexample": bool(
                 isinstance(check.get("json"), dict) and
                 check["json"].get("counterexample"))}
            for n, check in checks
        ],
        "final_verdict": verdict,
        "target_verified": target_result.target_verified,
        "schema_validated_on_probes": (
            target_result.schema_validated_on_probes),
        "claim_scope": "requested_target_only",
    })
    evidence["target_verified"] = target_result.target_verified
    evidence["schema_validated_on_probes"] = (
        target_result.schema_validated_on_probes)
    evidence["claim_scope"] = "requested_target_only"
    evidence["real_check"] = real_check
    evidence["target_check_method"] = final_check["proof_method"]
    evidence["target_check_result"] = final_check["result_string"]
    evidence["stage_evidence"]["target_check"] = {
        "method": final_check["proof_method"],
        "result": final_check["result_string"],
        "game": target_instance.game_path.name,
        "certificate": cert.name,
        "policy": policy.name if policy is not None else None,
    }
    if builder_evidence is not None:
        checker_started = final_check.get("started_monotonic_s")
        builder_process = builder_evidence.get("builder_process", {})
        builder_exit = (builder_process.get("exited_monotonic_s")
                        if isinstance(builder_process, dict) else None)
        evidence["manager_lifetime"] = {
            "supervisor_holds_bdd_manager": False,
            "candidate_builder_pid": builder_evidence.get("builder_pid"),
            "candidate_builder_exit_monotonic_s": builder_exit,
            "checker_started_monotonic_s": checker_started,
            "builder_exited_before_checker": bool(
                isinstance(builder_exit, (int, float)) and
                isinstance(checker_started, (int, float)) and
                builder_exit <= checker_started
            ),
            "handoff": "validated_paths_hashes_and_schema_bundle",
        }
    (out / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    wall_s = time.monotonic() - started_all
    charged_s = sum(
        _COST_TIMES.get(key, 0.0)
        for key in ("seed_monitor_game", "seed_solve", "target_monitor_game",
                    "stage_anti_unify", "stage_bus_schemas", "stage_ranks",
                    "stage_instantiate", "target_check", "probe_check")
    )
    driver_overhead_s = max(0.0, wall_s - charged_s)
    cost_accounting = {
        "seed_monitor_s": _COST_TIMES.get("seed_monitor_game", 0.0),
        "seed_solve_s": _COST_TIMES.get("seed_solve", 0.0),
        "target_monitor_s": _COST_TIMES.get("target_monitor_game", 0.0),
        "generalization_s": sum(
            _COST_TIMES.get(key, 0.0)
            for key in ("stage_anti_unify", "stage_bus_schemas", "stage_ranks")
        ),
        "instantiate_s": _COST_TIMES.get("stage_instantiate", 0.0),
        "target_check_s": _COST_TIMES.get("target_check", 0.0),
        "probe_check_s": _COST_TIMES.get("probe_check", 0.0),
        "driver_overhead_s": driver_overhead_s,
        "wall_s": wall_s,
    }
    evidence["cost_accounting"] = cost_accounting
    (out / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    solver_capacities = [limits.seed_capacity(n) for n in current]
    checker_caps = [
        f"{n}:" + "/".join(map(str, check["node_caps"]))
        for n, check in checks if check["node_caps"]]
    peak_rss = max(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                   resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    result = {"family": family, "target": target, "seeds": current,
              "arity": candidate.arity, "role_classes": candidate.role_classes,
              "predicate_arities": candidate.predicate_arities,
              "stages_passed": detail["evidence"]["stages"],
              "cegis_rounds": rounds, "verdict": verdict,
              "certificate": cert, "policy": policy, "checks": checks,
              "real_check": real_check,
              "target_check_method": final_check["proof_method"],
              "target_check_result": final_check["result_string"],
              "times": times, "wall_s": wall_s, "peak_rss_kib": peak_rss,
              "cost_accounting": cost_accounting,
              "reason": "" if verdict == "VERIFIED" else
              ((final_check["stderr"] or final_check["stdout"]).strip()[-500:])}
    _write_result({"family": family, "target": target,
                   "seeds": ",".join(map(str, current)), "arity": candidate.arity,
                   "role_classes": ",".join(candidate.role_classes),
                   "predicate_arities": _format_predicate_arities(
                       family, candidate.predicate_arities),
                   "stage_reached": "CEGIS",
                   "stages_passed": ",".join(result["stages_passed"]),
                   "cegis_rounds": rounds, "verdict": verdict,
                   "seed_s": f"{times.get('seed', 0):.6f}",
                   "canonicalize_s": f"{times.get('canonicalize', 0):.6f}",
                   "anti_unify_s": f"{times.get('anti_unify', 0):.6f}",
                   "bus_schemas_s": f"{times.get('bus_schemas', 0):.6f}",
                   "ranks_s": f"{times.get('ranks', 0):.6f}",
                   "instantiate_s": f"{times.get('instantiate', 0):.6f}",
                   "cegis_s": f"{times.get('cegis', 0):.6f}",
                   "wall_s": f"{wall_s:.6f}",
                   "seed_monitor_s": f"{cost_accounting['seed_monitor_s']:.6f}",
                   "seed_solve_s": f"{cost_accounting['seed_solve_s']:.6f}",
                   "target_monitor_s": f"{cost_accounting['target_monitor_s']:.6f}",
                   "target_solve_s": "0.000000",
                   "target_check_s": f"{cost_accounting['target_check_s']:.6f}",
                   "probe_check_s": f"{cost_accounting['probe_check_s']:.6f}",
                   "driver_overhead_s": f"{cost_accounting['driver_overhead_s']:.6f}",
                   "peak_rss_kib": peak_rss,
                   "solver_nodes": ",".join(str(nodes) for nodes, _ in solver_capacities),
                   "solver_cache": ",".join(str(cache) for _, cache in solver_capacities),
                   "checker_node_caps": ",".join(checker_caps),
                   "reason": result["reason"] or "-"})
    return result


def run_exact_direct(family: str, target: int, out: pathlib.Path,
                     limits: ProposerLimits, *,
                     target_source: pathlib.Path | None = None,
                     deadline: AbsoluteDeadline | None = None) -> dict:
    """Build, solve, and check either side of one exact target game.

    M5 made the dual certificate checkable but did not measure a bounded-arity
    cross-size environment generalizer.  Keeping this path in the same driver
    gives every campaign row a standalone ``generalize_gr1.py`` reproducer
    without mislabelling a direct target solve as lifting.  The capability
    selects this exact route; the solver and independent checker discover the
    answer rather than the family registry predicting it.
    """
    _LAST_STAGE_TIMES.clear()
    _COST_TIMES.clear()
    global _ACTIVE_STAGE
    _ACTIVE_STAGE = None
    _write_cost_progress()
    out.mkdir(parents=True, exist_ok=True)
    started_all = time.monotonic()
    absolute = deadline or AbsoluteDeadline.after(limits.checker_timeout_s)
    game, provenance = build_game(
        family, target, out, limits.checker_timeout_s, stage="canonicalize",
        source=target_source, reduction_semantics="exact", deadline=absolute)
    diagnostic_instance = None
    if _DIAGNOSTICS_ENABLED:
        try:
            diagnostic_instance = Instance.load(
                family, target, game, provenance)
            _diagnostic_register_instance(diagnostic_instance, "target")
        except Exception as error:
            if _DIAGNOSTICS is not None:
                _DIAGNOSTICS.record_error(
                    "load_exact_diagnostic_instance", error)
    certificate = out / f"{family}_{target}.certificate.aag"
    policy = out / f"{family}_{target}.policy.aag"
    nodes, cache = limits.seed_capacity(target)
    solve_command = [
        str(SOLVER), "--semantics", "exact",
        "--certificate", str(certificate),
        "--certificate-json", str(certificate) + ".json",
        "--policy", str(policy), "--policy-json", str(policy) + ".json",
        "--oxidd-nodes", str(nodes), "--oxidd-cache", str(cache), str(game),
    ]
    solve_started = time.monotonic()
    _set_active_progress_stage("target_solve", solve_started)
    solve_token = (
        _diagnostic_begin("target_solve") if _DIAGNOSTICS_ENABLED else None
    )
    try:
        solve = _run(
            solve_command,
            absolute.timeout_s(limits.checker_timeout_s, "target_solve"),
        )
    finally:
        _COST_TIMES["target_solve"] += time.monotonic() - solve_started
        if solve_token is not None:
            _diagnostic_end(solve_token)
        _clear_active_progress_stage("target_solve")
    if solve.returncode not in (0, 1):
        if (solve.returncode == 124 and _DIAGNOSTICS_ENABLED and
                _DIAGNOSTICS is not None):
            _DIAGNOSTICS.censor(
                "target_solve", time.monotonic() - solve_started,
                "subprocess_timeout")
        detail = (solve.stderr or solve.stdout).strip()[-500:]
        raise Decline("target_solve", "tlsfsolve", target,
                      f"exact target had no decisive export (exit {solve.returncode}): {detail}")
    answer = "REALIZABLE" if solve.returncode == 0 else "UNREALIZABLE"
    side = "system" if solve.returncode == 0 else "environment"
    status = "realizable" if answer == "REALIZABLE" else "unrealizable"
    required = (certificate, policy, pathlib.Path(str(certificate) + ".json"),
                pathlib.Path(str(policy) + ".json"))
    if not all(path.is_file() for path in required):
        raise Decline("target_solve", "environment artifacts", target,
                      "decisive solver verdict omitted certificate or policy")
    cert_meta = json.loads(pathlib.Path(str(certificate) + ".json").read_text(
        encoding="utf-8"))
    policy_meta = json.loads(pathlib.Path(str(policy) + ".json").read_text(
        encoding="utf-8"))
    common_metadata_ok = (
        cert_meta.get("status") == status
        and cert_meta.get("side") in (None, side)
        and policy_meta.get("side") in (None, side)
    )
    environment_metadata_ok = (
        side != "environment"
        or (
            cert_meta.get("side") == "environment"
            and cert_meta.get("reduction_semantics") == "exact"
            and cert_meta.get("environment_counter_strategy_exported") is True
            and policy_meta.get("side") == "environment"
            and policy_meta.get("reduction_semantics") == "exact"
        )
    )
    if not common_metadata_ok or not environment_metadata_ok:
        raise Decline("target_solve", f"{side} metadata", target,
                      f"certificate is not a valid exact {side} witness")

    json_out = out / f"check-target-{target}.json"
    attempts = []
    checker_started = time.monotonic()
    _set_active_progress_stage("target_check", checker_started)
    check = None
    check_token = (
        _diagnostic_begin("target_check") if _DIAGNOSTICS_ENABLED else None
    )
    # Environment certificates carry the dual outer/inner ranks and need a
    # larger checker arena than the system side on the measured M5 families.
    # The enclosing campaign cgroup remains the authoritative 8 GiB bound.
    initial_checker_cap = limits.checker_nodes or _scaled_checker_nodes(target)
    if side == "environment":
        initial_checker_cap = max(initial_checker_cap, 1 << 26)
    node_cap = initial_checker_cap
    for attempt_index in range(2):
        attempt_started = time.monotonic()
        json_out.unlink(missing_ok=True)
        checker_timeout = absolute.timeout_s(
            limits.checker_timeout_s, "target_check", reserve_s=0.05)
        command = [
            str(CHECKER), "--method", "certificate", "--timeout",
            str(checker_timeout), "--node-cap", str(node_cap),
            "--json-out", str(json_out), "--certificate", str(certificate),
            "--certificate-json", str(certificate) + ".json",
        ]
        if _DIAGNOSTICS_ENABLED:
            stats_path = out / f".s0-checker-stats-target-{target}-{node_cap}.json"
            command.extend(_checker_stats_options(stats_path))
            mode_count = 0
            if diagnostic_instance is not None:
                mode_count = _diagnostic_record_checker_attempt(
                    diagnostic_instance, side, f"target-{target}", node_cap)
        command.extend([str(game), str(policy)])
        check = _run(command, absolute.timeout_s(
            checker_timeout + 10, "target_check"))
        if _DIAGNOSTICS_ENABLED:
            _record_checker_stats(
                stats_path, check, f"target-{target}", node_cap, side,
                mode_count)
        attempt = {"node_cap": node_cap, "returncode": check.returncode,
                   "retry": False, "retry_reason": None}
        attempts.append(attempt)
        if attempt_index or not _recoverable_checker_capacity_failure(
                check, method="certificate", requested_node_cap=node_cap):
            break
        allowed, reason = _checker_retry_allowed(
            absolute, time.monotonic() - attempt_started)
        attempt["retry"] = allowed
        attempt["retry_reason"] = reason
        if not allowed:
            break
        node_cap *= 2
    if (check is not None and check.returncode == 124 and
            _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None):
        _DIAGNOSTICS.censor_active("subprocess_timeout")
    _COST_TIMES["target_check"] += time.monotonic() - checker_started
    if check_token is not None:
        _diagnostic_end(check_token)
    _clear_active_progress_stage("target_check")
    assert check is not None
    verdict = VERDICTS.get(check.returncode, "ERROR")
    payload = None
    if json_out.is_file():
        try:
            payload = json.loads(json_out.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            payload = None
    if (check.returncode != 0 or not isinstance(payload, dict) or
            payload.get("verdict") != "VERIFIED" or
            payload.get("methods", {}).get("certificate", {}).get("verdict") != "VERIFIED"):
        detail = (check.stderr or check.stdout).strip()[-500:]
        raise Decline("target_check", "tlsfcertcheck", target,
                      f"{side} certificate did not verify: {verdict}: {detail}")

    wall_s = time.monotonic() - started_all
    target_monitor = _COST_TIMES.get("target_monitor_game", 0.0)
    target_solve = _COST_TIMES.get("target_solve", 0.0)
    target_check = _COST_TIMES.get("target_check", 0.0)
    driver_overhead = max(0.0, wall_s - target_monitor - target_solve - target_check)
    evidence = {
        "format": "acacia-param-lift-gr1-evidence-v1",
        "compose_route": _compose_route(),
        "real_check": "policy",
        "target_check_method": "certificate",
        "target_check_result": "VERIFIED",
        "family": family, "target": target, "seeds": [],
        "path_kind": "direct-certified", "measured_arity": None,
        "semantics": "exact", "certificate_side": side,
        "stage_evidence": {
            "target_monitor_game": {"game": game.name,
                                    "provenance": provenance.name},
            "target_solve": {"exit_code": solve.returncode,
                             "certificate": certificate.name,
                             "policy": policy.name},
            "target_check": {"verdict": "VERIFIED", "attempts": attempts},
        },
        "cost_accounting": {
            "seed_monitor_s": 0.0, "seed_solve_s": 0.0,
            "generalization_s": 0.0, "instantiate_s": 0.0,
            "target_monitor_s": target_monitor,
            "target_solve_s": target_solve, "target_check_s": target_check,
            "probe_check_s": 0.0, "driver_overhead_s": driver_overhead,
            "wall_s": wall_s,
        },
        "final_verdict": "VERIFIED",
        "answer": answer,
    }
    (out / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    peak_rss = max(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                   resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    _write_result({
        "family": family, "target": target, "seeds": "", "arity": "",
        "role_classes": "", "predicate_arities": "",
        "stage_reached": "target_check",
        "stages_passed": "target_monitor_game,target_solve,target_check",
        "cegis_rounds": 0, "verdict": "VERIFIED", "seed_s": "0.000000",
        "canonicalize_s": "0.000000", "anti_unify_s": "0.000000",
        "bus_schemas_s": "0.000000", "ranks_s": "0.000000",
        "instantiate_s": "0.000000", "cegis_s": "0.000000",
        "wall_s": f"{wall_s:.6f}", "seed_monitor_s": "0.000000",
        "seed_solve_s": "0.000000", "target_monitor_s": f"{target_monitor:.6f}",
        "target_solve_s": f"{target_solve:.6f}",
        "target_check_s": f"{target_check:.6f}", "probe_check_s": "0.000000",
        "driver_overhead_s": f"{driver_overhead:.6f}",
        "peak_rss_kib": peak_rss, "solver_nodes": nodes,
        "solver_cache": cache,
        "checker_node_caps": "/".join(str(item["node_cap"]) for item in attempts),
        "reason": "-",
    })
    return {
        "family": family, "target": target, "seeds": (), "arity": "",
        "role_classes": (), "stages_passed": evidence["stage_evidence"].keys(),
        "cegis_rounds": 0, "verdict": "VERIFIED",
        "answer": answer,
        "certificate": certificate, "policy": policy,
        "wall_s": wall_s, "peak_rss_kib": peak_rss, "reason": "",
    }


def run_unreal_direct(family: str, target: int, out: pathlib.Path,
                      limits: ProposerLimits) -> dict:
    """Compatibility alias for historical callers of the former M5 route."""
    return run_exact_direct(family, target, out, limits)


def _decline_result(family: str, target: int, seeds: tuple[int, ...],
                    decline: Decline, limits: ProposerLimits,
                    wall_s: float) -> dict:
    times = dict(_LAST_STAGE_TIMES)
    if _ACTIVE_STAGE is not None:
        active_name, active_started = _ACTIVE_STAGE
        times[active_name] = time.monotonic() - active_started
    solver_capacities = [limits.seed_capacity(n) for n in seeds]
    peak_rss = max(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                   resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    charged_s = sum(
        _COST_TIMES.get(key, 0.0)
        for key in ("seed_monitor_game", "seed_solve", "target_monitor_game",
                    "stage_anti_unify", "stage_bus_schemas", "stage_ranks",
                    "stage_instantiate", "target_check", "probe_check")
    )
    driver_overhead_s = max(0.0, wall_s - charged_s)
    result = {"family": family, "target": target, "seeds": seeds,
              "arity": REAL_FAMILIES[family].arity if family in REAL_FAMILIES else "",
              "role_classes": (), "stages_passed": (), "cegis_rounds": 0,
              "verdict": "UNKNOWN", "reason": str(decline),
              "times": times, "wall_s": wall_s, "peak_rss_kib": peak_rss}
    _write_result({"family": family, "target": target,
                   "seeds": ",".join(map(str, seeds)),
                   "arity": result["arity"], "role_classes": "",
                   "predicate_arities": "", "stage_reached": decline.stage,
                   "stages_passed": ",".join(times), "cegis_rounds": 0,
                   "verdict": "UNKNOWN",
                   "seed_s": f"{times.get('seed', 0):.6f}",
                   "canonicalize_s": f"{times.get('canonicalize', 0):.6f}",
                   "anti_unify_s": f"{times.get('anti_unify', 0):.6f}",
                   "bus_schemas_s": f"{times.get('bus_schemas', 0):.6f}",
                   "ranks_s": f"{times.get('ranks', 0):.6f}",
                   "instantiate_s": f"{times.get('instantiate', 0):.6f}",
                   "cegis_s": f"{times.get('cegis', 0):.6f}",
                   "wall_s": f"{wall_s:.6f}", "peak_rss_kib": peak_rss,
                   "seed_monitor_s": f"{_COST_TIMES.get('seed_monitor_game', 0):.6f}",
                   "seed_solve_s": f"{_COST_TIMES.get('seed_solve', 0):.6f}",
                   "target_monitor_s": f"{_COST_TIMES.get('target_monitor_game', 0):.6f}",
                   "target_solve_s": f"{_COST_TIMES.get('target_solve', 0):.6f}",
                   "target_check_s": f"{_COST_TIMES.get('target_check', 0):.6f}",
                   "probe_check_s": f"{_COST_TIMES.get('probe_check', 0):.6f}",
                   "driver_overhead_s": f"{driver_overhead_s:.6f}",
                   "solver_nodes": ",".join(str(nodes) for nodes, _ in solver_capacities),
                   "solver_cache": ",".join(str(cache) for _, cache in solver_capacities),
                   "checker_node_caps": "", "reason": str(decline)})
    return result


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family")
    parser.add_argument("--target", type=int)
    parser.add_argument("--seeds", help="comma-separated stable seed sizes")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--absolute-deadline-monotonic", type=float,
        help="host monotonic deadline shared by the campaign and all children",
    )
    parser.add_argument("--candidate-builder", action="store_true",
                        help=argparse.SUPPRESS)
    parser.add_argument("--candidate-bundle-out", type=pathlib.Path,
                        help=argparse.SUPPRESS)
    parser.add_argument(
        "--node-cap", type=int,
        help="checker node cap (default: 2^24, doubled every five clients to 2^26)")
    parser.add_argument(
        "--solver-nodes", type=int,
        help="seed-solver node capacity (default: 2^25, doubled every four clients to 2^27)")
    parser.add_argument(
        "--solver-cache", type=int,
        help="seed-solver cache capacity (default: one quarter of solver nodes)")
    parser.add_argument("--out", type=pathlib.Path)
    parser.add_argument(
        "--diagnostics", type=pathlib.Path, metavar="PATH",
        help="write one opt-in S0 diagnostic JSON document",
    )
    parser.add_argument("--check-method", choices=("auto", "certificate", "both"),
                        default="auto", help=argparse.SUPPRESS)
    parser.add_argument(
        "--real-check", choices=("policy", "region"), default="policy",
        help=("proof route for lifted REAL requests: policy exports a "
              "synthesis artifact; region is decision-only and omits it"),
    )
    parser.add_argument("--monitor", type=pathlib.Path, help=argparse.SUPPRESS)
    parser.add_argument("--solver", type=pathlib.Path, help=argparse.SUPPRESS)
    parser.add_argument("--checker", type=pathlib.Path, help=argparse.SUPPRESS)
    parser.add_argument(
        "--target-source", type=pathlib.Path,
        help="source-bound target TLSF; seed instances still use the pinned template",
    )
    parser.add_argument(
        "--family-source", type=pathlib.Path,
        help="frozen, content-verified family template for seed instances",
    )
    parser.add_argument(
        "--reduction-semantics", choices=("exact", "strict"), default="exact",
        help=argparse.SUPPRESS,
    )
    add_configuration_arguments(parser)
    return parser


def _apply_tool_configuration(
    config: ToolConfiguration, args: argparse.Namespace
) -> None:
    global TOOL_CONFIG, TT, MONITOR, SOLVER, CHECKER, BINDINGS_PYTHON, BINDINGS_SITE
    TOOL_CONFIG = config
    TT = config.tlsf_tools_root
    MONITOR = (args.monitor or config.monitor).resolve()
    SOLVER = (args.solver or config.solver).resolve()
    CHECKER = (args.checker or config.checker).resolve()
    BINDINGS_PYTHON = config.bindings_python
    BINDINGS_SITE = config.bindings_site


def main(argv: list[str] | None = None) -> int:
    arguments = argv if argv is not None else sys.argv[1:]
    parser = _parser()
    args = parser.parse_args(arguments)
    config = configuration_from_args(args)
    monitor = (args.monitor or config.monitor).resolve()
    solver = (args.solver or config.solver).resolve()
    checker = (args.checker or config.checker).resolve()
    started = time.monotonic()
    deadline = (
        AbsoluteDeadline(args.absolute_deadline_monotonic)
        if args.absolute_deadline_monotonic is not None
        else AbsoluteDeadline.after(args.timeout)
    )
    if args.probe:
        if args.diagnostics is not None and deadline.remaining_s() > 0:
            _apply_tool_configuration(config, args)
            _diagnostic_initialize(args.diagnostics, deadline)
        probe_result = print_probe(
            config, monitor=monitor, solver=solver, checker=checker,
            required_checker_method=(
                "region" if args.real_check == "region" else None),
        )
        if _DIAGNOSTICS_ENABLED:
            _diagnostic_finish(
                "completed" if probe_result == 0 else "unknown",
                {"verdict": "PROBE", "reason": f"exit_{probe_result}"},
            )
        return probe_result
    if args.family is None or args.target is None:
        parser.error("--family and --target are required unless --probe is used")
    if args.real_check == "region" and args.family in REAL_FAMILIES:
        try:
            require_checker_method(checker, "region")
        except ProbeError as error:
            print(f"configuration: {error}", file=sys.stderr)
            return 4
    # Spot and BuDDy are native modules tied to the configured CPython ABI.
    if pathlib.Path(sys.executable).resolve() != config.bindings_python:
        os.execv(
            str(config.bindings_python),
            [str(config.bindings_python), __file__, *arguments],
        )
    _apply_tool_configuration(config, args)
    # Diagnostics are optional and may never extend the proof budget.  In
    # particular, do not start their checker capability probe after an outer
    # campaign has already exhausted the shared absolute deadline.
    if args.diagnostics is not None and deadline.remaining_s() > 0:
        _diagnostic_initialize(args.diagnostics, deadline)
    default = REAL_FAMILIES.get(args.family) or EXACT_FAMILIES.get(args.family)
    seeds = (tuple(int(item) for item in args.seeds.split(",") if item)
             if args.seeds is not None else (default.default_seeds if default else ()))
    out = (args.out or ROOT / "build_scratch" / "param-lift-m4" /
           f"{args.family}-n{args.target}").resolve()
    limits = ProposerLimits(checker_timeout_s=args.timeout,
                            solver_nodes=args.solver_nodes,
                            solver_cache=args.solver_cache,
                            checker_nodes=args.node_cap)
    previous_handlers: dict[int, object] = {}
    was_cancelled = False

    def diagnostic_cancel(signum: int, _frame: object) -> None:
        nonlocal was_cancelled
        was_cancelled = True
        if _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None:
            reason = signal.Signals(signum).name.lower()
            _write_cost_progress()
            _DIAGNOSTICS.censor_active(reason)
            _diagnostic_finish("cancelled")
        raise DiagnosticCancelled(signal.Signals(signum).name)

    for handled in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[handled] = signal.getsignal(handled)
        signal.signal(handled, diagnostic_cancel)
    try:
        if args.candidate_builder:
            if args.family not in REAL_FAMILIES:
                raise Decline(
                    "candidate_builder", "route", args.target,
                    "candidate builder is only valid for lifted REAL routes",
                )
            request_identity = _candidate_request_identity(
                args.family, args.target, seeds, out,
                family_source=args.family_source,
                target_source=args.target_source,
                reduction_semantics=args.reduction_semantics,
                real_check=args.real_check,
            )
            bundle = _build_lifted_candidate(
                args.family, args.target, seeds, out, limits, deadline,
                family_source=args.family_source,
                target_source=args.target_source,
                reduction_semantics=args.reduction_semantics,
                real_check=args.real_check,
            )
            bundle_out = (
                args.candidate_bundle_out or out / "candidate-bundle.json"
            ).resolve()
            _write_candidate_bundle(
                bundle_out, bundle, out, request_identity)
            if _DIAGNOSTICS_ENABLED:
                _diagnostic_finish("completed", {
                    "family": args.family, "target": args.target,
                    "verdict": "CANDIDATE_BUILT",
                })
            print(f"candidate_bundle: {bundle_out}")
            return 0
        if args.family in EXACT_FAMILIES:
            if args.reduction_semantics != "exact":
                raise Decline(
                    "scope", "reduction semantics", args.target,
                    "exact-game capability requires exact semantics",
                )
            result = run_exact_direct(
                args.family, args.target, out, limits,
                target_source=args.target_source,
                deadline=deadline,
            )
        else:
            _validate_lift_parameters(args.family, args.target, seeds)
            command = _candidate_builder_command(
                args.family, args.target, seeds, out, limits, deadline,
                args.check_method, args.family_source, args.target_source,
                args.reduction_semantics, args.real_check,
            )
            expected_identity = _candidate_request_identity(
                args.family, args.target, seeds, out,
                family_source=args.family_source,
                target_source=args.target_source,
                reduction_semantics=args.reduction_semantics,
                real_check=args.real_check,
            )
            child_diagnostics = None
            if args.diagnostics is not None:
                child_diagnostics = out / "candidate-builder-diagnostics.json"
                command.extend(("--diagnostics", str(child_diagnostics)))
            builder_token = (
                _diagnostic_begin("candidate_builder")
                if _DIAGNOSTICS_ENABLED else None
            )
            try:
                bundle, builder_evidence = _launch_candidate_builder(
                    command, out / "candidate-bundle.json", out, deadline,
                    expected_identity)
            finally:
                if builder_token is not None:
                    _diagnostic_end(builder_token)
            if child_diagnostics is not None and child_diagnostics.is_file():
                try:
                    child_payload = json.loads(
                        child_diagnostics.read_text(encoding="utf-8"))
                    if isinstance(child_payload, dict):
                        _diagnostic_merge_candidate_builder(child_payload)
                finally:
                    child_diagnostics.unlink(missing_ok=True)
            result = run(
                args.family, args.target, seeds, out, limits,
                args.check_method, args.real_check,
                family_source=args.family_source,
                target_source=args.target_source,
                reduction_semantics=args.reduction_semantics,
                deadline=deadline, prepared_bundle=bundle,
                builder_evidence=builder_evidence,
                orchestration_started=started,
            )
    except Decline as exc:
        result = _decline_result(args.family, args.target, seeds, exc, limits,
                                 time.monotonic() - started)
        out.mkdir(parents=True, exist_ok=True)
        (out / "evidence.json").write_text(json.dumps({
            "format": "acacia-param-lift-gr1-evidence-v1",
            "compose_route": _compose_route(),
            "real_check": args.real_check,
            "family": args.family, "target": args.target,
            "seeds": list(seeds), "verdict": "UNKNOWN",
            "decline": {"stage": exc.stage, "predicate": exc.predicate,
                        "n": exc.n, "reason": exc.reason},
            "capacities": {
                "solver": [{"n": n, "nodes": limits.seed_capacity(n)[0],
                            "cache": limits.seed_capacity(n)[1]} for n in seeds],
                "checker_initial_node_cap": limits.check_capacity(args.target)},
        }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except DiagnosticCancelled as exc:
        result = {
            "family": args.family, "target": args.target, "seeds": seeds,
            "verdict": "UNKNOWN", "reason": f"cancelled: {exc}",
        }
    except Exception as exc:
        if _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None:
            _DIAGNOSTICS.censor_active(type(exc).__name__)
            _diagnostic_finish("unknown", {
                "family": args.family,
                "target": args.target,
                "verdict": "ERROR",
                "reason": f"{type(exc).__name__}: {exc}",
            })
        raise
    finally:
        for handled, previous in previous_handlers.items():
            signal.signal(handled, previous)
    diagnostic_status = ("cancelled" if was_cancelled else
                         "completed" if result.get("verdict") == "VERIFIED"
                         else "unknown")
    if _DIAGNOSTICS_ENABLED and _DIAGNOSTICS is not None:
        if _DIAGNOSTICS.active:
            _DIAGNOSTICS.censor_active(diagnostic_status)
        _diagnostic_finish(diagnostic_status, result)
    print(f"{result['verdict']} family={args.family} target={args.target} "
          f"seeds={','.join(map(str, result['seeds'])) or '-'}")
    if result.get("reason"):
        print(f"reason: {result['reason']}")
    if result.get("certificate"):
        print(f"certificate: {result['certificate']}")
        if result.get("policy") is not None:
            print(f"policy: {result['policy']}")
    # UNKNOWN is a scientific verdict rather than a driver crash, but it must
    # not share an exit code with VERIFIED: only VERIFIED is decisive, and a
    # caller that tests the exit status would otherwise read a decline as a
    # success.  Mirror tlsfcertcheck: 0 VERIFIED, 3 UNKNOWN, 4 anything else.
    return {"VERIFIED": 0, "UNKNOWN": 3}.get(result["verdict"], 4)


if __name__ == "__main__":
    raise SystemExit(main())
