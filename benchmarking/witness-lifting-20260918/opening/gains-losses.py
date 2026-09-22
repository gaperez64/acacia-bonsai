#!/usr/bin/env python3
"""Build paired B-vs-S gain/loss tables for the completed W1 campaign."""

from __future__ import annotations

import collections
import csv
import pathlib


OPENING = pathlib.Path(__file__).resolve().parent
OUTPUT = OPENING / "gains-losses.tsv"
LEGS = ((120, 1), (120, 2), (17, 1), (17, 2))
KINDS = (
    "gain",
    "loss",
    "verdict-conflict",
    "unsolved-kind-change",
    "faster",
    "slower",
)
SENTINEL = "workstation_resupply_pb_3_pe_.ltl"
OUTPUT_COLUMNS = (
    "cap",
    "epoch",
    "instance",
    "kind",
    "baseline_status",
    "baseline_seconds",
    "candidate_status",
    "candidate_seconds",
    "delta_seconds",
)


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


def read_summary(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def summary_path(candidate, cap, epoch):
    label = f"{candidate}-cap{cap}-epoch{epoch}"
    return OPENING / f"{cap}s" / f"epoch-{epoch}" / f"{label}-summary.tsv"


def print_summary(rows):
    for cap, epoch in LEGS:
        leg_rows = [
            row for row in rows if row["cap"] == str(cap) and row["epoch"] == str(epoch)
        ]
        counts = collections.Counter(row["kind"] for row in leg_rows)
        breakdown = ", ".join(f"{counts[kind]} {kind}" for kind in KINDS if counts[kind])
        print(f"cap={cap} epoch={epoch}: {len(leg_rows)} rows ({breakdown or 'none'})")

    sentinel_rows = [row for row in rows if row["instance"] == SENTINEL]
    if sentinel_rows:
        print(f"sentinel {SENTINEL}: {len(sentinel_rows)} flagged row(s)")
        for row in sentinel_rows:
            cells = ", ".join(f"{column}={row[column]}" for column in OUTPUT_COLUMNS)
            print(f"  {cells}")
    else:
        print(f"sentinel {SENTINEL}: unaffected in all four legs (not in output)")


def main() -> int:
    rows = []
    for cap, epoch in LEGS:
        baseline = read_summary(summary_path("B", cap, epoch))
        candidate = read_summary(summary_path("S", cap, epoch))
        for comparison in compare_series(baseline, candidate):
            rows.append({"cap": str(cap), "epoch": str(epoch), **comparison})

    cap_order = {cap: index for index, cap in enumerate(dict.fromkeys(cap for cap, _ in LEGS))}
    rows.sort(
        key=lambda row: (cap_order[int(row["cap"])], int(row["epoch"]), row["instance"])
    )

    with OUTPUT.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=OUTPUT_COLUMNS, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {OUTPUT}")
    print_summary(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
