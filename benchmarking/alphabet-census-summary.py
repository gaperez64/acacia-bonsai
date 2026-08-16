#!/usr/bin/env python3
"""Score Step A's joint input/output BDD path-to-node census."""

from __future__ import annotations

import argparse
import csv
import pathlib
import statistics


def positive_int(row: dict[str, str], name: str) -> int | None:
    try:
        value = int(row.get(name, ""))
    except ValueError:
        return None
    return value if value > 0 else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csvs", nargs="+", type=pathlib.Path)
    parser.add_argument("--tsv", type=pathlib.Path)
    args = parser.parse_args()

    descents: list[dict[str, object]] = []
    census_rows: list[dict[str, str]] = []
    for source in args.csvs:
        with source.open(newline="") as handle:
            for row in csv.DictReader(handle):
                if row.get("checkpoint") != "alphabet-census":
                    continue
                census_rows.append(row)
                for side in ("input", "output"):
                    paths = positive_int(row, f"alphabet_{side}_paths")
                    nodes = positive_int(row, f"alphabet_{side}_nodes")
                    if paths is None or nodes is None:
                        continue
                    descents.append(
                        {
                            "source": str(source),
                            "target": row.get("target", row.get("instance", "-")),
                            "path": row.get("path", "-"),
                            "descent": side,
                            "paths": paths,
                            "nodes": nodes,
                            "ratio": paths / nodes,
                            "bdd_nodes": row.get("alphabet_bdd_nodes", ""),
                        }
                    )

    if not descents:
        parser.error("no usable alphabet-census checkpoints")

    ratios = [float(row["ratio"]) for row in descents]
    median = statistics.median(ratios)
    bins = [
        ("[1,2)", 1.0, 2.0),
        ("[2,4)", 2.0, 4.0),
        ("[4,8)", 4.0, 8.0),
        ("[8,16)", 8.0, 16.0),
        ("[16,64)", 16.0, 64.0),
        ("[64,+inf)", 64.0, float("inf")),
    ]

    targets = {str(row.get("target", row.get("instance", "-"))) for row in census_rows}
    real = sum(str(row.get("path", "")).startswith("real") for row in census_rows)
    unreal = sum(str(row.get("path", "")).startswith("unreal") for row in census_rows)
    expected_orientations = 2 * len(targets)
    missing_orientations = max(0, expected_orientations - len(census_rows))
    # Each missing orientation would contribute one input and one output
    # descent.  Treating every missing ratio as +infinity is the conservative
    # bound for the fixed median decision.
    worst_case_ratios = ratios + [float("inf")] * (2 * missing_orientations)
    worst_case_median = statistics.median(worst_case_ratios)
    print(f"census checkpoints: {len(census_rows)} ({len(targets)} targets)")
    print(f"orientations: real={real} unreal={unreal}")
    print(f"missing orientations at 120 s: {missing_orientations}")
    print(f"joint descents: {len(descents)}")
    print(f"median paths/nodes: {median:.6f}")
    print(f"worst-case median with missing=+inf: {worst_case_median:.6f}")
    print("histogram:")
    for label, low, high in bins:
        count = sum(low <= ratio < high for ratio in ratios)
        print(f"  {label}: {count}")
    decision = (
        "IMPLEMENT STEP B"
        if worst_case_median >= 4.0
        else "STOP BEFORE STEP B"
    )
    print(f"decision: {decision}")

    if args.tsv:
        args.tsv.parent.mkdir(parents=True, exist_ok=True)
        with args.tsv.open("w", newline="") as handle:
            fields = ["source", "target", "path", "descent", "paths", "nodes", "ratio", "bdd_nodes"]
            writer = csv.DictWriter(handle, fieldnames=fields, dialect="excel-tab")
            writer.writeheader()
            writer.writerows(descents)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
