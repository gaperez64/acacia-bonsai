#!/usr/bin/env python3
"""Build the per-instance W1 family frontier from canonical epoch-1 results.

The sprint's preregistered primary epoch is epoch 1, so this report uses only
epoch-1 B/S summaries at 120 s and 17 s. It neither averages the two epochs
nor selects a fastest run. H is absent because no clean H leg was recovered
or run this sprint (the plan section 3.1 fallback recorded in decisions.md).
"""

from __future__ import annotations

import collections
import csv
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[3]
OPENING = pathlib.Path(__file__).resolve().parent
FAMILY_INSTANCES = ROOT / "benchmarking" / "syntcomp26-family-instances.tsv"
TARGET_CHECKS = OPENING.parent / "families" / "target-checks.tsv"
P2_LIST = ROOT / "benchmarking" / "symbolic-rows-20260917" / "targets" / "p2.list"
P4_LIST = ROOT / "benchmarking" / "symbolic-rows-20260917" / "targets" / "p4.list"
FRONTIER_OUTPUT = OPENING / "family-frontier.tsv"
OUT_OF_COHORT_OUTPUT = OPENING / "out-of-previous-cohort.md"
EXPECTED_INSTANCES = 1524

OUTPUT_COLUMNS = (
    "logical_instance",
    "family_key",
    "family_display",
    "origin",
    "parameter_confidence",
    "parameter_names",
    "parameter_values_json",
    "inputs",
    "outputs",
    "semantics",
    "effective_target",
    "source_sha256",
    "b_120s_result",
    "b_120s_seconds",
    "s_120s_result",
    "s_120s_seconds",
    "b_17s_result",
    "b_17s_seconds",
    "s_17s_result",
    "s_17s_seconds",
    "verdict_transition",
    "winning_witness_reference",
)
PROVENANCE_COLUMNS = OUTPUT_COLUMNS[:12]
INTERESTING_TRANSITIONS = {
    "b-solved-s-not",
    "s-solved-b-not",
    "verdict-conflict",
}
TARGET_ROLE = re.compile(r"\btarget \(([^)]+\.ltl)\)")


def status(row: dict[str, str]) -> str:
    """Return the established display status for one summary row."""
    if row["still_unsolved_at_max_cap"] == "false":
        return row["decisive_result"]
    return row["failure_kind_at_max_cap"] or "UNSOLVED"


def solved(row: dict[str, str]) -> bool:
    """Return whether a summary row has a decisive result."""
    return row["still_unsolved_at_max_cap"] == "false"


def outcome(row: dict[str, str]) -> tuple[str, str]:
    """Return result/time cells, blanking time for every unsolved outcome."""
    return status(row), row["decisive_seconds"] if solved(row) else ""


def transition(baseline: dict[str, str], candidate: dict[str, str]) -> str:
    """Classify one instance from its 120 s B/S summaries only."""
    baseline_solved = solved(baseline)
    candidate_solved = solved(candidate)
    if baseline_solved and candidate_solved:
        baseline_result = status(baseline)
        candidate_result = status(candidate)
        if baseline_result != candidate_result:
            return "verdict-conflict"
        if baseline_result == "REALIZABLE":
            return "stable-realizable"
        if baseline_result == "UNREALIZABLE":
            return "stable-unrealizable"
        raise ValueError(f"unexpected decisive result: {baseline_result!r}")
    if baseline_solved:
        return "b-solved-s-not"
    if candidate_solved:
        return "s-solved-b-not"
    return "stable-unsolved"


def index_unique(
    rows: list[dict[str, str]], key: str, source: str
) -> dict[str, dict[str, str]]:
    """Index rows while rejecting duplicate or missing keys."""
    indexed = {}
    for row in rows:
        value = row.get(key, "")
        if not value:
            raise ValueError(f"{source}: row has no {key}")
        if value in indexed:
            raise ValueError(f"{source}: duplicate {key} {value!r}")
        indexed[value] = row
    return indexed


def build_frontier(
    family_rows: list[dict[str, str]],
    b_120_rows: list[dict[str, str]],
    s_120_rows: list[dict[str, str]],
    b_17_rows: list[dict[str, str]],
    s_17_rows: list[dict[str, str]],
    witness_references: dict[str, str] | None = None,
) -> list[dict[str, str]]:
    """Join family provenance and four B/S legs without family-level ordering."""
    family_by_id = index_unique(family_rows, "logical_instance", "family metadata")
    summaries = {
        "b_120s": index_unique(b_120_rows, "instance", "B 120s summary"),
        "s_120s": index_unique(s_120_rows, "instance", "S 120s summary"),
        "b_17s": index_unique(b_17_rows, "instance", "B 17s summary"),
        "s_17s": index_unique(s_17_rows, "instance", "S 17s summary"),
    }
    expected_ids = set(family_by_id)
    for label, indexed in summaries.items():
        if set(indexed) != expected_ids:
            missing = sorted(expected_ids - set(indexed))
            extra = sorted(set(indexed) - expected_ids)
            raise ValueError(
                f"{label}: instance set differs from family metadata "
                f"(missing={missing[:5]!r}, extra={extra[:5]!r})"
            )

    references = witness_references or {}
    unknown_references = set(references) - expected_ids
    if unknown_references:
        raise ValueError(
            "target checks reference instances absent from family metadata: "
            f"{sorted(unknown_references)!r}"
        )

    frontier = []
    # Preserve family-instances.tsv order. In particular, do not rank or compare
    # parameter tuples within a family: every transition is per-instance only.
    for family_row in family_rows:
        instance = family_row["logical_instance"]
        b_120 = summaries["b_120s"][instance]
        s_120 = summaries["s_120s"][instance]
        row = {column: family_row[column] for column in PROVENANCE_COLUMNS}
        for label in ("b_120s", "s_120s", "b_17s", "s_17s"):
            result, seconds = outcome(summaries[label][instance])
            row[f"{label}_result"] = result
            row[f"{label}_seconds"] = seconds
        row["verdict_transition"] = transition(b_120, s_120)
        row["winning_witness_reference"] = references.get(instance, "")
        frontier.append(row)
    return frontier


def target_witness_references(
    target_check_rows: list[dict[str, str]], family_rows: list[dict[str, str]]
) -> dict[str, str]:
    """Point target instances at their real target-check rows, without overclaiming."""
    family_by_id = index_unique(family_rows, "logical_instance", "family metadata")
    checks_by_target = collections.defaultdict(list)
    for row in target_check_rows:
        match = TARGET_ROLE.search(row["role"])
        if match:
            checks_by_target[match.group(1)].append(row)

    references = {}
    for instance, checks in checks_by_target.items():
        if instance not in family_by_id:
            raise ValueError(f"target-check row names unknown instance {instance!r}")
        family_row = family_by_id[instance]
        if any(check["family_id"] != family_row["family_key"] for check in checks):
            raise ValueError(f"target-check family mismatch for {instance!r}")
        verified = next((check for check in checks if check["verdict"] == "VERIFIED"), None)
        family = family_row["family_display"]
        parameter = checks[-1]["parameter"]
        if verified:
            references[instance] = (
                f"families/target-checks.tsv: {family} {parameter} VERIFIED via "
                f"{verified['checker_mode']}"
            )
        else:
            verdict = checks[-1]["verdict"].split(" (", 1)[0]
            references[instance] = (
                f"families/target-checks.tsv: {family} {parameter} target unresolved "
                f"({verdict})"
            )
    return references


def read_tsv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def summary_path(candidate: str, cap: int) -> pathlib.Path:
    label = f"{candidate}-cap{cap}-epoch1"
    return OPENING / f"{cap}s" / "epoch-1" / f"{label}-summary.tsv"


def out_of_previous_cohort(
    frontier: list[dict[str, str]], already_used_ids: set[str]
) -> tuple[str, int, int]:
    """Render section 4.4's families after the P2/P4 family-level exclusion."""
    by_id = {row["logical_instance"]: row for row in frontier}
    used_families = {
        by_id[instance]["family_key"]
        for instance in already_used_ids
        if instance in by_id
    }
    qualifying = collections.defaultdict(list)
    displays = {}
    for row in frontier:
        if (
            row["family_key"] not in used_families
            and row["verdict_transition"] in INTERESTING_TRANSITIONS
        ):
            qualifying[row["family_key"]].append(row)
            displays[row["family_key"]] = row["family_display"]

    family_count = len(qualifying)
    instance_count = sum(len(rows) for rows in qualifying.values())
    lines = [
        "# Results outside the previous P4/P2 cohorts",
        "",
        (
            f"This report contains **{family_count} families** and **{instance_count} instances** "
            "with a 120s epoch-1 `b-solved-s-not`, `s-solved-b-not`, or "
            "`verdict-conflict` transition, after excluding every family touched by the prior "
            "P2 or P4 instance lists."
        ),
        "",
        (
            "The prior sprint's workstation-sibling holdout is explicitly not counted as this "
            "kind of new evidence; it is not evidence of transfer to unseen families."
        ),
        "",
    ]
    for family_key in sorted(qualifying):
        lines.extend([f"## {displays[family_key]} (`{family_key}`)", ""])
        for row in sorted(qualifying[family_key], key=lambda item: item["logical_instance"]):
            lines.append(
                f"- `{row['logical_instance']}`: `{row['verdict_transition']}`"
            )
        lines.append("")
    return "\n".join(lines), family_count, instance_count


def write_tsv(path: pathlib.Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=OUTPUT_COLUMNS, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    family_rows = read_tsv(FAMILY_INSTANCES)
    if len(family_rows) != EXPECTED_INSTANCES:
        raise ValueError(
            f"expected {EXPECTED_INSTANCES} family rows, found {len(family_rows)}"
        )
    references = target_witness_references(read_tsv(TARGET_CHECKS), family_rows)
    frontier = build_frontier(
        family_rows,
        read_tsv(summary_path("B", 120)),
        read_tsv(summary_path("S", 120)),
        read_tsv(summary_path("B", 17)),
        read_tsv(summary_path("S", 17)),
        references,
    )
    write_tsv(FRONTIER_OUTPUT, frontier)

    already_used_ids = set(P2_LIST.read_text(encoding="utf-8").split())
    already_used_ids.update(P4_LIST.read_text(encoding="utf-8").split())
    report, family_count, instance_count = out_of_previous_cohort(
        frontier, already_used_ids
    )
    OUT_OF_COHORT_OUTPUT.write_text(report, encoding="utf-8")

    counts = collections.Counter(row["verdict_transition"] for row in frontier)
    print(f"wrote {FRONTIER_OUTPUT} ({len(frontier)} rows)")
    for category in (
        "stable-realizable",
        "stable-unrealizable",
        "stable-unsolved",
        "b-solved-s-not",
        "s-solved-b-not",
        "verdict-conflict",
    ):
        print(f"  {category}: {counts[category]}")
    print(
        f"wrote {OUT_OF_COHORT_OUTPUT} "
        f"({family_count} families, {instance_count} instances)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
