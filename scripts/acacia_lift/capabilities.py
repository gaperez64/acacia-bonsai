#!/usr/bin/env python3
"""Source-bound requests and the GR(1) lifting capability registry.

Production matching deliberately does not inspect a filename or a benchmark
result table. It asks the configured tlsf-tools used by ``gr1_monitor_game.py`` to
fully expand and lower both the supplied bytes and a fresh instantiation of a
pinned family template.  A capability matches only when the basic TLSF,
lowered LTL formula, semantics, target, and ordered input/output ownership are
identical.

This is a strong, cheap syntactic check under the existing lowering.  It does
not prove semantic equivalence between differently written LTL formulas, so
such equivalent rewrites conservatively decline.  Template SHA-256 pins also
make a stale or substituted capability manifest fail closed.
"""

from __future__ import annotations

import dataclasses
import json
import hashlib
import os
import pathlib
import re
import signal
import subprocess
import time
from collections.abc import Mapping


ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA_FILE = pathlib.Path(__file__).resolve().parent / "data" / "capabilities-v1.json"

REAL_PROPOSAL = "real-proposal"
EXACT_GAME = "exact-game-both-sides"
SOUND_ONE_SIDED = "sound-one-sided"
ROUTE_KINDS = frozenset((REAL_PROPOSAL, EXACT_GAME, SOUND_ONE_SIDED))


class BindingDeclined(RuntimeError):
    """A fail-closed source-binding decision with a stable reason code."""

    def __init__(self, code: str, detail: str = ""):
        self.code = code
        self.detail = detail
        super().__init__(f"{code}: {detail}" if detail else code)


@dataclasses.dataclass(frozen=True)
class Capability:
    family: str
    source: str
    template_sha256: str
    parameters: tuple[str, ...]
    route_kind: str
    arity: int | None
    default_seeds: tuple[int, ...]
    stable_from: int | None = None
    role_class_count: int | None = None
    invariant_arities: tuple[int, ...] = ()
    move_arities: tuple[int, ...] = ()
    real_check: str = "policy"

    def __post_init__(self) -> None:
        if self.route_kind not in ROUTE_KINDS:
            raise ValueError(f"unsupported route kind {self.route_kind}")

    @property
    def source_path(self) -> pathlib.Path:
        return ROOT / self.source

    @property
    def measured_arity(self) -> int | None:
        if len(self.invariant_arities) != 1:
            return None
        invariant = self.invariant_arities[0]
        return (invariant if not self.move_arities or
                self.move_arities == (invariant,) else None)


def _capability(
    family: str,
    source: str,
    template_sha256: str,
    route_kind: str,
    arity: int | None,
    seeds: tuple[int, ...],
) -> Capability:
    return Capability(
        family, source, template_sha256, ("n",), route_kind, arity, seeds
    )


def _positive(value: object, field: str, *, optional: bool = False) -> int | None:
    if value is None and optional:
        return None
    if type(value) is not int or value <= 0:
        raise ValueError(f"{field} must be a positive integer")
    return value


def load_capabilities(path: pathlib.Path = DATA_FILE) -> dict[str, Capability]:
    """Validate the complete versioned registry and bind every template hash."""
    payload = json.loads(path.read_text(encoding="utf-8"))
    if set(payload) != {"schema", "capabilities", "declined_family_stable_from"} or payload["schema"] != "acacia.lift.capabilities.v1":
        raise ValueError("unsupported capability schema")
    if not isinstance(payload["capabilities"], list) or not payload["capabilities"]:
        raise ValueError("capabilities must be a nonempty list")
    declined = payload["declined_family_stable_from"]
    if not isinstance(declined, dict) or any(
        not isinstance(family, str) or
        not re.fullmatch(r"[a-z][a-z0-9_]*", family) or
        type(stable) is not int or stable <= 0
        for family, stable in declined.items()
    ):
        raise ValueError("invalid declined-family stable regimes")
    result: dict[str, Capability] = {}
    required = {"family", "source", "template_sha256", "parameters", "route_kind",
                "arity", "default_seeds", "stable_from", "role_class_count",
                "invariant_arities", "move_arities"}
    for row in payload["capabilities"]:
        if not isinstance(row, dict) or set(row) not in (required, required | {"real_check"}):
            raise ValueError("invalid capability fields")
        family, source, digest = row["family"], row["source"], row["template_sha256"]
        if not isinstance(family, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", family):
            raise ValueError("invalid capability family")
        if family in result:
            raise ValueError(f"duplicate capability {family}")
        if not isinstance(source, str) or pathlib.PurePath(source).is_absolute() or ".." in pathlib.PurePath(source).parts:
            raise ValueError(f"invalid source path for {family}")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"invalid template hash for {family}")
        template = ROOT / source
        if not template.is_file() or hashlib.sha256(template.read_bytes()).hexdigest() != digest:
            raise ValueError(f"stale capability template for {family}")
        parameters = row["parameters"]
        if parameters != ["n"]:
            raise ValueError(f"unsupported parameters for {family}")
        if row["route_kind"] not in ROUTE_KINDS:
            raise ValueError(f"unsupported route kind for {family}")
        arity = _positive(row["arity"], "arity", optional=True)
        seeds = row["default_seeds"]
        if not isinstance(seeds, list) or any(_positive(n, "seed") is None for n in seeds):
            raise ValueError(f"invalid seeds for {family}")
        if len(set(seeds)) != len(seeds) or seeds != sorted(seeds):
            raise ValueError(f"invalid seed order for {family}")
        if row["route_kind"] == EXACT_GAME and (arity is not None or seeds):
            raise ValueError(f"invalid exact-game seeds for {family}")
        if row["route_kind"] != EXACT_GAME and (arity is None or not seeds):
            raise ValueError(f"missing proposal seeds for {family}")
        stable = _positive(row["stable_from"], "stable_from", optional=True)
        roles = _positive(row["role_class_count"], "role_class_count", optional=True)
        measured = []
        for field in ("invariant_arities", "move_arities"):
            values = row[field]
            if not isinstance(values, list) or any(_positive(n, field) is None for n in values):
                raise ValueError(f"invalid {field} for {family}")
            if values != sorted(set(values)):
                raise ValueError(f"invalid {field} order for {family}")
            measured.append(tuple(values))
        real_check = row.get("real_check", "policy")
        if real_check not in ("policy", "region"):
            raise ValueError(f"invalid real_check for {family}")
        result[family] = Capability(family, source, digest, tuple(parameters),
                                    row["route_kind"], arity, tuple(seeds), stable,
                                    roles, measured[0], measured[1], real_check)
    if result.keys() & declined.keys():
        raise ValueError("declined-family stable regime overlaps a capability")
    return result


CAPABILITIES = load_capabilities()
# These historical families can only decline; their stable-regime values
# preserve the dated CLI's validation order without consulting M4 TSVs.
DECLINED_FAMILY_STABLE_FROM = json.loads(DATA_FILE.read_text(encoding="utf-8"))[
    "declined_family_stable_from"]


@dataclasses.dataclass(frozen=True)
class LoweringTools:
    tlsf2tlsf: pathlib.Path
    tlsf2ltl: pathlib.Path
    tlsfinfo: pathlib.Path


@dataclasses.dataclass(frozen=True)
class SourceIdentity:
    semantics: str
    target: str
    parameters: tuple[tuple[str, int], ...]
    inputs: tuple[str, ...]
    outputs: tuple[str, ...]
    basic_sha256: str
    lowered_formula_sha256: str

    def parameter_map(self) -> dict[str, int]:
        return dict(self.parameters)


@dataclasses.dataclass(frozen=True)
class SourceRequest:
    source_path: pathlib.Path
    source_bytes: bytes = dataclasses.field(repr=False)
    source_sha256: str
    reduction_semantics: str
    identity: SourceIdentity
    capability: Capability
    template_bytes: bytes = dataclasses.field(repr=False)
    template_sha256: str
    match_method: str
    io_mapping: tuple[tuple[str, str, str], ...]

    @property
    def family(self) -> str:
        return self.capability.family

    @property
    def target_size(self) -> int:
        try:
            return self.identity.parameter_map()["n"]
        except KeyError as error:
            raise BindingDeclined("missing_target_parameter", "n") from error

    def validate_current(self) -> None:
        """Reject a request manifest after its source or template goes stale."""
        try:
            current_source = self.source_path.read_bytes()
            current_template = self.capability.source_path.read_bytes()
        except OSError as error:
            raise BindingDeclined("bound_source_missing", str(error)) from error
        if _sha256(current_source) != self.source_sha256:
            raise BindingDeclined("stale_source_binding")
        if _sha256(current_template) != self.template_sha256:
            raise BindingDeclined("stale_capability_template")

    def materialize(self, path: pathlib.Path) -> pathlib.Path:
        path.write_bytes(self.source_bytes)
        if _sha256(path.read_bytes()) != self.source_sha256:
            raise BindingDeclined("materialized_source_hash_mismatch")
        return path

    def materialize_template(self, path: pathlib.Path) -> pathlib.Path:
        path.write_bytes(self.template_bytes)
        if _sha256(path.read_bytes()) != self.template_sha256:
            raise BindingDeclined("materialized_template_hash_mismatch")
        return path

    def artifact_binding(self) -> dict[str, object]:
        return {
            "source_sha256": self.source_sha256,
            "family": self.family,
            "route_kind": self.capability.route_kind,
            "parameters": dict(self.identity.parameters),
            "source_semantics": self.identity.semantics,
            "target_semantics": self.identity.target,
            "reduction_semantics": self.reduction_semantics,
            "inputs": list(self.identity.inputs),
            "outputs": list(self.identity.outputs),
            "template_sha256": self.template_sha256,
            "lowered_formula_sha256": self.identity.lowered_formula_sha256,
        }

    def accepts_artifact_binding(self, binding: Mapping[str, object]) -> bool:
        return dict(binding) == self.artifact_binding()

    def evidence(self) -> dict[str, object]:
        return {
            "schema": "acacia.gr1-source-binding.v1",
            "input": {
                "path": str(self.source_path),
                "sha256": self.source_sha256,
                "size_bytes": len(self.source_bytes),
            },
            "source_identity": {
                "semantics": self.identity.semantics,
                "target": self.identity.target,
                "parameters": dict(self.identity.parameters),
                "inputs": list(self.identity.inputs),
                "outputs": list(self.identity.outputs),
                "basic_tlsf_sha256": self.identity.basic_sha256,
                "lowered_formula_sha256": self.identity.lowered_formula_sha256,
            },
            "capability": {
                "family": self.family,
                "route_kind": self.capability.route_kind,
                "template": self.capability.source,
                "template_sha256": self.template_sha256,
                "parameters": list(self.capability.parameters),
                "arity": self.capability.arity,
            },
            "match": {
                "how": self.match_method,
                "check": (
                    "SYFCO basic-TLSF and normalized lowered-LTL equality, "
                    "plus exact semantics/target and ordered I/O equality"
                ),
            },
            "reduction_semantics": self.reduction_semantics,
            "io_mapping": [
                {"owner": owner, "source": source, "template": template}
                for owner, source, template in self.io_mapping
            ],
            "artifact_binding": self.artifact_binding(),
        }


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _kill_tool_group(proc: subprocess.Popen[str]) -> None:
    # The direct tool may have exited while one of its descendants still owns
    # the captured pipes, so target the process group even after poll().
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.communicate()


def _run_tool(
    command: list[pathlib.Path | str],
    source: str,
    timeout_s: float = 5.0,
    *,
    absolute_deadline_monotonic: float | None = None,
) -> str:
    effective_timeout = timeout_s
    deadline_limited = False
    if absolute_deadline_monotonic is not None:
        remaining = absolute_deadline_monotonic - time.monotonic()
        if remaining <= 0:
            raise BindingDeclined("absolute_deadline_exhausted")
        deadline_limited = remaining <= timeout_s
        effective_timeout = min(timeout_s, remaining)
    try:
        proc = subprocess.Popen(
            [str(item) for item in command], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            stdin=subprocess.PIPE, start_new_session=True,
        )
    except OSError as error:
        raise BindingDeclined("lowering_tool_failed", str(error)) from error
    if absolute_deadline_monotonic is not None:
        remaining = absolute_deadline_monotonic - time.monotonic()
        if remaining <= 0:
            _kill_tool_group(proc)
            raise BindingDeclined("absolute_deadline_exhausted")
        deadline_limited = remaining <= timeout_s
        effective_timeout = min(timeout_s, remaining)
    try:
        stdout, stderr = proc.communicate(source, timeout=effective_timeout)
    except subprocess.TimeoutExpired as error:
        _kill_tool_group(proc)
        code = (
            "absolute_deadline_exhausted"
            if deadline_limited else "lowering_tool_failed"
        )
        raise BindingDeclined(code, str(error)) from error
    except BaseException:
        _kill_tool_group(proc)
        raise
    if proc.returncode != 0:
        detail = (stderr or stdout).strip()[-300:]
        raise BindingDeclined("source_parse_or_lowering_failed", detail)
    return stdout.strip()


def _metadata(
    tools: LoweringTools,
    selection: str,
    basic: str,
    absolute_deadline_monotonic: float | None,
) -> str:
    return _run_tool(
        [tools.tlsfinfo, selection], basic,
        absolute_deadline_monotonic=absolute_deadline_monotonic,
    )


def _signals(value: str, owner: str) -> tuple[str, ...]:
    result = tuple(item for item in value.split(",") if item)
    if len(result) != len(set(result)):
        raise BindingDeclined("duplicate_atomic_proposition", owner)
    return result


def _parameter_names(
    tools: LoweringTools,
    source: str,
    absolute_deadline_monotonic: float | None,
) -> tuple[str, ...]:
    raw = _run_tool(
        [tools.tlsfinfo, "--parameters"], source,
        absolute_deadline_monotonic=absolute_deadline_monotonic,
    )
    names = tuple(item for item in re.split(r"[\s,]+", raw) if item)
    if len(names) != len(set(names)):
        raise BindingDeclined("duplicate_parameter")
    return names


def _parameter_values(
    tools: LoweringTools,
    source: str,
    names: tuple[str, ...],
    absolute_deadline_monotonic: float | None,
) -> tuple[tuple[str, int], ...]:
    normalized = _run_tool(
        [tools.tlsf2tlsf], source,
        absolute_deadline_monotonic=absolute_deadline_monotonic,
    )
    values = []
    for name in names:
        matches = re.findall(
            rf"(?m)^\s*{re.escape(name)}\s*=\s*(-?\d+)\s*;\s*$", normalized
        )
        if len(matches) != 1:
            raise BindingDeclined("parameter_assignment_not_concrete", name)
        values.append((name, int(matches[0])))
    return tuple(values)


def _identity(
    tools: LoweringTools,
    source: str,
    parameters: tuple[tuple[str, int], ...],
    absolute_deadline_monotonic: float | None,
) -> SourceIdentity:
    overrides = [
        item
        for name, value in parameters
        for item in ("--param", f"{name}={value}")
    ]
    basic = _run_tool(
        [tools.tlsf2tlsf, "--basic", *overrides], source,
        absolute_deadline_monotonic=absolute_deadline_monotonic,
    )
    semantics = _metadata(
        tools, "--semantics", basic, absolute_deadline_monotonic).lower()
    target = _metadata(
        tools, "--target", basic, absolute_deadline_monotonic).lower()
    inputs = _signals(
        _metadata(tools, "--expanded-ins", basic,
                  absolute_deadline_monotonic),
        "inputs",
    )
    outputs = _signals(
        _metadata(tools, "--expanded-outs", basic,
                  absolute_deadline_monotonic),
        "outputs",
    )
    overlap = set(inputs) & set(outputs)
    if overlap:
        raise BindingDeclined(
            "atomic_proposition_has_two_owners", ",".join(sorted(overlap))
        )
    lowered = _run_tool(
        [tools.tlsf2ltl, "--format", "ltl", *overrides], source,
        absolute_deadline_monotonic=absolute_deadline_monotonic,
    )
    return SourceIdentity(
        semantics=semantics,
        target=target,
        parameters=parameters,
        inputs=inputs,
        outputs=outputs,
        basic_sha256=_sha256(basic.encode("utf-8")),
        lowered_formula_sha256=_sha256(lowered.encode("utf-8")),
    )


def bind_source_request(
    source_path: pathlib.Path,
    tools: LoweringTools,
    reduction_semantics: str,
    *,
    family_hint: str | None = None,
    target_hint: int | None = None,
    capabilities: Mapping[str, Capability] = CAPABILITIES,
    absolute_deadline_monotonic: float | None = None,
) -> SourceRequest:
    """Bind actual TLSF bytes to exactly one content-verified capability."""
    if reduction_semantics not in ("exact", "strict"):
        raise BindingDeclined("unsupported_reduction_semantics")
    path = source_path.expanduser().resolve()
    try:
        source_bytes = path.read_bytes()
        source = source_bytes.decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise BindingDeclined("source_unreadable", str(error)) from error

    # A named capability is cheap to reject and its pin can be checked before
    # invoking SYFCO.  With no hint, defer template I/O until the parameter
    # signature has selected a small capability bucket.
    hinted: Capability | None = None
    hinted_template: tuple[bytes, str] | None = None
    if family_hint is not None:
        hinted = capabilities.get(family_hint)
        if hinted is None:
            raise BindingDeclined("unknown_capability", family_hint)
        try:
            template_bytes = hinted.source_path.read_bytes()
            template_bytes.decode("utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise BindingDeclined(
                "capability_template_unreadable", str(error)
            ) from error
        template_hash = _sha256(template_bytes)
        if template_hash != hinted.template_sha256:
            raise BindingDeclined("stale_capability_template", hinted.family)
        hinted_template = (template_bytes, template_hash)

    # This must remain the first lowering-tool call.  In particular, an
    # ordinary non-parametric TLSF declines without normalization, metadata,
    # formula lowering, or instantiating any registered template.
    names = _parameter_names(tools, source, absolute_deadline_monotonic)
    if hinted is not None:
        candidates = (hinted,) if names == hinted.parameters else ()
    else:
        candidates = tuple(
            capability for capability in capabilities.values()
            if capability.parameters == names
        )
    if not candidates:
        raise BindingDeclined("unsupported_parameter_signature")

    # Only a shortlisted parameter signature is normalized to discover its
    # concrete assignment.  Route compatibility and cheap target constraints
    # precede the expensive per-template identity checks below.
    parameters = _parameter_values(
        tools, source, names, absolute_deadline_monotonic)
    parameter_map = dict(parameters)
    if target_hint is not None and parameter_map.get("n") != target_hint:
        raise BindingDeclined("target_parameter_mismatch")
    if any(not isinstance(value, int) or value <= 0
           for _name, value in parameters):
        raise BindingDeclined("parameter_out_of_bounds")
    candidates = tuple(
        capability for capability in candidates
        if not (capability.route_kind == EXACT_GAME and
                reduction_semantics != "exact")
    )
    if not candidates:
        raise BindingDeclined("route_incompatible_with_reduction")

    # Compute the actual identity once.  _identity deliberately obtains the
    # cheap basic metadata before invoking tlsf2ltl for the final equality.
    actual = _identity(
        tools, source, parameters, absolute_deadline_monotonic)

    matches: list[tuple[Capability, bytes, SourceIdentity]] = []
    stale: list[str] = []
    for capability in candidates:
        if hinted_template is not None:
            template_bytes, template_hash = hinted_template
        else:
            try:
                template_bytes = capability.source_path.read_bytes()
                template_bytes.decode("utf-8")
            except (OSError, UnicodeDecodeError) as error:
                raise BindingDeclined(
                    "capability_template_unreadable", str(error)
                ) from error
            template_hash = _sha256(template_bytes)
            if template_hash != capability.template_sha256:
                stale.append(capability.family)
                continue
        template = template_bytes.decode("utf-8")
        expected = _identity(
            tools, template, parameters, absolute_deadline_monotonic)
        if actual == expected:
            matches.append((capability, template_bytes, expected))

    if stale and family_hint is not None:
        raise BindingDeclined("stale_capability_template", stale[0])
    if not matches:
        raise BindingDeclined("source_not_content_verified_for_capability")
    if len(matches) != 1:
        raise BindingDeclined(
            "ambiguous_source_capability", ",".join(item[0].family for item in matches)
        )
    capability, template_bytes, _expected = matches[0]
    if capability.route_kind == EXACT_GAME and reduction_semantics != "exact":
        raise BindingDeclined("exact_game_requires_exact_reduction")
    mapping = tuple(
        (owner, name, name)
        for owner, signals in (("input", actual.inputs), ("output", actual.outputs))
        for name in signals
    )
    return SourceRequest(
        source_path=path,
        source_bytes=source_bytes,
        source_sha256=_sha256(source_bytes),
        reduction_semantics=reduction_semantics,
        identity=actual,
        capability=capability,
        template_bytes=template_bytes,
        template_sha256=_sha256(template_bytes),
        match_method="content-verified-template-instantiation",
        io_mapping=mapping,
    )
