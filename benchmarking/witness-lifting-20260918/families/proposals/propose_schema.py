#!/usr/bin/env python3
"""Bounded automatic schema proposer for the arbiter witness-lifting pilot.

This is deliberately a small research-layer recognizer, not a controller
decompiler and not an all-parameter synthesis procedure.  It reads two
checker-verified ASCII AIGER seed controllers, exhaustively observes their
reachable behavior within a fixed budget, and recognizes one grammar class:

* one remembered ``pending`` flag per indexed request;
* an index in ``[0, n)`` with mathematical ``+1 mod n`` update; and
* ``g[i] := pending[i] && (phase == i)``.

The proposer's evidence is untrusted.  Every returned candidate is instantiated
for both seed parameters, a third small parameter, and the requested target,
then checked by the existing sprint checkers.  Returning no candidate means
UNKNOWN.  A target success certifies only that target parameter.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Iterable, Iterator, TypeVar


ROOT = Path(__file__).resolve().parents[4]
PROPOSALS_DIR = Path(__file__).resolve().parent
MAX_CANDIDATES_PER_TARGET = 32
_INDEXED_AP = re.compile(r"(?P<role>[A-Za-z][A-Za-z0-9]*)_(?P<index>[0-9]+)\Z")


def _load_existing_builder():
    """Import the sprint's existing AIGER builder without making this dir a package."""
    module_name = "witness_lifting_build_arbiter_witness"
    if module_name in sys.modules:
        return sys.modules[module_name]
    path = PROPOSALS_DIR / "build_arbiter_witness.py"
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load existing AIGER builder from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


_existing_builder = _load_existing_builder()
AigerBuilder = _existing_builder.AigerBuilder
render_aiger = _existing_builder.render


@dataclass(frozen=True)
class ProposerLimits:
    """All proposer bounds; the package-wide candidate ceiling has one source."""

    max_candidates: int = MAX_CANDIDATES_PER_TARGET
    max_reachable_states: int = 4096
    max_trace_depth: int = 128
    max_input_valuations: int = 256
    checker_timeout_s: float = 120.0

    def __post_init__(self):
        if not 1 <= self.max_candidates <= MAX_CANDIDATES_PER_TARGET:
            raise ValueError(
                f"max_candidates must be in [1, {MAX_CANDIDATES_PER_TARGET}]"
            )
        if (
            self.max_reachable_states < 1
            or self.max_trace_depth < 1
            or self.max_input_valuations < 1
        ):
            raise ValueError("observation bounds must be positive")
        if self.checker_timeout_s <= 0:
            raise ValueError("checker_timeout_s must be positive")


@dataclass(frozen=True)
class CheckedSeed:
    parameter: int
    aiger_path: Path
    tlsf_path: Path
    checked: bool = True


@dataclass(frozen=True)
class CheckCase:
    parameter: int
    role: str
    tlsf_path: Path
    checker_mode: str


@dataclass(frozen=True)
class Family:
    family_id: str
    input_role: str
    output_role: str
    checks: tuple[CheckCase, ...]
    output_dir: Path
    verify_aiger_ltl: Path
    verify_conjuncts: Path
    tlsf2ltl: Path


@dataclass(frozen=True)
class AagCircuit:
    path: Path
    max_var: int
    inputs: tuple[int, ...]
    latches: tuple[tuple[int, int, int], ...]
    outputs: tuple[int, ...]
    gates: dict[int, tuple[int, int]]
    input_names: tuple[str, ...]
    output_names: tuple[str, ...]

    @property
    def initial_state(self) -> tuple[int, ...]:
        return tuple(reset for _, _, reset in self.latches)

    def step(
        self, state: tuple[int, ...], inputs: tuple[int, ...]
    ) -> tuple[tuple[int, ...], tuple[int, ...]]:
        if len(state) != len(self.latches) or len(inputs) != len(self.inputs):
            raise ValueError("state/input width does not match AIGER header")
        memo = dict(zip(self.inputs, inputs, strict=True))
        memo.update(
            (latch_lit, bit)
            for (latch_lit, _, _), bit in zip(self.latches, state, strict=True)
        )

        def evaluate(lit: int) -> int:
            if lit < 2:
                return lit
            inverted = lit & 1
            base = lit ^ inverted
            if base not in memo:
                try:
                    left, right = self.gates[base]
                except KeyError as exc:
                    raise ValueError(f"undefined AIGER literal {base}") from exc
                memo[base] = evaluate(left) & evaluate(right)
            return memo[base] ^ inverted

        output = tuple(evaluate(lit) for lit in self.outputs)
        next_state = tuple(evaluate(next_lit) for _, next_lit, _ in self.latches)
        return output, next_state


@dataclass(frozen=True)
class TraceStep:
    state: tuple[int, ...]
    inputs: tuple[int, ...]
    outputs: tuple[int, ...]
    next_state: tuple[int, ...]

    def compact(self) -> str:
        def bits(values):
            return "".join(str(bit) for bit in values)

        return (
            f"s={bits(self.state)} r={bits(self.inputs)} "
            f"g={bits(self.outputs)} s'={bits(self.next_state)}"
        )


@dataclass(frozen=True)
class SeedObservation:
    parameter: int
    aiger_path: Path
    input_names: tuple[str, ...]
    output_names: tuple[str, ...]
    reachable_states: int
    reachable_edges: int
    chosen_output_indices: tuple[int, ...]
    saw_zero_output: bool
    mutual_exclusion: bool
    remembered_request_traces: tuple[tuple[TraceStep, ...] | None, ...]
    all_high_prefix: tuple[TraceStep, ...]
    all_high_cycle: tuple[TraceStep, ...]
    cyclic_increment: bool
    boundary_wrap: bool

    def evidence_lines(self) -> tuple[str, ...]:
        phase = tuple(_one_hot_index(step.outputs) for step in self.all_high_cycle)
        remembered = sum(trace is not None for trace in self.remembered_request_traces)
        lines = [
            (
                f"n={self.parameter}: AAG/source-aligned roles inputs={list(self.input_names)}, "
                f"outputs={list(self.output_names)}"
            ),
            (
                f"n={self.parameter}: exhaustive reachable predicate over "
                f"{self.reachable_states} states/{self.reachable_edges} edges: "
                f"sum(g)<=1 is {self.mutual_exclusion}; output support="
                f"{list(self.chosen_output_indices)}; zero-output={self.saw_zero_output}"
            ),
            (
                f"n={self.parameter}: remembered-request evidence for "
                f"{remembered}/{self.parameter} indexed clients"
            ),
            (
                f"n={self.parameter}: all-r-high eventual phase={phase}, "
                f"+1 mod n={self.cyclic_increment}, n-1->0={self.boundary_wrap}; "
                f"cycle states={[step.state for step in self.all_high_cycle]}"
            ),
        ]
        for index, trace in enumerate(self.remembered_request_traces):
            if trace is not None:
                lines.append(
                    f"n={self.parameter}: pending[{index}] trace: "
                    + " | ".join(step.compact() for step in trace)
                )
        return tuple(lines)


@dataclass(frozen=True)
class VerificationResult:
    parameter: int
    role: str
    checker: str
    checker_mode: str
    verdict: str
    elapsed_s: float
    artifact_path: Path
    artifact_sha256: str
    stdout: str
    stderr: str
    command: tuple[str, ...]


@dataclass(frozen=True)
class CandidateSchema:
    schema_id: str
    proposal_origin: str
    memory: str
    read: str
    test: str
    update: str
    output: str
    replicate: str
    phase_origin: int
    phase_step: int
    influenced_by: tuple[str, ...]
    checks: tuple[VerificationResult, ...] = field(default=())

    def description(self) -> str:
        return (
            f"{self.schema_id}: memory={self.memory}; read={self.read}; "
            f"test={self.test}; update={self.update}; output={self.output}; "
            f"replicate={self.replicate}; phase_origin={self.phase_origin}; "
            f"phase_step=+{self.phase_step} mod n; "
            f"proposal_origin={self.proposal_origin}"
        )


def parse_aag(path: Path) -> AagCircuit:
    """Parse the bounded ASCII AIGER subset used by the checked seed witnesses."""
    path = Path(path)
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise ValueError(f"empty AIGER file: {path}")
    header = lines[0].split()
    if len(header) != 6 or header[0] != "aag":
        raise ValueError(f"only basic ASCII AIGER 'aag M I L O A' is supported: {path}")
    max_var, input_count, latch_count, output_count, gate_count = map(int, header[1:])
    cursor = 1

    def take(count: int, label: str) -> list[str]:
        nonlocal cursor
        result = lines[cursor:cursor + count]
        if len(result) != count:
            raise ValueError(f"truncated AIGER {label} section: {path}")
        cursor += count
        return result

    inputs = tuple(int(line) for line in take(input_count, "input"))
    latch_rows = []
    for line in take(latch_count, "latch"):
        fields = tuple(map(int, line.split()))
        if len(fields) not in (2, 3):
            raise ValueError(f"unsupported AIGER latch row {line!r}")
        latch_lit, next_lit = fields[:2]
        reset = fields[2] if len(fields) == 3 else 0
        if reset not in (0, 1):
            raise ValueError("only constant-reset latches are supported")
        latch_rows.append((latch_lit, next_lit, reset))
    outputs = tuple(int(line) for line in take(output_count, "output"))
    gates = {}
    for line in take(gate_count, "AND-gate"):
        lhs, rhs0, rhs1 = map(int, line.split())
        if lhs in gates:
            raise ValueError(f"duplicate AIGER gate literal {lhs}")
        gates[lhs] = (rhs0, rhs1)

    symbols: dict[tuple[str, int], str] = {}
    for line in lines[cursor:]:
        if line == "c":
            break
        match = re.fullmatch(r"([io])(\d+) (.+)", line)
        if match:
            symbols[(match.group(1), int(match.group(2)))] = match.group(3)
    try:
        input_names = tuple(symbols[("i", i)] for i in range(input_count))
        output_names = tuple(symbols[("o", i)] for i in range(output_count))
    except KeyError as exc:
        raise ValueError(f"complete AIGER input/output symbols are required: missing {exc}") from exc
    return AagCircuit(
        path=path,
        max_var=max_var,
        inputs=inputs,
        latches=tuple(latch_rows),
        outputs=outputs,
        gates=gates,
        input_names=input_names,
        output_names=output_names,
    )


def _aligned_indices(names: tuple[str, ...], role: str) -> bool:
    parsed = []
    for name in names:
        match = _INDEXED_AP.fullmatch(name)
        if match is None or match.group("role") != role:
            return False
        parsed.append(int(match.group("index")))
    return parsed == list(range(len(names)))


def _source_declarations(path: Path) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """Read AP roles from the already-instantiated basic TLSF source."""
    text = Path(path).read_text(encoding="utf-8")

    def declaration(kind: str) -> tuple[str, ...]:
        match = re.search(rf"\b{kind}\s*\{{(?P<body>[^}}]*)\}}", text, re.DOTALL)
        if match is None:
            raise ValueError(f"TLSF source has no {kind} declaration: {path}")
        return tuple(
            name
            for item in match.group("body").split(";")
            if (name := item.strip())
        )

    return declaration("INPUTS"), declaration("OUTPUTS")


def _all_inputs(width: int) -> Iterator[tuple[int, ...]]:
    for value in range(1 << width):
        yield tuple((value >> index) & 1 for index in range(width))


def _one_hot_index(bits: tuple[int, ...]) -> int | None:
    if sum(bits) != 1:
        return None
    return bits.index(1)


def _reachable_edges(
    circuit: AagCircuit, limits: ProposerLimits
) -> tuple[set[tuple[int, ...]], tuple[TraceStep, ...]]:
    states = {circuit.initial_state}
    queue = deque([circuit.initial_state])
    edges = []
    while queue:
        state = queue.popleft()
        for inputs in _all_inputs(len(circuit.inputs)):
            outputs, next_state = circuit.step(state, inputs)
            edges.append(TraceStep(state, inputs, outputs, next_state))
            if next_state not in states:
                if len(states) >= limits.max_reachable_states:
                    raise OverflowError("reachable-state observation budget exhausted")
                states.add(next_state)
                queue.append(next_state)
    return states, tuple(edges)


def _remembered_request_trace(
    circuit: AagCircuit, client: int, limits: ProposerLimits
) -> tuple[TraceStep, ...] | None:
    """Find a trace granting a previously-unserved request after r[client] is low."""
    initial = (circuit.initial_state, False)
    queue = deque([(initial, 0)])
    predecessor: dict[
        tuple[tuple[int, ...], bool],
        tuple[tuple[tuple[int, ...], bool], TraceStep] | None,
    ] = {initial: None}
    while queue:
        node, depth = queue.popleft()
        state, waiting = node
        if depth >= limits.max_trace_depth:
            continue
        for inputs in _all_inputs(len(circuit.inputs)):
            outputs, next_state = circuit.step(state, inputs)
            edge = TraceStep(state, inputs, outputs, next_state)
            if waiting and not inputs[client] and outputs[client]:
                trace = [edge]
                cursor = node
                while predecessor[cursor] is not None:
                    previous, previous_edge = predecessor[cursor]
                    trace.append(previous_edge)
                    cursor = previous
                trace.reverse()
                return tuple(trace)
            next_waiting = (waiting or bool(inputs[client])) and not bool(outputs[client])
            next_node = (next_state, next_waiting)
            if next_node not in predecessor:
                predecessor[next_node] = (node, edge)
                queue.append((next_node, depth + 1))
    return None


def _all_high_run(
    circuit: AagCircuit, limits: ProposerLimits
) -> tuple[tuple[TraceStep, ...], tuple[TraceStep, ...]]:
    state = circuit.initial_state
    inputs = (1,) * len(circuit.inputs)
    first_seen: dict[tuple[int, ...], int] = {}
    run = []
    while state not in first_seen:
        if len(run) >= limits.max_reachable_states:
            raise OverflowError("all-high cycle observation budget exhausted")
        first_seen[state] = len(run)
        outputs, next_state = circuit.step(state, inputs)
        run.append(TraceStep(state, inputs, outputs, next_state))
        state = next_state
    cycle_start = first_seen[state]
    return tuple(run[:cycle_start]), tuple(run[cycle_start:])


def observe_seed(
    seed: CheckedSeed,
    input_role: str,
    output_role: str,
    limits: ProposerLimits = ProposerLimits(),
) -> SeedObservation | None:
    if not seed.checked or seed.parameter < 2:
        return None
    circuit = parse_aag(seed.aiger_path)
    n = seed.parameter
    if 1 << len(circuit.inputs) > limits.max_input_valuations:
        return None
    if len(circuit.inputs) != n or len(circuit.outputs) != n:
        return None
    source_inputs, source_outputs = _source_declarations(seed.tlsf_path)
    if circuit.input_names != source_inputs or circuit.output_names != source_outputs:
        return None
    if not _aligned_indices(circuit.input_names, input_role):
        return None
    if not _aligned_indices(circuit.output_names, output_role):
        return None

    states, edges = _reachable_edges(circuit, limits)
    chosen = tuple(
        index
        for index in range(n)
        if any(edge.outputs[index] for edge in edges)
    )
    remembered = tuple(_remembered_request_trace(circuit, index, limits) for index in range(n))
    prefix, cycle = _all_high_run(circuit, limits)
    phases = tuple(_one_hot_index(edge.outputs) for edge in cycle)
    cyclic_increment = (
        len(cycle) == n
        and None not in phases
        and set(phases) == set(range(n))
        and all(phases[(i + 1) % n] == (phases[i] + 1) % n for i in range(n))
    )
    boundary_wrap = any(
        phases[i] == n - 1 and phases[(i + 1) % len(phases)] == 0
        for i in range(len(phases))
    ) if phases else False
    return SeedObservation(
        parameter=n,
        aiger_path=seed.aiger_path,
        input_names=circuit.input_names,
        output_names=circuit.output_names,
        reachable_states=len(states),
        reachable_edges=len(edges),
        chosen_output_indices=chosen,
        saw_zero_output=any(not any(edge.outputs) for edge in edges),
        mutual_exclusion=all(sum(edge.outputs) <= 1 for edge in edges),
        remembered_request_traces=remembered,
        all_high_prefix=prefix,
        all_high_cycle=cycle,
        cyclic_increment=cyclic_increment,
        boundary_wrap=boundary_wrap,
    )


def _recognize_candidates(
    observations: tuple[SeedObservation, SeedObservation],
) -> Iterator[CandidateSchema]:
    """Match the one supported grammar skeleton; do not infer arbitrary AIG logic."""
    if observations[0].parameter == observations[1].parameter:
        return
    for observation in observations:
        n = observation.parameter
        if observation.chosen_output_indices != tuple(range(n)):
            return
        if not observation.saw_zero_output or not observation.mutual_exclusion:
            return
        if not all(observation.remembered_request_traces):
            return
        if not observation.cyclic_increment or not observation.boundary_wrap:
            return

    evidence = []
    for observation in observations:
        evidence.extend(observation.evidence_lines())
    yield CandidateSchema(
        schema_id="pending-flags+cyclic-index/v1",
        proposal_origin="automatic",
        memory=(
            "pending[n] local flags (reset 0) + phase index in [0,n), "
            "one-hot-zero encoded with n-1 latches"
        ),
        read="r[index], pending[index], phase",
        test="phase == index; last index is checked as n-1",
        update=(
            "simultaneous old-state updates: pending[index] := "
            "(pending[index] or r[index]) and not g[index]; phase := "
            "mathematical (phase + 1) mod n (not binary overflow)"
        ),
        output="g[index] := pending[index] and (phase == index)",
        replicate="one fixed pending/grant rule for every integer index 0 <= index < n",
        phase_origin=0,
        phase_step=1,
        influenced_by=tuple(evidence),
    )


T = TypeVar("T")


def _take_candidate_budget(candidates: Iterable[T], limits: ProposerLimits) -> tuple[T, ...]:
    """Apply the sole candidate-count budget in deterministic producer order."""
    result = []
    for candidate in candidates:
        if len(result) >= limits.max_candidates:
            break
        result.append(candidate)
    return tuple(result)


def discover_candidate_schemas(
    checked_seed_1: CheckedSeed | None,
    checked_seed_2: CheckedSeed | None,
    input_role: str = "r",
    output_role: str = "g",
    limits: ProposerLimits = ProposerLimits(),
) -> tuple[CandidateSchema, ...]:
    """Analyze two actual seeds and return bounded, unverified schema hypotheses."""
    if checked_seed_1 is None or checked_seed_2 is None:
        return ()
    try:
        first = observe_seed(checked_seed_1, input_role, output_role, limits)
        second = observe_seed(checked_seed_2, input_role, output_role, limits)
    except (OSError, OverflowError, ValueError):
        return ()
    if first is None or second is None:
        return ()
    return _take_candidate_budget(_recognize_candidates((first, second)), limits)


def instantiate(candidate: CandidateSchema, n: int) -> str:
    """Instantiate the supported schema using the existing sprint AigerBuilder."""
    if candidate.schema_id != "pending-flags+cyclic-index/v1":
        raise ValueError(f"unsupported schema {candidate.schema_id!r}")
    if n < 1:
        raise ValueError("n must be >= 1")
    if candidate.phase_step != 1:
        raise ValueError("this bounded grammar currently supports only +1 mod n")
    if not 0 <= candidate.phase_origin < n:
        raise ValueError("phase_origin must be an integer in [0,n)")

    builder = AigerBuilder()
    requests = [builder.new_var() for _ in range(n)]

    # Instantiate the inferred grammar rather than calling the existing manual
    # build() fast path.  Pending flags precede phase bits because Spot's BDD
    # construction is substantially more expensive with the inverse state-
    # variable order; this is an encoding choice, not a semantic assumption.
    pending = [builder.new_latch() for _ in range(n)]
    hot = [builder.new_latch() for _ in range(n - 1)]

    def at_physical(position: int) -> int:
        if position == 0:
            any_hot = 0
            for bit in hot:
                any_hot = builder.OR(any_hot, bit)
            return builder.NOT(any_hot)
        return hot[position - 1]

    def at_logical(index: int) -> int:
        # Python's integer modulo is mathematical modulo n.  No binary-width
        # overflow is used or silently relied upon here.
        physical = (index - candidate.phase_origin) % n
        return at_physical(physical)

    grants = []
    for index in range(n):
        grant = builder.AND(pending[index], at_logical(index))
        grants.append(grant)
        remembered = builder.OR(pending[index], requests[index])
        builder.set_latch_next(pending[index], builder.AND(remembered, builder.NOT(grant)))

    # Simultaneous old-state update: next physical position p+1 is true iff
    # old p was active.  Falling off n-1 leaves all hot bits zero, the explicit
    # one-hot-zero representation of mathematical phase 0.
    for position in range(n - 1):
        builder.set_latch_next(hot[position], at_physical(position))
    rendered = render_aiger(builder, requests, grants, n)
    return rendered + "c\nautomatic schema: pending-flags+cyclic-index/v1\n"


def _artifact_name(candidate: CandidateSchema, parameter: int) -> str:
    short_id = candidate.schema_id.split("/", maxsplit=1)[0].replace("+", "-")
    return f"automatic_{short_id}_n{parameter}.aag"


def _checker_command(
    family: Family, case: CheckCase, artifact_path: Path
) -> tuple[str, ...]:
    if case.checker_mode == "monolithic":
        return (
            sys.executable,
            str(family.verify_aiger_ltl),
            "--aiger",
            str(artifact_path),
            "--tlsf",
            str(case.tlsf_path),
            "--tlsf2ltl",
            str(family.tlsf2ltl),
        )
    if case.checker_mode == "exact conjunct decomposition":
        return (
            sys.executable,
            str(family.verify_conjuncts),
            "--aiger",
            str(artifact_path),
            "--tlsf",
            str(case.tlsf_path),
            "--tlsf2ltl",
            str(family.tlsf2ltl),
        )
    raise ValueError(f"unsupported checker mode {case.checker_mode!r}")


def _check_candidate(
    candidate: CandidateSchema,
    family: Family,
    cases: tuple[CheckCase, ...],
    limits: ProposerLimits,
) -> CandidateSchema | None:
    family.output_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for case in cases:
        artifact_path = family.output_dir / _artifact_name(candidate, case.parameter)
        artifact_text = instantiate(candidate, case.parameter)
        artifact_path.write_text(artifact_text, encoding="utf-8")
        digest = hashlib.sha256(artifact_text.encode()).hexdigest()
        command = _checker_command(family, case, artifact_path)
        started = time.monotonic()
        try:
            proc = subprocess.run(
                command,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=limits.checker_timeout_s,
            )
            elapsed = time.monotonic() - started
            verdict = "VERIFIED" if proc.returncode == 0 else (
                "REFUTED" if proc.returncode == 1 else "UNKNOWN"
            )
            stdout, stderr = proc.stdout, proc.stderr
        except subprocess.TimeoutExpired as exc:
            elapsed = time.monotonic() - started
            verdict = "UNKNOWN"
            stdout = exc.stdout or ""
            stderr = (exc.stderr or "") + "checker timeout"
        result = VerificationResult(
            parameter=case.parameter,
            role=case.role,
            checker=Path(command[1]).name,
            checker_mode=case.checker_mode,
            verdict=verdict,
            elapsed_s=elapsed,
            artifact_path=artifact_path,
            artifact_sha256=digest,
            stdout=stdout,
            stderr=stderr,
            command=command,
        )
        results.append(result)
        if verdict != "VERIFIED":
            return None
    return replace(candidate, checks=tuple(results))


def propose(
    family: Family,
    checked_seed_1: CheckedSeed | None,
    checked_seed_2: CheckedSeed | None,
    limits: ProposerLimits = ProposerLimits(),
) -> list[CandidateSchema]:
    """Propose and independently verify bounded schemas; [] is UNKNOWN/decline."""
    discovered = discover_candidate_schemas(
        checked_seed_1,
        checked_seed_2,
        family.input_role,
        family.output_role,
        limits,
    )
    if not discovered or checked_seed_1 is None or checked_seed_2 is None:
        return []
    seed_cases = (
        CheckCase(checked_seed_1.parameter, "seed automatic-schema check", checked_seed_1.tlsf_path,
                  "monolithic"),
        CheckCase(checked_seed_2.parameter, "seed automatic-schema check", checked_seed_2.tlsf_path,
                  "monolithic"),
    )
    cases = seed_cases + family.checks
    verified = []
    for candidate in discovered:
        checked = _check_candidate(candidate, family, cases, limits) if cases else candidate
        if checked is not None:
            verified.append(checked)
    return verified


def arbiter_inputs(output_dir: Path | None = None) -> tuple[Family, CheckedSeed, CheckedSeed]:
    base = ROOT / "benchmarking" / "witness-lifting-20260918" / "families"
    seeds = base / "seeds" / "arbiter"
    family = Family(
        family_id="param:tlsf/arbiters_zoo/parametric/arbiter.tlsf",
        input_role="r",
        output_role="g",
        checks=(
            CheckCase(4, "sanity automatic-schema check", seeds / "arbiter_n4.tlsf", "monolithic"),
            CheckCase(
                10,
                "target automatic-schema check (arbiter_pb_10_pe_.ltl)",
                seeds / "arbiter_n10.tlsf",
                "exact conjunct decomposition",
            ),
        ),
        output_dir=output_dir or (base / "proposals" / "arbiter"),
        verify_aiger_ltl=ROOT / "subprojects" / "tlsf-tools" / "scripts" /
        "verify_aiger_ltl.py",
        verify_conjuncts=base / "proposals" / "verify_conjuncts.py",
        tlsf2ltl=ROOT / "subprojects" / "tlsf-tools" / "build_nospot" / "tlsf2ltl",
    )
    seed_2 = CheckedSeed(
        2,
        seeds / "controllers" / "arbiter_n2.aag",
        seeds / "arbiter_n2.tlsf",
    )
    seed_3 = CheckedSeed(
        3,
        seeds / "controllers" / "arbiter_n3.aag",
        seeds / "arbiter_n3.tlsf",
    )
    return family, seed_2, seed_3


def _print_observation(observation: SeedObservation | None):
    if observation is None:
        print("  UNKNOWN: seed roles or bounds were not recognizable")
        return
    for line in observation.evidence_lines():
        print(f"  {line}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--max-candidates", type=int, default=MAX_CANDIDATES_PER_TARGET)
    parser.add_argument("--checker-timeout", type=float, default=120.0)
    parser.add_argument(
        "--discover-only",
        action="store_true",
        help="extract hints and propose without invoking checkers",
    )
    args = parser.parse_args(argv)
    limits = ProposerLimits(
        max_candidates=args.max_candidates,
        checker_timeout_s=args.checker_timeout,
    )
    family, seed_2, seed_3 = arbiter_inputs(args.output_dir)

    print("Seed observations (actual Acacia-synthesized AAGs only):")
    _print_observation(observe_seed(seed_2, family.input_role, family.output_role, limits))
    _print_observation(observe_seed(seed_3, family.input_role, family.output_role, limits))
    discovered = discover_candidate_schemas(
        seed_2, seed_3, family.input_role, family.output_role, limits
    )
    print(
        f"Proposal budget: {len(discovered)} schema hypothesis/hypotheses produced; "
        f"hard limit={limits.max_candidates}"
    )
    if args.discover_only:
        candidates = discovered
    else:
        candidates = tuple(propose(family, seed_2, seed_3, limits))
    if not candidates:
        print("UNKNOWN: no bounded schema survived recognition and checking")
        return 1

    for index, candidate in enumerate(candidates, start=1):
        print(f"Candidate {index}: {candidate.description()}")
        print("Influenced by:")
        for evidence in candidate.influenced_by:
            print(f"  - {evidence}")
        for result in candidate.checks:
            print(
                f"CHECK n={result.parameter} role={result.role}: {result.verdict} "
                f"in {result.elapsed_s:.3f}s sha256={result.artifact_sha256}"
            )
            print(f"  checker={result.checker} mode={result.checker_mode}")
            for line in result.stdout.rstrip().splitlines():
                print(f"  stdout: {line}")
            for line in result.stderr.rstrip().splitlines():
                print(f"  stderr: {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
