#!/usr/bin/env python3
"""Adjudicate the small-inductive-invariant gates from a probe campaign.

Reads benchmarking/small-invariant-results.tsv, detects whether it contains a
core, kernel, or width probe, and prints the tables the sprint record carries
plus an explicit gate verdict, so the record's numbers are regenerated rather
than retyped.

Gate 1A (a generator-subset core is worth integrating) asks for an early core
on at least five workers from at least two families, with median compression at
most one half.  Gate 3A asks for verified kernels within budget 64 on the same
cohort.  Gate 4 asks for five workers from two families that retain the initial
vector at no more than one eighth of the checkpoint width.

Example:

    benchmarking/small-invariant-report.py \\
        --results benchmarking/small-invariant-results.tsv
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import re
import statistics
import sys


MODE_COLUMNS = {
    "core": {"core_maxima", "core_contains_init", "verified"},
    "kernel": {"kernel_maxima", "verified", "budget", "search_nodes"},
    "width": {"width", "contains_init", "matches_full_width"},
}


def family(instance: str) -> str:
    """Collapse an instance name to its parameterized family.

    syntcomp names carry their parameters three different ways -- a trailing
    index, a _pb_N_pe_ block, and a content hash -- so a family count that did
    not strip all three would report one family per instance and every
    two-family gate clause would pass trivially.
    """
    stem = instance[:-4] if instance.endswith(".ltl") else instance
    stem = re.sub(r"_pb_\d+(_\d+)*_pe_$", "", stem)
    stem = re.sub(r"_[0-9a-f]{8}$", "", stem)
    stem = re.sub(r"[_-]?\d+$", "", stem)
    return stem or instance


def detect_mode(fieldnames) -> str:
    """Identify the probe mode from its mode-specific TSV columns."""
    columns = set(fieldnames or ())
    matches = [mode for mode, required in MODE_COLUMNS.items() if required <= columns]
    if len(matches) != 1:
        found = ", ".join(sorted(columns)) or "no columns"
        raise ValueError(f"cannot detect a unique probe mode from {found}")
    return matches[0]


def read_tsv(path: pathlib.Path) -> tuple[str, list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        mode = detect_mode(reader.fieldnames)
        return mode, list(reader)


def number(row: dict[str, str], column: str) -> int:
    return int(row[column] or 0)


def optional_number(value: str | None) -> int | None:
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def worker_key(row: dict[str, str]) -> tuple[str, str, str]:
    return (row["suite"], row["instance"], row["worker"])


def is_early_core(row: dict[str, str]) -> bool:
    """Whether this non-empty core contains init before solver convergence."""
    solver_final = optional_number(row.get("solver_final_maxima"))
    return (
        number(row, "core_maxima") > 0
        and row["core_contains_init"] == "yes"
        and solver_final is not None
        and number(row, "checkpoint_maxima") < solver_final
    )


def early_core_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in rows if is_early_core(row)]


def print_worker_counts(rows: list[dict[str, str]]) -> None:
    print(f"worker rows: {len(rows)}")
    print(f"distinct workers: {len({worker_key(row) for row in rows})}")


def report_core(rows: list[dict[str, str]]) -> bool:
    print_worker_counts(rows)
    non_empty = [row for row in rows if number(row, "core_maxima") > 0]
    contains_init = [row for row in non_empty if row["core_contains_init"] == "yes"]
    early = early_core_rows(rows)
    print(f"checkpoints with a non-empty core: {len(non_empty)}")
    print(f"non-empty cores containing the initial vector: {len(contains_init)}")
    print(f"early cores containing the initial vector: {len(early)}")
    print()

    print("probe cost curve: checkpoint_maxima against probe_ms")
    print(f"  {'instance':<40}{'worker':<18}{'checkpoint':>12}{'probe ms':>12}")
    for row in sorted(
        rows,
        key=lambda row: (
            number(row, "checkpoint_maxima"),
            row["suite"],
            row["instance"],
            row["worker"],
        ),
    ):
        name = f"{row['suite']}/{row['instance']}"
        print(
            f"  {name[:39]:<40}{row['worker']:<18}"
            f"{number(row, 'checkpoint_maxima'):>12,}{row['probe_ms']:>12}"
        )
    print()

    workers = {worker_key(row) for row in early}
    families = {family(key[1]) for key in workers}
    ratios = [
        number(row, "core_maxima") / number(row, "checkpoint_maxima")
        for row in early
        if number(row, "checkpoint_maxima") > 0
    ]
    median = statistics.median(ratios) if ratios else None
    passed = len(workers) >= 5 and len(families) >= 2 and median is not None and median <= 0.5
    median_text = f"{median:.3f}" if median is not None else "n/a"
    print(
        f"GATE 1A {'PASS' if passed else 'FAIL'}: {len(workers)} distinct workers "
        f"with an early success across {len(families)} families; median "
        f"core_maxima/checkpoint_maxima = {median_text} across {len(ratios)} "
        f"early checkpoints"
    )
    return passed


def report_kernel(rows: list[dict[str, str]]) -> bool:
    print_worker_counts(rows)
    verified = [row for row in rows if row["verified"] == "yes"]
    print()
    print("verified kernels")
    print(
        f"  {'instance':<40}{'worker':<18}{'k':>5}{'checkpoint':>12}"
        f"{'kernel':>10}{'search ms':>12}{'compression':>13}"
    )
    for row in verified:
        checkpoint = number(row, "checkpoint_maxima")
        kernel = number(row, "kernel_maxima")
        ratio = f"{checkpoint / kernel:.3f}" if kernel else "n/a"
        name = f"{row['suite']}/{row['instance']}"
        print(
            f"  {name[:39]:<40}{row['worker']:<18}{number(row, 'k'):>5}"
            f"{checkpoint:>12,}{kernel:>10,}{row['search_ms']:>12}{ratio:>13}"
        )
    print()

    within_budget = [row for row in verified if number(row, "budget") <= 64]
    workers = {worker_key(row) for row in within_budget}
    families = {family(key[1]) for key in workers}
    passed = len(workers) >= 5 and len(families) >= 2
    print(
        f"GATE 3A {'PASS' if passed else 'FAIL'}: {len(workers)} distinct workers "
        f"with verified kernels at budget <= 64 across {len(families)} families; "
        f"{len(within_budget)} verified rows"
    )
    return passed


def checkpoint_key(row: dict[str, str]) -> tuple[str, str, str, str, str, int]:
    return (
        row["suite"],
        row["instance"],
        row["worker"],
        row["loop"],
        row["k"],
        number(row, "checkpoint_maxima"),
    )


def narrowest_widths(
    rows: list[dict[str, str]],
) -> list[tuple[dict[str, str], dict[str, str] | None]]:
    """Return one row and the narrowest successful row for each checkpoint."""
    checkpoints: dict[
        tuple[str, str, str, str, str, int], list[dict[str, str]]
    ] = {}
    for row in rows:
        checkpoints.setdefault(checkpoint_key(row), []).append(row)

    selected = []
    for key in sorted(checkpoints, key=lambda key: (key[5], key[:5])):
        group = checkpoints[key]
        successful = [row for row in group if row["contains_init"] == "yes"]
        best = min(successful, key=lambda row: number(row, "width")) if successful else None
        selected.append((group[0], best))
    return selected


def report_width(rows: list[dict[str, str]]) -> bool:
    print_worker_counts(rows)
    checkpoints = narrowest_widths(rows)
    print()
    print("smallest width retaining the initial vector per checkpoint")
    print(
        f"  {'instance':<40}{'worker':<18}{'k':>5}{'checkpoint':>12}"
        f"{'width':>8}{'fraction':>11}{'matches full':>14}"
    )
    qualifying = []
    for checkpoint_row, best in checkpoints:
        checkpoint = number(checkpoint_row, "checkpoint_maxima")
        width = number(best, "width") if best is not None else None
        fraction = width / checkpoint if width is not None and checkpoint else None
        width_text = f"{width:,}" if width is not None else "none"
        fraction_text = f"{fraction:.3f}" if fraction is not None else "n/a"
        matches = best["matches_full_width"] if best is not None else "n/a"
        name = f"{checkpoint_row['suite']}/{checkpoint_row['instance']}"
        print(
            f"  {name[:39]:<40}{checkpoint_row['worker']:<18}"
            f"{number(checkpoint_row, 'k'):>5}{checkpoint:>12,}{width_text:>8}"
            f"{fraction_text:>11}{matches:>14}"
        )
        if width is not None and checkpoint > 0 and width * 8 <= checkpoint:
            qualifying.append(best)
    print()

    workers = {worker_key(row) for row in qualifying}
    families = {family(key[1]) for key in workers}
    passed = len(workers) >= 5 and len(families) >= 2
    print(
        f"GATE 4 {'PASS' if passed else 'FAIL'}: {len(workers)} distinct workers "
        f"retain the initial vector at width <= 1/8 of a checkpoint across "
        f"{len(families)} families; {len(qualifying)} qualifying checkpoints"
    )
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--results", default="benchmarking/small-invariant-results.tsv")
    args = parser.parse_args()

    results_path = pathlib.Path(args.results)
    try:
        mode, rows = read_tsv(results_path)
    except ValueError as exc:
        raise SystemExit(f"{results_path}: {exc}") from exc

    print(f"probe mode: {mode}")
    reporters = {
        "core": report_core,
        "kernel": report_kernel,
        "width": report_width,
    }
    return 0 if reporters[mode](rows) else 1


if __name__ == "__main__":
    sys.exit(main())
