#!/usr/bin/env python3
"""Select a deterministic, diverse diagnostics panel from frozen frontiers.

This is an offline Stage A4 selector.  It consumes only the committed coverage,
frontier, pair, and family metadata artifacts (plus TLSF ``//STATUS`` comments
when the materialized corpus is present).  It never runs or consults a solver.
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
import sys
from collections import Counter, defaultdict
from decimal import Decimal, InvalidOperation
from typing import Iterable


# Stage A4's frozen weights.  Keep the names in output-friendly form so a row's
# score can be reconstructed directly from score_breakdown.
SCORE_WEIGHTS = {
    "minimal": 12,
    "unsolved60": 10,
    "nbr5s": 8,
    "nbr17s": 5,
    "family3": 6,
    "tlsf20k": 4,
    "tlsf50k": 2,
    "infra": -10,
}

DECISIVE_RESULTS = frozenset(("REALIZABLE", "UNREALIZABLE"))
UNSOLVED_RESULTS = frozenset(("TIMEOUT", "MEMOUT", "CRASH", "ERROR", "UNKNOWN"))
KNOWN_RESULTS = DECISIVE_RESULTS | UNSOLVED_RESULTS
INFRASTRUCTURE_FAILURE_RESULTS = frozenset(("ERROR", "UNKNOWN"))
MEMORY_LIMIT_RESULTS = frozenset(("MEMOUT", "CRASH"))
PER_FAMILY_CAP = 3
MIN_DISTINCT_FAMILIES = 10

# IMPORTANT Stage A4 deviation: do not add MEMOUT or CRASH here.  An 8 GiB
# MEMOUT, and in particular an OOM CRASH caused by signal 9, is a genuine
# unsolved point for the backward antichain rather than a frontend/translation
# failure.  Those points remain fully eligible for a forward-solver campaign.
FAILURE_KINDS = {
    "TIMEOUT": "time_limit",
    "MEMOUT": "memory_limit",
    # Both CRASH rows in the frozen 2026 summary are signal-9 OOM kills.  The
    # summary no longer carries the original signal, so preserve that known
    # classification here rather than treating them as infrastructure faults.
    "CRASH": "memory_limit",
    "ERROR": "frontend_or_translation_error",
    "UNKNOWN": "no_usable_answer",
}

SUMMARY_REQUIRED = frozenset(
    (
        "instance",
        "tlsf_file",
        "family_key",
        "family_display",
        "origin_kind",
        "parameter_confidence",
        "parameter_values_json",
        "parameter_dimension",
        "tlsf_bytes",
        "P_result",
    )
)
FRONTIER_REQUIRED = frozenset(
    (
        "family_key",
        "count",
        "first_unsolved_instance",
        "minimal_unsolved_points_json",
    )
)
PAIR_REQUIRED = frozenset(
    (
        "family_key",
        "solved_instance",
        "solved_params_json",
        "solved_seconds",
        "unsolved_instance",
        "unsolved_params_json",
        "unsolved_failure",
        "pair_kind",
    )
)
METADATA_REQUIRED = frozenset(("logical_instance", "tlsf_file"))

OUTPUT_COLUMNS = (
    "rank",
    "score",
    "instance",
    "tlsf_file",
    "family_key",
    "family_display",
    "origin_kind",
    "parameter_values_json",
    "parameter_dimension",
    "tlsf_bytes",
    "expectation",
    "unsolved_result",
    "failure_kind",
    "selection_reason",
    "neighbour_instance",
    "neighbour_params_json",
    "neighbour_seconds",
    "neighbour_result",
    "pair_kind",
    "score_breakdown",
)

STATUS_RE = re.compile(
    r"^\s*//\s*STATUS\s*:\s*(?P<status>[A-Za-z]+)\s*$", re.IGNORECASE
)
EXPECTATION_COLUMNS = ("expected", "expectation", "tlsf_status", "status")


def require_columns(
    path: pathlib.Path, fieldnames: list[str] | None, required: frozenset[str]
) -> None:
    if fieldnames is None:
        raise ValueError(f"{path} has no TSV header")
    missing = sorted(required - set(fieldnames))
    if missing:
        raise ValueError(f"{path} is missing required columns: {', '.join(missing)}")


def load_tsv(
    path: pathlib.Path, required: frozenset[str], key: str
) -> tuple[list[dict[str, str]], dict[str, dict[str, str]], list[str]]:
    rows: list[dict[str, str]] = []
    by_key: dict[str, dict[str, str]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        require_columns(path, reader.fieldnames, required)
        fieldnames = list(reader.fieldnames or ())
        for line_number, row in enumerate(reader, 2):
            value = row[key].strip()
            if not value:
                raise ValueError(f"{path}:{line_number}: empty {key}")
            if value in by_key:
                raise ValueError(f"{path}:{line_number}: duplicate {key} {value!r}")
            normalized = {name: (cell or "").strip() for name, cell in row.items()}
            rows.append(normalized)
            by_key[value] = normalized
    return rows, by_key, fieldnames


def load_pairs(path: pathlib.Path) -> dict[str, list[dict[str, str]]]:
    by_unsolved: dict[str, list[dict[str, str]]] = defaultdict(list)
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        require_columns(path, reader.fieldnames, PAIR_REQUIRED)
        for line_number, row in enumerate(reader, 2):
            normalized = {name: (cell or "").strip() for name, cell in row.items()}
            instance = normalized["unsolved_instance"]
            if not instance:
                raise ValueError(f"{path}:{line_number}: empty unsolved_instance")
            neighbour_fields = (
                normalized["solved_instance"],
                normalized["solved_params_json"],
                normalized["solved_seconds"],
            )
            if any(neighbour_fields) and not all(neighbour_fields):
                raise ValueError(
                    f"{path}:{line_number}: incomplete solved neighbour for {instance}"
                )
            by_unsolved[instance].append(normalized)
    return dict(by_unsolved)


def parse_nonnegative_int(text: str, description: str) -> int:
    try:
        value = int(text)
    except ValueError as error:
        raise ValueError(f"invalid {description}: {text!r}") from error
    if value < 0:
        raise ValueError(f"negative {description}: {text!r}")
    return value


def parse_nonnegative_decimal(text: str, description: str) -> Decimal:
    try:
        value = Decimal(text)
    except InvalidOperation as error:
        raise ValueError(f"invalid {description}: {text!r}") from error
    if not value.is_finite() or value < 0:
        raise ValueError(f"invalid {description}: {text!r}")
    return value


def parse_minimal_instances(
    path: pathlib.Path, frontier_rows: Iterable[dict[str, str]]
) -> set[str]:
    minimal: set[str] = set()
    for row in frontier_rows:
        first = row["first_unsolved_instance"]
        if first:
            minimal.add(first)
        try:
            points = json.loads(row["minimal_unsolved_points_json"] or "[]")
        except json.JSONDecodeError as error:
            raise ValueError(
                f"{path}: invalid minimal_unsolved_points_json for "
                f"{row['family_key']}"
            ) from error
        if not isinstance(points, list):
            raise ValueError(
                f"{path}: minimal_unsolved_points_json for {row['family_key']} "
                "is not a list"
            )
        for point in points:
            if not isinstance(point, dict) or not isinstance(point.get("instance"), str):
                raise ValueError(
                    f"{path}: malformed minimal unsolved point for {row['family_key']}"
                )
            minimal.add(point["instance"])
    return minimal


def complete_neighbour(pair: dict[str, str]) -> bool:
    return bool(
        pair["solved_instance"]
        and pair["solved_params_json"]
        and pair["solved_seconds"]
    )


def choose_pair(rows: list[dict[str, str]]) -> dict[str, str]:
    """Choose the fastest complete boundary neighbour, deterministically.

    A complete pair always wins over a ``pair_kind=none`` placeholder.  This
    prevents a candidate from losing known neighbour fields merely because the
    input rows happened to be ordered differently.
    """

    def key(row: dict[str, str]) -> tuple[object, ...]:
        is_complete = complete_neighbour(row)
        seconds = (
            parse_nonnegative_decimal(row["solved_seconds"], "solved_seconds")
            if is_complete
            else Decimal("Infinity")
        )
        return (
            0 if is_complete else 1,
            seconds,
            row["solved_instance"],
            row["pair_kind"],
        )

    return min(rows, key=key)


def exact_parametric(summary_row: dict[str, str]) -> bool:
    return (
        summary_row["origin_kind"] == "param"
        and summary_row["parameter_confidence"] == "exact"
    )


def score_candidate(
    summary_row: dict[str, str],
    frontier_row: dict[str, str],
    pair_row: dict[str, str],
    minimal_instances: set[str],
) -> tuple[int, str]:
    """Return the Stage A4 score and its stable, compact explanation."""
    result = summary_row["P_result"].upper()
    if result not in KNOWN_RESULTS:
        raise ValueError(
            f"unknown P_result {summary_row['P_result']!r} for "
            f"{summary_row['instance']}"
        )
    family_count = parse_nonnegative_int(
        frontier_row["count"], f"family count for {summary_row['family_key']}"
    )
    tlsf_bytes = parse_nonnegative_int(
        summary_row["tlsf_bytes"], f"tlsf_bytes for {summary_row['instance']}"
    )

    applied: list[tuple[str, int]] = []
    is_exact = exact_parametric(summary_row)
    if is_exact and summary_row["instance"] in minimal_instances:
        applied.append(("minimal", SCORE_WEIGHTS["minimal"]))
    if result in UNSOLVED_RESULTS:
        applied.append(("unsolved60", SCORE_WEIGHTS["unsolved60"]))

    if complete_neighbour(pair_row):
        seconds = parse_nonnegative_decimal(
            pair_row["solved_seconds"],
            f"neighbour seconds for {summary_row['instance']}",
        )
        if seconds <= Decimal(5):
            applied.append(("nbr5s", SCORE_WEIGHTS["nbr5s"]))
        elif seconds <= Decimal(17):
            applied.append(("nbr17s", SCORE_WEIGHTS["nbr17s"]))

    if is_exact and family_count >= 3:
        applied.append(("family3", SCORE_WEIGHTS["family3"]))
    if tlsf_bytes <= 20 * 1024:
        applied.append(("tlsf20k", SCORE_WEIGHTS["tlsf20k"]))
    elif tlsf_bytes <= 50 * 1024:
        applied.append(("tlsf50k", SCORE_WEIGHTS["tlsf50k"]))
    if result in INFRASTRUCTURE_FAILURE_RESULTS:
        applied.append(("infra", SCORE_WEIGHTS["infra"]))

    score = sum(weight for _, weight in applied)
    breakdown = ",".join(
        f"{name}{weight:+d}" for name, weight in applied
    )
    return score, breakdown


def normalize_expectation(value: str) -> str | None:
    normalized = value.strip().lower()
    if normalized == "realizable":
        return "realizable"
    if normalized == "unrealizable":
        return "unrealizable"
    return None


def metadata_expectation(
    row: dict[str, str], fieldnames: Iterable[str]
) -> tuple[bool, str | None]:
    for column in EXPECTATION_COLUMNS:
        if column not in fieldnames:
            continue
        value = row.get(column, "").strip()
        if value:
            return True, normalize_expectation(value)
    return False, None


def tlsf_expectation(path: pathlib.Path) -> str | None:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    for line in lines:
        match = STATUS_RE.match(line)
        if match is not None:
            return normalize_expectation(match.group("status"))
    return None


def expectation_for(
    metadata_path: pathlib.Path,
    metadata_row: dict[str, str],
    metadata_fields: list[str],
) -> str:
    present, expected = metadata_expectation(metadata_row, metadata_fields)
    if present:
        return expected or "none"

    tlsf_file = metadata_row["tlsf_file"]
    roots = (
        metadata_path.parent.parent / "tlsf-corpus",
        pathlib.Path("tlsf-corpus"),
    )
    seen: set[pathlib.Path] = set()
    for root in roots:
        source = root / tlsf_file
        try:
            canonical = source.resolve()
        except OSError:
            canonical = source
        if canonical in seen:
            continue
        seen.add(canonical)
        if source.is_file():
            return tlsf_expectation(source) or "none"
    return "none"


def candidate_sort_key(candidate: dict[str, str]) -> tuple[int, str]:
    return (-int(candidate["score"]), candidate["instance"])


def can_add(
    candidate: dict[str, str], family_counts: Counter[str]
) -> bool:
    return family_counts[candidate["family_key"]] < PER_FAMILY_CAP


def select_scored_candidates(
    candidates: list[dict[str, str]], target_count: int
) -> list[dict[str, str]]:
    """Apply diversity constraints using stable descending-score greediness."""
    if target_count < 0:
        raise ValueError("target_count must be nonnegative")
    ordered = sorted(candidates, key=candidate_sort_key)
    if not ordered or target_count == 0:
        return []

    # The family seed makes the "at least ten" constraint explicit even for a
    # caller requesting a small panel, where the cap of three alone is weaker.
    family_goal = min(
        MIN_DISTINCT_FAMILIES,
        target_count,
        len({candidate["family_key"] for candidate in ordered}),
    )
    selected: list[dict[str, str]] = []
    selected_instances: set[str] = set()
    family_counts: Counter[str] = Counter()

    for candidate in ordered:
        family = candidate["family_key"]
        if family in family_counts:
            continue
        selected.append(candidate)
        selected_instances.add(candidate["instance"])
        family_counts[family] += 1
        if len(family_counts) == family_goal:
            break

    def add(candidate: dict[str, str]) -> bool:
        if (
            len(selected) >= target_count
            or candidate["instance"] in selected_instances
            or not can_add(candidate, family_counts)
        ):
            return False
        selected.append(candidate)
        selected_instances.add(candidate["instance"])
        family_counts[candidate["family_key"]] += 1
        return True

    # Reserve representation for both decisive TLSF expectations when those
    # annotations exist.  Usually the 40-target panel has ample room after the
    # ten-family seed.  The replacement path handles smaller synthetic panels.
    available_expectations = {
        candidate["expectation"]
        for candidate in ordered
        if candidate["expectation"] in {"realizable", "unrealizable"}
    }
    required_expectations = tuple(
        expectation
        for expectation in ("realizable", "unrealizable")
        if expectation in available_expectations
    )
    if target_count < len(required_expectations):
        required_expectations = required_expectations[:target_count]

    for expectation in required_expectations:
        if any(row["expectation"] == expectation for row in selected):
            continue
        choices = [row for row in ordered if row["expectation"] == expectation]
        if any(add(row) for row in choices):
            continue

        # The seed filled a small panel.  Replace its lowest-ranked member only
        # when all already-met diversity requirements survive the swap.
        previously_required = set(required_expectations)
        for replacement in choices:
            if replacement["instance"] in selected_instances:
                continue
            for victim in sorted(selected, key=candidate_sort_key, reverse=True):
                trial = [row for row in selected if row is not victim]
                trial.append(replacement)
                counts = Counter(row["family_key"] for row in trial)
                represented = {
                    row["expectation"]
                    for row in trial
                    if row["expectation"] in previously_required
                }
                already_processed = set(
                    required_expectations[: required_expectations.index(expectation) + 1]
                )
                if (
                    max(counts.values(), default=0) <= PER_FAMILY_CAP
                    and len(counts) >= family_goal
                    and already_processed <= represented
                ):
                    selected.remove(victim)
                    selected_instances.remove(victim["instance"])
                    selected.append(replacement)
                    selected_instances.add(replacement["instance"])
                    family_counts = counts
                    break
            else:
                continue
            break

    for candidate in ordered:
        if len(selected) >= target_count:
            break
        add(candidate)

    return sorted(selected, key=candidate_sort_key)


def memory_bounded(candidate: dict[str, str]) -> bool:
    return candidate.get("failure_kind") == "memory_limit"


def round_robin_memory_choices(
    candidates: list[dict[str, str]], selected: list[dict[str, str]]
) -> list[dict[str, str]]:
    """Order remaining memory candidates by family-diversity rounds."""
    selected_instances = {candidate["instance"] for candidate in selected}
    selected_memory_counts = Counter(
        candidate["family_key"] for candidate in selected if memory_bounded(candidate)
    )
    choices_by_family: dict[str, list[dict[str, str]]] = defaultdict(list)
    for candidate in sorted(candidates, key=candidate_sort_key):
        if (
            memory_bounded(candidate)
            and candidate["instance"] not in selected_instances
        ):
            choices_by_family[candidate["family_key"]].append(candidate)

    ranked: list[tuple[int, int, dict[str, str]]] = []
    for family_order, (family, family_choices) in enumerate(choices_by_family.items()):
        for offset, candidate in enumerate(family_choices, 1):
            ranked.append(
                (selected_memory_counts[family] + offset, family_order, candidate)
            )
    ranked.sort(key=lambda item: (item[0], item[1]))
    return [candidate for _, _, candidate in ranked]


def apply_memory_quota(
    candidates: list[dict[str, str]],
    selected: list[dict[str, str]],
    target_count: int,
    memory_quota: int,
) -> list[dict[str, str]]:
    """Reserve panel slots for the best memory-bounded candidates."""
    if memory_quota < 0:
        raise ValueError("memory_quota must be nonnegative")
    if memory_quota > target_count:
        raise ValueError("memory_quota cannot exceed target_count")

    # The score is built around exact parametric-family evidence: a minimal
    # point, an ordered solved neighbour, and at least three observations in a
    # family.  Every memory-bounded instance in the 2026 set is instead a direct
    # TLSF file with no parameters, so it earns none of those weights and caps
    # around 14 versus 40 for a parametric candidate.  A purely scored panel
    # consequently contains no memory case, even though exhausting 8 GiB is the
    # failure mode a forward reachable solver is most likely to change.  Such a
    # panel cannot test the campaign's hypothesis, which is why this quota is
    # applied after the ordinary scored, diversity-constrained selection.
    rows = [dict(candidate, selection_reason="score") for candidate in selected]
    selected_instances = {candidate["instance"] for candidate in rows}
    shortfall = memory_quota - sum(memory_bounded(row) for row in rows)
    if shortfall <= 0:
        return sorted(rows, key=candidate_sort_key)

    choices = round_robin_memory_choices(candidates, rows)
    if len(choices) < shortfall:
        raise ValueError(
            f"memory quota {memory_quota} cannot be met: only "
            f"{memory_quota - shortfall + len(choices)} memory-bounded "
            "candidates are available"
        )

    family_counts = Counter(row["family_key"] for row in rows)
    added = 0
    for candidate in choices:
        if added == shortfall:
            break
        family = candidate["family_key"]
        if len(rows) < target_count:
            if not can_add(candidate, family_counts):
                continue
            rows.append(dict(candidate, selection_reason="memory_quota"))
        else:
            victims = [
                row
                for row in rows
                if row["selection_reason"] == "score"
                and not memory_bounded(row)
                and family_counts[row["family_key"]] > 1
            ]
            if family_counts[family] >= PER_FAMILY_CAP:
                victims = [row for row in victims if row["family_key"] == family]
            if not victims:
                continue
            victim = max(victims, key=candidate_sort_key)
            rows.remove(victim)
            selected_instances.remove(victim["instance"])
            family_counts[victim["family_key"]] -= 1
            rows.append(dict(candidate, selection_reason="memory_quota"))

        selected_instances.add(candidate["instance"])
        family_counts[family] += 1
        added += 1

    if added < shortfall:
        raise ValueError(
            f"memory quota {memory_quota} cannot be met without exceeding "
            f"the {PER_FAMILY_CAP}-per-family cap or displacing a sole "
            "family representative"
        )

    return sorted(rows, key=candidate_sort_key)


def select_candidates(
    candidates: list[dict[str, str]], target_count: int, memory_quota: int = 0
) -> list[dict[str, str]]:
    selected = select_scored_candidates(candidates, target_count)
    return apply_memory_quota(
        candidates, selected, target_count, memory_quota
    )


def build_candidates(
    summary_path: pathlib.Path,
    frontier_path: pathlib.Path,
    pairs_path: pathlib.Path,
    metadata_path: pathlib.Path,
) -> list[dict[str, str]]:
    summary_rows, summaries, _ = load_tsv(
        summary_path, SUMMARY_REQUIRED, "instance"
    )
    frontier_rows, frontiers, _ = load_tsv(
        frontier_path, FRONTIER_REQUIRED, "family_key"
    )
    pairs = load_pairs(pairs_path)
    _, metadata, metadata_fields = load_tsv(
        metadata_path, METADATA_REQUIRED, "logical_instance"
    )
    minimal_instances = parse_minimal_instances(frontier_path, frontier_rows)

    candidate_instances = set(pairs)
    candidate_instances.update(
        summary["instance"]
        for summary in summary_rows
        if summary["origin_kind"] == "direct"
        and summary["P_result"].upper() in MEMORY_LIMIT_RESULTS
    )

    candidates: list[dict[str, str]] = []
    for instance in sorted(candidate_instances):
        summary = summaries.get(instance)
        if summary is None:
            raise ValueError(f"pair candidate {instance!r} is absent from {summary_path}")
        result = summary["P_result"].upper()
        if result in DECISIVE_RESULTS:
            # The candidate set is frozen before later annotations, but tolerate
            # a newer summary by simply refusing to call a solved point a target.
            continue
        if result not in UNSOLVED_RESULTS:
            raise ValueError(f"unknown P_result {result!r} for {instance}")
        frontier = frontiers.get(summary["family_key"])
        if frontier is None:
            raise ValueError(
                f"candidate {instance!r} has no frontier for {summary['family_key']!r}"
            )
        metadata_row = metadata.get(instance)
        if metadata_row is None:
            raise ValueError(f"candidate {instance!r} is absent from {metadata_path}")
        if metadata_row["tlsf_file"] != summary["tlsf_file"]:
            raise ValueError(f"tlsf_file mismatch for candidate {instance!r}")

        pair = (
            choose_pair(pairs[instance])
            if instance in pairs
            else {
                "family_key": summary["family_key"],
                "solved_instance": "",
                "solved_params_json": "",
                "solved_seconds": "",
                "unsolved_instance": instance,
                "unsolved_params_json": summary["parameter_values_json"],
                "unsolved_failure": result,
                "pair_kind": "none",
            }
        )
        if pair["family_key"] != summary["family_key"]:
            raise ValueError(f"family_key mismatch for pair candidate {instance!r}")
        # Ordinary frontier targets still require a solved control.  A memory
        # target may be a non-orderable direct instance, in which case it is a
        # useful diagnostics target on its own and deliberately has no pair.
        if not complete_neighbour(pair) and result not in MEMORY_LIMIT_RESULTS:
            continue
        pair_failure = pair["unsolved_failure"].upper()
        if pair_failure and pair_failure != result:
            raise ValueError(
                f"failure mismatch for {instance}: pair has {pair_failure}, "
                f"summary has {result}"
            )

        neighbour = pair["solved_instance"]
        neighbour_result = ""
        if neighbour:
            neighbour_summary = summaries.get(neighbour)
            if neighbour_summary is None:
                raise ValueError(
                    f"neighbour {neighbour!r} for {instance!r} is absent from "
                    f"{summary_path}"
                )
            neighbour_result = neighbour_summary["P_result"].upper()
            if neighbour_result not in DECISIVE_RESULTS:
                raise ValueError(
                    f"neighbour {neighbour!r} for {instance!r} is not solved"
                )

        score, breakdown = score_candidate(
            summary, frontier, pair, minimal_instances
        )
        candidates.append(
            {
                "rank": "",
                "score": str(score),
                "instance": instance,
                "tlsf_file": summary["tlsf_file"],
                "family_key": summary["family_key"],
                "family_display": summary["family_display"],
                "origin_kind": summary["origin_kind"],
                "parameter_values_json": summary["parameter_values_json"],
                "parameter_dimension": summary["parameter_dimension"],
                "tlsf_bytes": summary["tlsf_bytes"],
                "expectation": expectation_for(
                    metadata_path, metadata_row, metadata_fields
                ),
                "unsolved_result": result,
                "failure_kind": FAILURE_KINDS[result],
                "neighbour_instance": neighbour,
                "neighbour_params_json": pair["solved_params_json"],
                "neighbour_seconds": pair["solved_seconds"],
                "neighbour_result": neighbour_result,
                "pair_kind": pair["pair_kind"] if neighbour else "none",
                "score_breakdown": breakdown,
            }
        )
    return candidates


def write_output(path: pathlib.Path, selected: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=OUTPUT_COLUMNS,
            delimiter="\t",
            lineterminator="\n",
            extrasaction="raise",
        )
        writer.writeheader()
        for rank, candidate in enumerate(selected, 1):
            row = dict(candidate)
            row["rank"] = str(rank)
            writer.writerow(row)


def nonnegative_target_count(text: str) -> int:
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a nonnegative integer") from None
    if value < 0:
        raise argparse.ArgumentTypeError("must be a nonnegative integer")
    return value


def parser() -> argparse.ArgumentParser:
    argument_parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    argument_parser.add_argument(
        "--summary",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-coverage-summary.tsv"),
        help="per-instance coverage summary TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--frontiers",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-family-frontiers.tsv"),
        help="family frontier TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--pairs",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-frontier-pairs.tsv"),
        help="boundary-pair TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--metadata",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-family-instances.tsv"),
        help="family instance metadata TSV (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--target-count",
        type=nonnegative_target_count,
        default=40,
        help="maximum number of targets to select (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--memory-quota",
        type=nonnegative_target_count,
        default=4,
        help="minimum number of memory-bounded targets (default: %(default)s)",
    )
    argument_parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("benchmarking/syntcomp26-frontier-preselection.tsv"),
        help="selected target TSV (default: %(default)s)",
    )
    return argument_parser


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        candidates = build_candidates(
            args.summary, args.frontiers, args.pairs, args.metadata
        )
        selected = select_candidates(
            candidates, args.target_count, args.memory_quota
        )
        write_output(args.output, selected)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    expectations = Counter(row["expectation"] for row in selected)
    memory_quota_rows = sum(
        row["selection_reason"] == "memory_quota" for row in selected
    )
    print(
        f"selected {len(selected)} targets; "
        f"{len({row['family_key'] for row in selected})} distinct families; "
        "expectations: "
        f"realizable={expectations['realizable']}, "
        f"unrealizable={expectations['unrealizable']}, "
        f"no-expectation={expectations['none']}; "
        f"memory-quota rows={memory_quota_rows}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
