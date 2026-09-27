#!/usr/bin/env python3
"""Pure B-vs-S gain/loss classification from the archived W1 campaign."""

from __future__ import annotations

def status(row):
    """Return the reference script's display status for a summary row."""
    if row["still_unsolved_at_max_cap"] == "false":
        return row["decisive_result"]
    return row["failure_kind_at_max_cap"] or "UNSOLVED"


def solved(row):
    """Use the reference script's definition of a solved summary row."""
    return row["still_unsolved_at_max_cap"] == "false"


def compare_series(baseline_summary_rows, candidate_summary_rows):
    """Compare paired summary rows and return only categorized differences.

    The categorization, its precedence, and the timing threshold are copied
    from _bm-logs.20260916-demand-sparse/scripts/closing-tables.py. Returned
    dictionaries use the gain/loss TSV columns that do not depend on a W1 leg.
    """
    baseline = {row["instance"]: row for row in baseline_summary_rows}
    candidate = {row["instance"]: row for row in candidate_summary_rows}
    assert set(candidate) == set(baseline)

    differences = []
    for instance in sorted(candidate):
        baseline_row = baseline[instance]
        candidate_row = candidate[instance]
        baseline_status = status(baseline_row)
        candidate_status = status(candidate_row)
        baseline_time = (
            float(baseline_row["decisive_seconds"]) if solved(baseline_row) else None
        )
        candidate_time = (
            float(candidate_row["decisive_seconds"]) if solved(candidate_row) else None
        )

        kind = None
        if solved(baseline_row) and solved(candidate_row) and baseline_status != candidate_status:
            kind = "verdict-conflict"
        elif solved(candidate_row) and not solved(baseline_row):
            kind = "gain"
        elif solved(baseline_row) and not solved(candidate_row):
            kind = "loss"
        elif (
            not solved(baseline_row)
            and not solved(candidate_row)
            and baseline_status != candidate_status
        ):
            kind = "unsolved-kind-change"
        elif (
            solved(baseline_row)
            and solved(candidate_row)
            and abs(candidate_time - baseline_time) > max(0.05 * baseline_time, 0.05)
        ):
            kind = "slower" if candidate_time > baseline_time else "faster"

        if kind:
            differences.append(
                {
                    "instance": instance,
                    "kind": kind,
                    "baseline_status": baseline_status,
                    "baseline_seconds": (
                        "" if baseline_time is None else f"{baseline_time:.3f}"
                    ),
                    "candidate_status": candidate_status,
                    "candidate_seconds": (
                        "" if candidate_time is None else f"{candidate_time:.3f}"
                    ),
                    "delta_seconds": (
                        ""
                        if baseline_time is None or candidate_time is None
                        else f"{candidate_time - baseline_time:+.3f}"
                    ),
                }
            )
    return differences
