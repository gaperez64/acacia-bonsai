#!/usr/bin/env python3
"""Select the best <=4-arm subset from an isolated arm census (§7.5).

Reads the *-summary.tsv files run-syntcomp26-coverage.py writes for each arm
(solver_label, instance, smallest_cap_solved, decisive_result, decisive_seconds,
still_unsolved_at_60, failure_kind_at_60) and enumerates every arm subset of size
1 to 4 that:
  - includes at least one arm that returned REALIZABLE somewhere in the census
  - includes at least one arm that returned UNREALIZABLE somewhere in the census
  - contains no duplicate arm

ranked by, in order:
  1. size of the union of decisive answers at cap 17s (larger is better)
  2. sum of decisive_seconds on the instances the subset answers (smaller is better)
  3. count of answers with decisive_seconds < 1.0 (larger is better)
  4. number of arms in the subset (fewer is better)

The isolated union is an oracle upper bound on what a race could achieve, not a
race result -- an actual concurrent race (P5's static-replacement experiment)
still has to confirm it, because racing several arms changes contention and can
change which one answers first.  This script only prunes the search space of
which four arms are worth racing.

Peak RSS, the third tie-breaker in the handoff, is not written by the run-per-
instance TSV this reads and is not fabricated here; it is left out, and the
tie-break falls through to the next criterion.  If it is ever needed, add a
memory-tracking column to run-syntcomp26-coverage.py's output rather than
estimate it here.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import pathlib
import sys

DECISIVE = {"REALIZABLE", "UNREALIZABLE"}


def load_arm(path: pathlib.Path) -> dict[str, tuple[str, float]]:
    """Return {instance: (decisive_result, decisive_seconds)} for one arm."""
    answers: dict[str, tuple[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            result = row["decisive_result"]
            if result in DECISIVE:
                answers[row["instance"]] = (result, float(row["decisive_seconds"]))
    return answers


def evaluate(subset: tuple[str, ...], arms: dict[str, dict[str, tuple[str, float]]]):
    union: dict[str, tuple[str, float]] = {}
    for name in subset:
        for instance, (result, seconds) in arms[name].items():
            existing = union.get(instance)
            if existing is None or seconds < existing[1]:
                union[instance] = (result, seconds)
    total_seconds = sum(seconds for _, seconds in union.values())
    under_one_s = sum(1 for _, seconds in union.values() if seconds < 1.0)
    # Larger union, smaller time, more sub-1s answers, fewer arms: each term
    # negated where "larger is better" so a plain ascending sort picks the winner.
    key = (-len(union), total_seconds, -under_one_s, len(subset))
    return key, len(union), total_seconds, under_one_s


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--census-dir", required=True, type=pathlib.Path,
                        help="directory of <arm-name>-summary.tsv files")
    parser.add_argument("--max-arms", type=int, default=4)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    summaries = sorted(args.census_dir.glob("*-summary.tsv"))
    if not summaries:
        print(f"no *-summary.tsv files found under {args.census_dir}", file=sys.stderr)
        return 1

    arms: dict[str, dict[str, tuple[str, float]]] = {}
    for path in summaries:
        name = path.name.removesuffix("-summary.tsv")
        arms[name] = load_arm(path)
        counts = {}
        for result, _ in arms[name].values():
            counts[result] = counts.get(result, 0) + 1
        print(f"{name}: {len(arms[name])} decisive ({counts})")

    names = sorted(arms)
    ranked = []
    for size in range(1, args.max_arms + 1):
        for subset in itertools.combinations(names, size):
            results_present = {r for a in subset for r, _ in arms[a].values()}
            if "REALIZABLE" not in results_present or "UNREALIZABLE" not in results_present:
                continue
            key, union_size, total_seconds, under_one_s = evaluate(subset, arms)
            ranked.append((key, subset, union_size, total_seconds, under_one_s))
    ranked.sort(key=lambda row: row[0])

    print(f"\n{len(ranked)} eligible subsets (size 1..{args.max_arms}); top {args.top}:")
    rows = []
    for key, subset, union_size, total_seconds, under_one_s in ranked[: args.top]:
        row = {
            "arms": "+".join(subset),
            "decisive_union": union_size,
            "sum_decisive_seconds": round(total_seconds, 2),
            "under_1s": under_one_s,
            "arm_count": len(subset),
        }
        rows.append(row)
        print(f"  {row['decisive_union']:>5} decided  "
              f"{row['sum_decisive_seconds']:>10.2f}s total  "
              f"{row['under_1s']:>5} under 1s  "
              f"{len(subset)} arms  {row['arms']}")

    if args.output:
        with args.output.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()) if rows else
                                    ["arms", "decisive_union", "sum_decisive_seconds",
                                     "under_1s", "arm_count"], delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nwrote {args.output}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
