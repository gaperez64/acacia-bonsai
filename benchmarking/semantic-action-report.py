#!/usr/bin/env python3
"""Adjudicate Sprint A's gates from the semantic-action census.

Reads benchmarking/semantic-action-census.tsv, joins it to gap-census.tsv on
(suite, instance), and prints the tables the sprint record carries plus an
explicit gate verdict, so the record's numbers are regenerated rather than
retyped.

Gate A0 (equality quotient is worth integrating) asks for at least ten workers
from at least two families at raw_output_paths / unique_residual_roots >= 2, or
any one worker at >= 8.

Gate A2 (dominance pruning is worth attempting) asks for
unique_residual_roots / minimal_residual_roots >= 1.5 on a meaningful cohort, or
one worker at >= 4.

Example:

    benchmarking/semantic-action-report.py \\
        --census benchmarking/semantic-action-census.tsv \\
        --gap-census benchmarking/gap-census.tsv
"""

from __future__ import annotations

import argparse
import collections
import csv
import pathlib
import re
import statistics
import sys


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


def read_tsv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def load(census_path: pathlib.Path, gap_path: pathlib.Path | None) -> list[dict]:
    mechanisms = {}
    if gap_path is not None:
        for row in read_tsv(gap_path):
            mechanisms[(row["suite"], row["instance"])] = row["mechanism"]

    workers = []
    for row in read_tsv(census_path):
        roots = int(row["unique_residual_roots"] or 0)
        if row["worker"] == "none" or roots == 0:
            continue
        minimal = int(row["minimal_residual_roots"] or 0)
        workers.append(
            {
                "suite": row["suite"],
                "instance": row["instance"],
                "worker": row["worker"],
                "family": family(row["instance"]),
                "mechanism": mechanisms.get((row["suite"], row["instance"]), "?"),
                "paths": int(row["raw_output_paths"] or 0),
                "roots": roots,
                "minimal": minimal,
                "declined": int(row["dominance_declines"] or 0) > 0,
                "equality": int(row["raw_output_paths"] or 0) / roots,
                "dominance": roots / minimal if minimal else 0.0,
            }
        )
    return workers


def histogram(values, thresholds):
    return [(t, sum(1 for v in values if v >= t)) for t in thresholds]


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--census", default="benchmarking/semantic-action-census.tsv")
    parser.add_argument("--gap-census", default="benchmarking/gap-census.tsv")
    parser.add_argument("--top", type=int, default=5, help="rows per leaderboard")
    args = parser.parse_args()

    census_path = pathlib.Path(args.census)
    gap_path = pathlib.Path(args.gap_census)
    workers = load(census_path, gap_path if gap_path.exists() else None)
    if not workers:
        raise SystemExit(f"{census_path} has no worker rows with a census")

    equality = [w["equality"] for w in workers]
    print(f"workers with a census: {len(workers)}")
    print(f"median equality ratio: {statistics.median(equality):.3f}")
    print(f"maximum equality ratio: {max(equality):,.1f}")
    print(f"no-op workers (ratio exactly 1): {sum(1 for v in equality if v == 1.0)}")
    print()

    print("equality: raw_output_paths / unique_residual_roots")
    for threshold, count in histogram(equality, (2, 4, 8, 100)):
        fams = {w["family"] for w in workers if w["equality"] >= threshold}
        print(f"  >= {threshold:<4}: {count:4d} workers across {len(fams):3d} families")
    print()

    print(f"top {args.top} by equality ratio")
    print(f"  {'instance':<40}{'worker':<18}{'paths':>12}{'roots':>8}{'ratio':>14}")
    for w in sorted(workers, key=lambda w: -w["equality"])[: args.top]:
        name = f"{w['suite']}/{w['instance']}"
        print(f"  {name[:39]:<40}{w['worker']:<18}{w['paths']:>12,}{w['roots']:>8,}"
              f"{w['equality']:>14,.1f}")
    print()

    dominance = [w["dominance"] for w in workers if w["minimal"] > 0]
    declined = sum(1 for w in workers if w["declined"])
    print("dominance: unique_residual_roots / minimal_residual_roots")
    for threshold, count in histogram(dominance, (1.5, 2, 4, 8)):
        fams = {w["family"] for w in workers if w["dominance"] >= threshold}
        print(f"  >= {threshold:<4}: {count:4d} workers across {len(fams):3d} families")
    print(f"  budget declines: {declined} workers, recorded as no reduction, so these "
          f"figures are lower bounds")
    print()

    print(f"top {args.top} by dominance ratio")
    print(f"  {'instance':<40}{'worker':<18}{'roots':>8}{'minimal':>9}{'ratio':>8}")
    for w in sorted(workers, key=lambda w: -w["dominance"])[: args.top]:
        name = f"{w['suite']}/{w['instance']}"
        print(f"  {name[:39]:<40}{w['worker']:<18}{w['roots']:>8,}{w['minimal']:>9,}"
              f"{w['dominance']:>8.1f}")
    print()

    by_mechanism = collections.defaultdict(list)
    for w in workers:
        by_mechanism[w["mechanism"]].append(w)
    print("by mechanism")
    print(f"  {'cohort':<22}{'workers':>9}{'median':>9}{'>=2':>7}{'>=8':>7}{'dom>=1.5':>10}")
    for mechanism in sorted(by_mechanism):
        sub = by_mechanism[mechanism]
        print(f"  {mechanism[:21]:<22}{len(sub):>9}"
              f"{statistics.median(w['equality'] for w in sub):>9.2f}"
              f"{sum(1 for w in sub if w['equality'] >= 2):>7}"
              f"{sum(1 for w in sub if w['equality'] >= 8):>7}"
              f"{sum(1 for w in sub if w['dominance'] >= 1.5):>10}")
    print()

    at_two = [w for w in workers if w["equality"] >= 2]
    a0 = (len(at_two) >= 10 and len({w["family"] for w in at_two}) >= 2) or any(
        w["equality"] >= 8 for w in workers
    )
    at_1_5 = [w for w in workers if w["dominance"] >= 1.5]
    a2 = len(at_1_5) >= 10 or any(w["dominance"] >= 4 for w in workers)
    print(f"GATE A0 {'PASS' if a0 else 'FAIL'}: "
          f"{len(at_two)} workers at ratio >= 2 across "
          f"{len({w['family'] for w in at_two})} families; "
          f"{sum(1 for w in workers if w['equality'] >= 8)} at >= 8")
    print(f"GATE A2 {'PASS' if a2 else 'FAIL'}: "
          f"{len(at_1_5)} workers at dominance >= 1.5 across "
          f"{len({w['family'] for w in at_1_5})} families; "
          f"{sum(1 for w in workers if w['dominance'] >= 4)} at >= 4")
    return 0 if a0 else 1


if __name__ == "__main__":
    sys.exit(main())
