#!/usr/bin/env python3
"""Source-bound requests and the GR(1) lifting capability registry.

Production matching deliberately does not inspect a filename or a benchmark
result table.  It asks the same SYFCO tools used by ``gr1_monitor_game.py`` to
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
import hashlib
import os
import pathlib
import re
import signal
import subprocess
import time
from collections.abc import Mapping


ROOT = pathlib.Path(__file__).resolve().parents[2]

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

    def __post_init__(self) -> None:
        if self.route_kind not in ROUTE_KINDS:
            raise ValueError(f"unsupported route kind {self.route_kind}")

    @property
    def source_path(self) -> pathlib.Path:
        return ROOT / self.source


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


# This is the sole family/capability registry used by the campaign and the
# generalizer.  A route describes a proof method, never a cached answer.
CAPABILITIES: dict[str, Capability] = {
    item.family: item
    for item in (
        _capability(
            "arbiter",
            "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter.tlsf",
            "a9839c448b1b56bb43ac2073afc07f2dde8af8af408a13f553e78b97ef280420",
            REAL_PROPOSAL, 2, (3, 4),
        ),
        _capability(
            "prioritized_arbiter",
            "tests/syntcomp-benchmarks/tlsf/prioritized_arbiter/parametric/prioritized_arbiter.tlsf",
            "026c8fd27461aa313f60c66b9c46d23b03bbf1150ebf0b4575b81b65c59074fd",
            REAL_PROPOSAL, 1, (3, 4),
        ),
        _capability(
            "load_balancer",
            "tests/syntcomp-benchmarks/tlsf/load_balancer/parametric/load_balancer.tlsf",
            "7d99413d91924379be7d1701b9803267ce1d8b2afccfbe8ff3538e7d4295bef4",
            REAL_PROPOSAL, 2, (2, 3, 4),
        ),
        _capability(
            "arbiter_with_cancel",
            "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter_with_cancel.tlsf",
            "faa4ddb14e7a0caaa4214af55f6f9c33bf709c242cdff7e88fc99b4ef41684a3",
            REAL_PROPOSAL, 2, (2, 3, 4),
        ),
        _capability(
            "collector_v1",
            "tests/syntcomp-benchmarks/tlsf/collector/parametric/collector_v1.tlsf",
            "ec5e47ec5ab0c8e8db0a36810268a7bb4e7e566e067e0114aa2d43597d3c7518",
            REAL_PROPOSAL, 1, (3,),
        ),
        _capability(
            "arbiter_with_buffer",
            "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter_with_buffer.tlsf",
            "9377a68b62951eaac73a3c0c55c841113e97bd17855506fefc50d1f9f891bf9e",
            REAL_PROPOSAL, 1, (2, 3, 4),
        ),
        _capability(
            "simple_arbiter_with_hints",
            "tests/syntcomp-benchmarks/tlsf/ltl_with_hints/parametric/simple_arbiter_with_hints.tlsf",
            "5a107f96609e84c73b5ffc59a24f3cd285422419efb7af92f477d75bb89154cf",
            REAL_PROPOSAL, 1, (2, 4, 6),
        ),
        _capability(
            "amba_decomposed_lock",
            "tests/syntcomp-benchmarks/tlsf/amba/amba_decomposed/parametric/amba_decomposed_lock.tlsf",
            "ce9497a013a4d93d94c330a8ec7322ce0efe4dafca6c9bf5237ca265beb7843b",
            REAL_PROPOSAL, 1, (2, 3, 4),
        ),
        _capability(
            "abcg_arbiter",
            "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/abcg_arbiter.tlsf",
            "c6d71e54800c141404de00a9ac52bdbaa61e459f614d97afdda196ab0455dfef",
            REAL_PROPOSAL, 2, (2, 3),
        ),
        _capability(
            "arbiter_on_inpchange",
            "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter_on_inpchange.tlsf",
            "fb63b0d2e20ab1d3eeb6fc763009926229006f07ae9405247d29d215fd23af93",
            REAL_PROPOSAL, 2, (2, 3, 4),
        ),
        _capability(
            "round_robin_arbiter_unreal2",
            "tests/syntcomp-benchmarks/tlsf/round_robin_arbiter_unreal/parametric/round_robin_arbiter_unreal2.tlsf",
            "c479b842361509f6877bab34307d6d1de6e60284b5506e121ddd9ffc1df01db0",
            EXACT_GAME, None, (),
        ),
        _capability(
            "prioritized_arbiter_unreal2",
            "tests/syntcomp-benchmarks/tlsf/prioritized_arbiter_unreal/parametric/prioritized_arbiter_unreal2.tlsf",
            "7134e5f7e62c30e71a5b65640038840d24f418a78a3df7228731288105d2ebe6",
            EXACT_GAME, None, (),
        ),
        _capability(
            "load_balancer_unreal2",
            "tests/syntcomp-benchmarks/tlsf/load_balancer_unreal/parametric/load_balancer_unreal2.tlsf",
            "42667ce7756e1e62d457f884ff8702be95f1cf3c000d3b8b2037bb635cef6f93",
            EXACT_GAME, None, (),
        ),
        _capability(
            "amba_case_study_unreal",
            "tests/syntcomp-benchmarks/tlsf/amba/amba/parametric/amba_case_study_unreal.tlsf",
            "4eb061890a918d6c0114965d00bbf0ee661dc63a4b050c4dd9314d87e1d1d83d",
            EXACT_GAME, None, (),
        ),
    )
}


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
