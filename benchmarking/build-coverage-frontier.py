#!/usr/bin/env python3
"""Build deterministic family frontiers from staged-cap coverage TSVs.

Parameterized families with more than one coordinate are partially ordered:
``p <= q`` only when every coordinate of ``p`` is at most the corresponding
coordinate of ``q``.  A lexicographic sort would invent comparisons between
incomparable instances and can manufacture a false cutoff, so this module uses
the componentwise order for antichains, holes, and boundary pairs.

Likewise, a family whose parameters are guessed, nonnumeric, missing, or
inconsistently named is not assigned a cutoff.  Such families are still useful
coverage clusters, but their filenames are not evidence of an ordering.

This is an offline report builder.  It reads existing TSVs and never invokes a
solver.
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from typing import Any


DECISIVE_RESULTS = frozenset(("REALIZABLE", "UNREALIZABLE"))
FAILURE_PRECEDENCE = {
    "UNKNOWN": 0,
    "TIMEOUT": 1,
    "MEMOUT": 2,
    "CRASH": 3,
    "ERROR": 4,
}
KNOWN_RESULTS = DECISIVE_RESULTS | FAILURE_PRECEDENCE.keys()
ORDERABLE_CONFIDENCE = frozenset(("exact", "override"))

DEFAULT_RUNS = (
    "B=benchmarking/_coverage26/B-runs.tsv",
    "S=benchmarking/_coverage26/S-runs.tsv",
)

METADATA_COLUMNS = frozenset(
    (
        "logical_instance",
        "tlsf_file",
        "origin_kind",
        "family_key",
        "family_display",
        "parameter_confidence",
        "parameter_names",
        "parameter_values_json",
        "parameter_dimension",
        "parameters_numeric",
        "tlsf_bytes",
    )
)
RUN_COLUMNS = frozenset(
    ("solver_label", "instance", "tlsf_file", "cap_s", "result", "seconds")
)

SUMMARY_METADATA_COLUMNS = (
    "instance",
    "tlsf_file",
    "family_key",
    "family_display",
    "origin_kind",
    "parameter_confidence",
    "parameter_names",
    "parameter_values_json",
    "parameter_dimension",
    "tlsf_bytes",
)
PORTFOLIO_COLUMNS = (
    "P_result",
    "P_smallest_cap",
    "P_seconds",
    "P_winning_solver",
)
FRONTIER_COLUMNS = (
    "family_key",
    "family_display",
    "origin_kind",
    "orderable",
    "parameter_names",
    "parameter_dimension",
    "classification",
    "count",
    "solved_count",
    "unsolved_count",
    "min_unsolved_tlsf_bytes",
    "largest_solved_parameter",
    "first_unsolved_parameter",
    "first_unsolved_instance",
    "immediate_solved_predecessor",
    "predecessor_time",
    "minimal_unsolved_points_json",
    "maximal_solved_points_json",
    "hole_witness_pairs_json",
)
PAIR_COLUMNS = (
    "family_key",
    "solved_instance",
    "solved_params_json",
    "solved_seconds",
    "unsolved_instance",
    "unsolved_params_json",
    "unsolved_failure",
    "pair_kind",
    "l1_distance",
)


class CorrectnessFailure(ValueError):
    """Opposite decisive verdicts were observed for at least one instance."""

    def __init__(self, messages: list[str]):
        super().__init__("\n".join(messages))
        self.messages = messages


@dataclass(frozen=True)
class RunSpec:
    label: str
    path: pathlib.Path


@dataclass(frozen=True)
class Observation:
    cap: Decimal
    result: str
    seconds: Decimal
    seconds_text: str


@dataclass(frozen=True)
class InstanceMetadata:
    instance: str
    tlsf_file: str
    origin_kind: str
    family_key: str
    family_display: str
    parameter_confidence: str
    parameter_names_text: str
    parameter_names: tuple[str, ...]
    parameter_values_text: str
    parameter_values: dict[str, Any]
    parameter_dimension: int
    parameters_numeric: bool
    coordinates: tuple[Decimal, ...] | None
    tlsf_bytes_text: str


@dataclass(frozen=True)
class LabelResult:
    result: str
    smallest_cap: Decimal | None
    seconds: Decimal
    seconds_text: str


@dataclass(frozen=True)
class PortfolioResult:
    result: str
    smallest_cap: Decimal | None
    seconds: Decimal | None
    seconds_text: str
    winning_solver: str


@dataclass(frozen=True)
class FamilyMember:
    metadata: InstanceMetadata
    portfolio: PortfolioResult


def require_columns(
    path: pathlib.Path, fieldnames: list[str] | None, required: frozenset[str]
) -> None:
    """Reject a malformed input before any derived report is written."""
    if fieldnames is None:
        raise ValueError(f"{path} has no TSV header")
    missing = sorted(required - set(fieldnames))
    if missing:
        raise ValueError(f"{path} is missing required columns: {', '.join(missing)}")


def parse_decimal(text: str, description: str) -> Decimal:
    """Parse one finite decimal with a useful source-oriented error."""
    try:
        value = Decimal(text.strip())
    except InvalidOperation as error:
        raise ValueError(f"invalid {description}: {text!r}") from error
    if not value.is_finite():
        raise ValueError(f"non-finite {description}: {text!r}")
    return value


def decimal_text(value: Decimal) -> str:
    """Stable, compact spelling for caps derived from command-line input."""
    if value == value.to_integral_value():
        return str(value.to_integral_value())
    text = format(value.normalize(), "f")
    return text.rstrip("0").rstrip(".") if "." in text else text


def compact_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def scalar_json(value: Any) -> str:
    """Use JSON's unambiguous numeric spelling for a parameter scalar."""
    return compact_json(value)


def parse_bool(text: str, description: str) -> bool:
    normalized = text.strip().lower()
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    raise ValueError(f"invalid {description}: {text!r} (expected true or false)")


def reject_json_constant(text: str) -> None:
    """JSON permits no NaN or infinity, despite json.loads' permissive default."""
    raise ValueError(f"non-finite JSON number {text!r}")


def parse_run_spec(text: str) -> RunSpec:
    label, separator, raw_path = text.partition("=")
    label = label.strip()
    if not separator or not label or not raw_path.strip():
        raise ValueError(f"invalid --runs value {text!r}; expected LABEL=PATH")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", label):
        raise ValueError(
            f"invalid run label {label!r}; use letters, digits, '.', '_' or '-'"
        )
    return RunSpec(label, pathlib.Path(raw_path.strip()))


def parse_caps(text: str) -> tuple[Decimal, ...]:
    pieces = [piece.strip() for piece in text.split(",")]
    if not pieces or any(not piece for piece in pieces):
        raise ValueError(f"invalid --caps value {text!r}")
    caps = tuple(parse_decimal(piece, "cap") for piece in pieces)
    if any(cap <= 0 for cap in caps):
        raise ValueError("all caps must be positive")
    if len(set(caps)) != len(caps):
        raise ValueError("--caps contains a duplicate value")
    return tuple(sorted(caps))


def load_metadata(path: pathlib.Path) -> dict[str, InstanceMetadata]:
    metadata: dict[str, InstanceMetadata] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        require_columns(path, reader.fieldnames, METADATA_COLUMNS)
        for line_number, row in enumerate(reader, 2):
            instance = row["logical_instance"].strip()
            if not instance:
                raise ValueError(f"{path}:{line_number}: empty logical_instance")
            if instance in metadata:
                raise ValueError(f"{path}:{line_number}: duplicate instance {instance}")

            names_text = row["parameter_names"].strip()
            names = tuple(names_text.split(",")) if names_text else ()
            if any(not name for name in names) or len(set(names)) != len(names):
                raise ValueError(
                    f"{path}:{line_number}: malformed parameter_names for {instance}"
                )
            try:
                dimension = int(row["parameter_dimension"])
            except ValueError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid parameter_dimension for {instance}"
                ) from error
            if dimension != len(names):
                raise ValueError(
                    f"{path}:{line_number}: parameter_dimension disagrees with "
                    f"parameter_names for {instance}"
                )

            values_text = row["parameter_values_json"].strip()
            try:
                values = json.loads(values_text, parse_constant=reject_json_constant)
                exact_values = json.loads(
                    values_text,
                    parse_int=Decimal,
                    parse_float=Decimal,
                    parse_constant=reject_json_constant,
                )
            except (json.JSONDecodeError, ValueError) as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid parameter_values_json for {instance}"
                ) from error
            if not isinstance(values, dict):
                raise ValueError(
                    f"{path}:{line_number}: parameters for {instance} are not an object"
                )
            if set(values) != set(names) or len(values) != dimension:
                raise ValueError(
                    f"{path}:{line_number}: parameter names and values disagree for "
                    f"{instance}"
                )

            numeric = parse_bool(
                row["parameters_numeric"],
                f"parameters_numeric for {instance} at {path}:{line_number}",
            )
            coordinates: tuple[Decimal, ...] | None = None
            if numeric:
                raw_coordinates: list[Decimal] = []
                for name in names:
                    value = values[name]
                    if isinstance(value, bool) or not isinstance(value, (int, float)):
                        raise ValueError(
                            f"{path}:{line_number}: parameter {name} for {instance} "
                            "is marked numeric but is not a number"
                        )
                    if not exact_values[name].is_finite():
                        raise ValueError(
                            f"{path}:{line_number}: non-finite parameter for {instance}"
                        )
                    raw_coordinates.append(exact_values[name])
                coordinates = tuple(raw_coordinates)

            metadata[instance] = InstanceMetadata(
                instance=instance,
                tlsf_file=row["tlsf_file"].strip(),
                origin_kind=row["origin_kind"].strip(),
                family_key=row["family_key"].strip(),
                family_display=row["family_display"].strip(),
                parameter_confidence=row["parameter_confidence"].strip(),
                parameter_names_text=names_text,
                parameter_names=names,
                parameter_values_text=values_text,
                parameter_values=values,
                parameter_dimension=dimension,
                parameters_numeric=numeric,
                coordinates=coordinates,
                tlsf_bytes_text=row["tlsf_bytes"].strip(),
            )
    if not metadata:
        raise ValueError(f"{path} contains no instances")
    return metadata


def load_run(
    spec: RunSpec,
    metadata: dict[str, InstanceMetadata],
    caps: tuple[Decimal, ...],
) -> dict[str, tuple[Observation, ...]]:
    """Load one staged-cap run, allowing post-decision stages to be omitted."""
    wanted_caps = set(caps)
    observations: dict[tuple[str, Decimal], Observation] = {}
    with spec.path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        require_columns(spec.path, reader.fieldnames, RUN_COLUMNS)
        for line_number, row in enumerate(reader, 2):
            embedded_label = row["solver_label"].strip()
            if embedded_label != spec.label:
                raise ValueError(
                    f"{spec.path}:{line_number}: solver_label {embedded_label!r} "
                    f"does not match --runs label {spec.label!r}"
                )
            cap = parse_decimal(
                row["cap_s"], f"cap_s at {spec.path}:{line_number}"
            )
            if cap not in wanted_caps:
                continue

            instance = row["instance"].strip()
            point = metadata.get(instance)
            if point is None:
                raise ValueError(
                    f"{spec.path}:{line_number}: unknown instance {instance!r}"
                )
            tlsf_file = row["tlsf_file"].strip()
            if tlsf_file != point.tlsf_file:
                raise ValueError(
                    f"{spec.path}:{line_number}: tlsf_file for {instance} is "
                    f"{tlsf_file!r}, expected {point.tlsf_file!r}"
                )

            result = row["result"].strip().upper()
            if result not in KNOWN_RESULTS:
                raise ValueError(
                    f"{spec.path}:{line_number}: unknown result {result!r}"
                )
            seconds_text = row["seconds"].strip()
            seconds = parse_decimal(
                seconds_text, f"seconds at {spec.path}:{line_number}"
            )
            if seconds < 0:
                raise ValueError(
                    f"{spec.path}:{line_number}: seconds must be nonnegative"
                )
            key = (instance, cap)
            if key in observations:
                raise ValueError(
                    f"{spec.path}:{line_number}: duplicate row for {instance} at "
                    f"cap {decimal_text(cap)}"
                )
            observations[key] = Observation(cap, result, seconds, seconds_text)

    missing: list[tuple[str, Decimal]] = []
    for instance in sorted(metadata):
        decisive_seen = False
        for cap in caps:
            observation = observations.get((instance, cap))
            if observation is None:
                if not decisive_seen:
                    missing.append((instance, cap))
                continue
            if observation.result in DECISIVE_RESULTS:
                decisive_seen = True
    if missing:
        examples = ", ".join(
            f"{instance}@{decimal_text(cap)}" for instance, cap in missing[:8]
        )
        suffix = " ..." if len(missing) > 8 else ""
        raise ValueError(
            f"{spec.path} is missing {len(missing)} requested staged rows: "
            f"{examples}{suffix}"
        )

    return {
        instance: tuple(
            observations[(instance, cap)]
            for cap in caps
            if (instance, cap) in observations
        )
        for instance in sorted(metadata)
    }


def find_correctness_failures(
    metadata: dict[str, InstanceMetadata],
    specs: list[RunSpec],
    runs: dict[str, dict[str, tuple[Observation, ...]]],
) -> None:
    messages: list[str] = []
    ordered_instances = sorted(
        metadata, key=lambda instance: (metadata[instance].family_key, instance)
    )
    for instance in ordered_instances:
        sources: dict[str, list[str]] = {}
        for spec in specs:
            for observation in runs[spec.label][instance]:
                if observation.result in DECISIVE_RESULTS:
                    source = f"{spec.label}@{decimal_text(observation.cap)}"
                    sources.setdefault(observation.result, []).append(source)
        if len(sources) > 1:
            details = "; ".join(
                f"{verdict} ({', '.join(sources[verdict])})"
                for verdict in sorted(sources)
            )
            messages.append(f"correctness failure: {instance}: {details}")
    if messages:
        raise CorrectnessFailure(messages)


def aggregate_label(observations: tuple[Observation, ...]) -> LabelResult:
    decisive = [row for row in observations if row.result in DECISIVE_RESULTS]
    if decisive:
        # Correctness conflicts have already been rejected.  The timing is the
        # one attached to the smallest-cap decisive witness.
        earliest = min(decisive, key=lambda row: (row.cap, row.seconds))
        return LabelResult(
            earliest.result,
            earliest.cap,
            earliest.seconds,
            earliest.seconds_text,
        )
    largest_cap_row = max(observations, key=lambda row: row.cap)
    return LabelResult(
        largest_cap_row.result,
        None,
        largest_cap_row.seconds,
        largest_cap_row.seconds_text,
    )


def aggregate_portfolio(
    instance: str,
    specs: list[RunSpec],
    runs: dict[str, dict[str, tuple[Observation, ...]]],
    label_results: dict[str, LabelResult],
) -> PortfolioResult:
    decisive_rows = [
        (index, spec.label, row)
        for index, spec in enumerate(specs)
        for row in runs[spec.label][instance]
        if row.result in DECISIVE_RESULTS
    ]
    if decisive_rows:
        result = decisive_rows[0][2].result
        smallest_cap = min(row.cap for _, _, row in decisive_rows)
        fastest = min(
            decisive_rows,
            key=lambda item: (item[2].seconds, item[0], item[2].cap),
        )[2]
        winning_solver = next(
            spec.label
            for spec in specs
            if any(
                row.result in DECISIVE_RESULTS and row.cap == smallest_cap
                for row in runs[spec.label][instance]
            )
        )
        return PortfolioResult(
            result,
            smallest_cap,
            fastest.seconds,
            fastest.seconds_text,
            winning_solver,
        )

    worst = max(
        (label_results[spec.label].result for spec in specs),
        key=lambda result: FAILURE_PRECEDENCE[result],
    )
    return PortfolioResult(worst, None, None, "", "")


def build_instance_summary(
    metadata: dict[str, InstanceMetadata],
    specs: list[RunSpec],
    runs: dict[str, dict[str, tuple[Observation, ...]]],
) -> tuple[list[dict[str, str]], dict[str, PortfolioResult]]:
    """Build all rows in memory so verdict conflicts cannot emit a summary."""
    find_correctness_failures(metadata, specs, runs)
    rows: list[dict[str, str]] = []
    portfolios: dict[str, PortfolioResult] = {}
    ordered_instances = sorted(
        metadata, key=lambda instance: (metadata[instance].family_key, instance)
    )
    for instance in ordered_instances:
        point = metadata[instance]
        label_results = {
            spec.label: aggregate_label(runs[spec.label][instance]) for spec in specs
        }
        portfolio = aggregate_portfolio(instance, specs, runs, label_results)
        portfolios[instance] = portfolio
        row = {
            "instance": instance,
            "tlsf_file": point.tlsf_file,
            "family_key": point.family_key,
            "family_display": point.family_display,
            "origin_kind": point.origin_kind,
            "parameter_confidence": point.parameter_confidence,
            "parameter_names": point.parameter_names_text,
            "parameter_values_json": point.parameter_values_text,
            "parameter_dimension": str(point.parameter_dimension),
            "tlsf_bytes": point.tlsf_bytes_text,
        }
        for spec in specs:
            result = label_results[spec.label]
            row[f"{spec.label}_result"] = result.result
            row[f"{spec.label}_smallest_cap"] = (
                decimal_text(result.smallest_cap)
                if result.smallest_cap is not None
                else ""
            )
            row[f"{spec.label}_seconds"] = result.seconds_text
        row.update(
            {
                "P_result": portfolio.result,
                "P_smallest_cap": (
                    decimal_text(portfolio.smallest_cap)
                    if portfolio.smallest_cap is not None
                    else ""
                ),
                "P_seconds": portfolio.seconds_text,
                "P_winning_solver": portfolio.winning_solver,
            }
        )
        rows.append(row)
    return rows, portfolios


def is_solved(member: FamilyMember) -> bool:
    return member.portfolio.result in DECISIVE_RESULTS


def is_orderable(members: list[FamilyMember]) -> bool:
    names = {member.metadata.parameter_names for member in members}
    return (
        all(
            member.metadata.parameter_confidence in ORDERABLE_CONFIDENCE
            and member.metadata.parameters_numeric
            and member.metadata.coordinates is not None
            for member in members
        )
        and len(names) == 1
        and bool(next(iter(names)))
    )


def coordinates(member: FamilyMember) -> tuple[Decimal, ...]:
    value = member.metadata.coordinates
    if value is None:
        raise AssertionError("coordinates requested for a non-orderable member")
    return value


def componentwise_le(left: FamilyMember, right: FamilyMember) -> bool:
    return all(a <= b for a, b in zip(coordinates(left), coordinates(right)))


def componentwise_lt(left: FamilyMember, right: FamilyMember) -> bool:
    left_coordinates = coordinates(left)
    right_coordinates = coordinates(right)
    return all(a <= b for a, b in zip(left_coordinates, right_coordinates)) and any(
        a < b for a, b in zip(left_coordinates, right_coordinates)
    )


def minimal_members(members: list[FamilyMember]) -> list[FamilyMember]:
    return sorted(
        (
            member
            for member in members
            if not any(
                other is not member and componentwise_lt(other, member)
                for other in members
            )
        ),
        key=lambda member: member.metadata.instance,
    )


def maximal_members(members: list[FamilyMember]) -> list[FamilyMember]:
    return sorted(
        (
            member
            for member in members
            if not any(
                other is not member and componentwise_lt(member, other)
                for other in members
            )
        ),
        key=lambda member: member.metadata.instance,
    )


def point_description(member: FamilyMember, include_failure: bool) -> dict[str, Any]:
    description: dict[str, Any] = {
        "instance": member.metadata.instance,
        "params": member.metadata.parameter_values,
    }
    if include_failure:
        description["failure"] = member.portfolio.result
    else:
        description["seconds"] = member.portfolio.seconds_text
    return description


def min_unsolved_tlsf_bytes(unsolved: list[FamilyMember]) -> str:
    sizes: list[int] = []
    for member in unsolved:
        text = member.metadata.tlsf_bytes_text
        if not text:
            continue
        try:
            size = int(text)
        except ValueError as error:
            raise ValueError(
                f"invalid tlsf_bytes for {member.metadata.instance}: {text!r}"
            ) from error
        if size < 0:
            raise ValueError(
                f"negative tlsf_bytes for {member.metadata.instance}: {text!r}"
            )
        sizes.append(size)
    return str(min(sizes)) if sizes else ""


def base_frontier_row(
    family_key: str,
    members: list[FamilyMember],
    orderable: bool,
) -> dict[str, str]:
    solved = [member for member in members if is_solved(member)]
    unsolved = [member for member in members if not is_solved(member)]
    displays = sorted({member.metadata.family_display for member in members})
    origins = sorted({member.metadata.origin_kind for member in members})
    names = members[0].metadata.parameter_names_text if orderable else ""
    dimension = str(members[0].metadata.parameter_dimension) if orderable else ""
    return {
        "family_key": family_key,
        "family_display": ",".join(displays),
        "origin_kind": ",".join(origins),
        "orderable": "true" if orderable else "false",
        "parameter_names": names,
        "parameter_dimension": dimension,
        "classification": "",
        "count": str(len(members)),
        "solved_count": str(len(solved)),
        "unsolved_count": str(len(unsolved)),
        "min_unsolved_tlsf_bytes": min_unsolved_tlsf_bytes(unsolved),
        "largest_solved_parameter": "",
        "first_unsolved_parameter": "",
        "first_unsolved_instance": "",
        "immediate_solved_predecessor": "",
        "predecessor_time": "",
        "minimal_unsolved_points_json": "[]",
        "maximal_solved_points_json": "[]",
        "hole_witness_pairs_json": "[]",
    }


def classify_non_orderable(members: list[FamilyMember]) -> str:
    solved_count = sum(is_solved(member) for member in members)
    if solved_count == len(members):
        return "all_solved"
    if len(members) == 1:
        return "singleton_hard"
    if 0 < solved_count < len(members):
        return "mixed_direct"
    # Preserve the useful direct-cluster distinction while allowing a rare
    # non-direct family with unusable parameter metadata to say only what is
    # known: all of its observed members are unsolved.
    if all(member.metadata.origin_kind == "direct" for member in members):
        return "direct_cluster"
    return "all_unsolved"


def has_irregular_error(members: list[FamilyMember]) -> bool:
    return any(member.portfolio.result in ("ERROR", "CRASH") for member in members)


def classify_orderable(
    members: list[FamilyMember], holes: list[tuple[FamilyMember, FamilyMember]]
) -> str:
    solved_count = sum(is_solved(member) for member in members)
    if has_irregular_error(members):
        return "irregular_error"
    if solved_count == len(members):
        return "all_solved"
    if solved_count == 0:
        return "all_unsolved"
    if holes:
        return "holes"
    return "clean_cutoff"


def one_dimensional_frontier(
    row: dict[str, str], members: list[FamilyMember]
) -> None:
    solved = [member for member in members if is_solved(member)]
    unsolved = [member for member in members if not is_solved(member)]
    holes = [
        (harder, easier)
        for harder in unsolved
        for easier in solved
        if coordinates(harder)[0] < coordinates(easier)[0]
    ]
    row["classification"] = classify_orderable(members, holes)

    if solved:
        largest = max(solved, key=lambda member: coordinates(member)[0])
        parameter_name = largest.metadata.parameter_names[0]
        row["largest_solved_parameter"] = scalar_json(
            largest.metadata.parameter_values[parameter_name]
        )
    if unsolved:
        first = min(
            unsolved,
            key=lambda member: (coordinates(member)[0], member.metadata.instance),
        )
        parameter_name = first.metadata.parameter_names[0]
        row["first_unsolved_parameter"] = scalar_json(
            first.metadata.parameter_values[parameter_name]
        )
        row["first_unsolved_instance"] = first.metadata.instance
        predecessors = [
            member
            for member in solved
            if coordinates(member)[0] < coordinates(first)[0]
        ]
        if predecessors:
            predecessor_coordinate = max(
                coordinates(member)[0] for member in predecessors
            )
            predecessor = min(
                (
                    member
                    for member in predecessors
                    if coordinates(member)[0] == predecessor_coordinate
                ),
                key=lambda member: member.metadata.instance,
            )
            row["immediate_solved_predecessor"] = predecessor.metadata.instance
            row["predecessor_time"] = predecessor.portfolio.seconds_text


def multi_dimensional_frontier(
    row: dict[str, str], members: list[FamilyMember]
) -> None:
    solved = [member for member in members if is_solved(member)]
    unsolved = [member for member in members if not is_solved(member)]
    minimum_unsolved = minimal_members(unsolved)
    maximum_solved = maximal_members(solved)
    holes = sorted(
        (
            (hard, easy)
            for hard in unsolved
            for easy in solved
            if componentwise_le(hard, easy)
        ),
        key=lambda pair: (pair[0].metadata.instance, pair[1].metadata.instance),
    )

    row["classification"] = classify_orderable(members, holes)
    row["minimal_unsolved_points_json"] = compact_json(
        [point_description(member, include_failure=True) for member in minimum_unsolved]
    )
    row["maximal_solved_points_json"] = compact_json(
        [point_description(member, include_failure=False) for member in maximum_solved]
    )
    row["hole_witness_pairs_json"] = compact_json(
        [
            {
                "unsolved_instance": hard.metadata.instance,
                "unsolved_params": hard.metadata.parameter_values,
                "solved_instance": easy.metadata.instance,
                "solved_params": easy.metadata.parameter_values,
            }
            for hard, easy in holes
        ]
    )


def normalized_l1(
    left: FamilyMember,
    right: FamilyMember,
    ranges: tuple[Decimal, ...],
) -> Decimal:
    distance = Decimal(0)
    for a, b, observed_range in zip(coordinates(left), coordinates(right), ranges):
        if observed_range != 0:
            distance += abs(a - b) / observed_range
    return distance


def distance_text(distance: Decimal) -> str:
    if distance == 0:
        return "0"
    text = format(distance.normalize(), "f")
    return text.rstrip("0").rstrip(".") if "." in text else text


def make_pair_row(
    family_key: str,
    solved: FamilyMember | None,
    unsolved: FamilyMember,
    pair_kind: str,
    distance: Decimal | None,
) -> dict[str, str]:
    return {
        "family_key": family_key,
        "solved_instance": solved.metadata.instance if solved is not None else "",
        "solved_params_json": (
            compact_json(solved.metadata.parameter_values)
            if solved is not None
            else ""
        ),
        "solved_seconds": (
            solved.portfolio.seconds_text if solved is not None else ""
        ),
        "unsolved_instance": unsolved.metadata.instance,
        "unsolved_params_json": compact_json(unsolved.metadata.parameter_values),
        "unsolved_failure": unsolved.portfolio.result,
        "pair_kind": pair_kind,
        "l1_distance": distance_text(distance) if distance is not None else "",
    }


def boundary_pairs(
    family_key: str, members: list[FamilyMember]
) -> list[dict[str, str]]:
    solved = [member for member in members if is_solved(member)]
    unsolved = [member for member in members if not is_solved(member)]
    observed_ranges = tuple(
        max(coordinates(member)[index] for member in members)
        - min(coordinates(member)[index] for member in members)
        for index in range(len(coordinates(members[0])))
    )

    rows: list[dict[str, str]] = []
    covered_unsolved: set[str] = set()
    for easy in solved:
        for hard in unsolved:
            if not componentwise_lt(easy, hard):
                continue
            if any(
                componentwise_lt(easy, middle)
                and componentwise_lt(middle, hard)
                for middle in members
            ):
                continue
            rows.append(
                make_pair_row(
                    family_key,
                    easy,
                    hard,
                    "cover",
                    normalized_l1(easy, hard, observed_ranges),
                )
            )
            covered_unsolved.add(hard.metadata.instance)

    for hard in minimal_members(unsolved):
        if hard.metadata.instance in covered_unsolved:
            continue
        comparable = [
            easy
            for easy in solved
            if componentwise_le(easy, hard) or componentwise_le(hard, easy)
        ]
        if not comparable:
            rows.append(make_pair_row(family_key, None, hard, "none", None))
            continue
        distances = [
            (normalized_l1(easy, hard, observed_ranges), easy)
            for easy in comparable
        ]
        distance, nearest = min(
            distances,
            key=lambda item: (item[0], item[1].metadata.instance),
        )
        rows.append(
            make_pair_row(
                family_key, nearest, hard, "nearest_comparable", distance
            )
        )

    return rows


def build_family_reports(
    metadata: dict[str, InstanceMetadata],
    portfolios: dict[str, PortfolioResult],
) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    families: dict[str, list[FamilyMember]] = {}
    for instance in sorted(metadata):
        member = FamilyMember(metadata[instance], portfolios[instance])
        families.setdefault(member.metadata.family_key, []).append(member)

    frontier_rows: list[dict[str, str]] = []
    pair_rows: list[dict[str, str]] = []
    for family_key in sorted(families):
        members = sorted(
            families[family_key], key=lambda member: member.metadata.instance
        )
        orderable = is_orderable(members)
        row = base_frontier_row(family_key, members, orderable)
        if not orderable:
            row["classification"] = classify_non_orderable(members)
        elif members[0].metadata.parameter_dimension == 1:
            one_dimensional_frontier(row, members)
            pair_rows.extend(boundary_pairs(family_key, members))
        else:
            multi_dimensional_frontier(row, members)
            pair_rows.extend(boundary_pairs(family_key, members))
        frontier_rows.append(row)

    pair_rows.sort(
        key=lambda row: (
            row["family_key"],
            row["unsolved_instance"],
            row["solved_instance"],
            row["pair_kind"],
        )
    )
    return frontier_rows, pair_rows


def write_tsv(
    path: pathlib.Path, columns: tuple[str, ...] | list[str], rows: list[dict[str, str]]
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=columns,
            delimiter="\t",
            lineterminator="\n",
            extrasaction="raise",
        )
        writer.writeheader()
        writer.writerows(rows)


def parser() -> argparse.ArgumentParser:
    argument_parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    argument_parser.add_argument(
        "--family-metadata",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-family-instances.tsv"),
        help="instance/family metadata TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--runs",
        action="append",
        metavar="LABEL=PATH",
        help=(
            "staged-cap run TSV; repeatable (default: "
            "B=benchmarking/_coverage26/B-runs.tsv and "
            "S=benchmarking/_coverage26/S-runs.tsv)"
        ),
    )
    argument_parser.add_argument(
        "--caps",
        default="1,5,17,60",
        help="comma-separated staged caps in seconds (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--out-summary",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-coverage-summary.tsv"),
        help="per-instance portfolio TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--out-frontiers",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-family-frontiers.tsv"),
        help="family frontier TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--out-pairs",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-frontier-pairs.tsv"),
        help="solved-to-unsolved boundary pair TSV (default: %(default)s)",
    )
    return argument_parser


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        specs = [parse_run_spec(text) for text in (args.runs or DEFAULT_RUNS)]
        labels = [spec.label for spec in specs]
        if len(set(labels)) != len(labels):
            raise ValueError("--runs labels must be unique")
        if "P" in labels:
            raise ValueError("run label 'P' is reserved for portfolio columns")
        caps = parse_caps(args.caps)
        metadata = load_metadata(args.family_metadata)
        runs = {
            spec.label: load_run(spec, metadata, caps)
            for spec in specs
        }
        summary_rows, portfolios = build_instance_summary(metadata, specs, runs)
        frontier_rows, pair_rows = build_family_reports(metadata, portfolios)
    except CorrectnessFailure as error:
        for message in error.messages:
            print(message, file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    label_columns = [
        column
        for spec in specs
        for column in (
            f"{spec.label}_result",
            f"{spec.label}_smallest_cap",
            f"{spec.label}_seconds",
        )
    ]
    summary_columns = [
        *SUMMARY_METADATA_COLUMNS,
        *label_columns,
        *PORTFOLIO_COLUMNS,
    ]
    try:
        write_tsv(args.out_summary, summary_columns, summary_rows)
        write_tsv(args.out_frontiers, FRONTIER_COLUMNS, frontier_rows)
        write_tsv(args.out_pairs, PAIR_COLUMNS, pair_rows)
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print(f"wrote {args.out_summary} ({len(summary_rows)} instances)")
    print(f"wrote {args.out_frontiers} ({len(frontier_rows)} families)")
    print(f"wrote {args.out_pairs} ({len(pair_rows)} boundary pairs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
