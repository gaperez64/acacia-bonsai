#!/usr/bin/env python3
"""Tables A-D of an isolated arm census.

Reads the *-summary.tsv files run-syntcomp26-coverage.py writes per arm and
reports what the census exists to answer: how much of the coverage is reachable
only by some arms, and whether a fixed subset leaves anything on the table.

Every union here is an *isolated* oracle: the arms were run one at a time, so a
union is an upper bound on what a concurrent race could reach, never a race
result.  Racing changes contention and can change which arm answers first, or
whether one answers at all.

Answers are counted only if they were first decided at or below --cap, for the
reason select-portfolio-arms.py documents: decisive_result carries the answer
from the earliest staged cap that decided the instance.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import pathlib
import sys

DECISIVE = {"REALIZABLE", "UNREALIZABLE"}


def load(path: pathlib.Path, cap: float) -> dict[str, tuple[str, float]]:
    answers: dict[str, tuple[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if row["decisive_result"] not in DECISIVE:
                continue
            solved_at = row["smallest_cap_solved"]
            if solved_at == "" or float(solved_at) > cap:
                continue
            answers[row["instance"]] = (
                row["decisive_result"], float(row["decisive_seconds"]))
    return answers


def par2(answers, instances, cap):
    """PAR-2: solved instances cost their time, unsolved cost twice the cap."""
    total = 0.0
    for name in instances:
        got = answers.get(name)
        total += got[1] if got else 2 * cap
    return total


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--census-dir", required=True, type=pathlib.Path)
    ap.add_argument("--cap", type=float, default=17.0)
    ap.add_argument("--max-k", type=int, default=4)
    ap.add_argument("--shipping", default="",
                    help="comma-separated arm labels forming the current shipping union")
    args = ap.parse_args()

    arms = {}
    for path in sorted(args.census_dir.glob("*-summary.tsv")):
        arms[path.name.removesuffix("-summary.tsv")] = load(path, args.cap)
    if not arms:
        print(f"no *-summary.tsv under {args.census_dir}", file=sys.stderr)
        return 1

    instances = sorted({i for a in arms.values() for i in a})

    # A verdict conflict is a soundness bug, never portfolio data.
    conflicts = []
    for name in instances:
        seen = {a: arms[a][name][0] for a in arms if name in arms[a]}
        if len(set(seen.values())) > 1:
            conflicts.append(f"  {name}: " + ", ".join(f"{k}={v}" for k, v in sorted(seen.items())))
    if conflicts:
        print(f"FATAL: {len(conflicts)} verdict conflict(s); this is a soundness bug, "
              f"not a ranking input.", file=sys.stderr)
        print("\n".join(conflicts), file=sys.stderr)
        return 1

    print(f"# Arm census: {len(arms)} arms, {len(instances)} instances answered by "
          f"at least one arm, cap {args.cap:g}s\n")

    print("## Table A - individual arms\n")
    print("| arm | solved | REAL | UNREAL | PAR-2 | median solved |")
    print("|---|---:|---:|---:|---:|---:|")
    for name in sorted(arms, key=lambda n: -len(arms[n])):
        a = arms[name]
        real = sum(1 for r, _ in a.values() if r == "REALIZABLE")
        times = sorted(s for _, s in a.values())
        med = times[len(times) // 2] if times else float("nan")
        print(f"| `{name}` | {len(a)} | {real} | {len(a)-real} | "
              f"{par2(a, instances, args.cap):.1f} | {med:.2f} |")

    print("\n## Table B - unique and marginal contribution\n")
    ship = [s for s in args.shipping.split(",") if s]
    ship_union = {i for s in ship for i in arms.get(s, {})}
    print("| arm | unique to it | not in shipping union |")
    print("|---|---:|---:|")
    for name in sorted(arms, key=lambda n: -len(arms[n])):
        others = {i for o in arms if o != name for i in arms[o]}
        unique = set(arms[name]) - others
        beyond = set(arms[name]) - ship_union if ship else set()
        print(f"| `{name}` | {len(unique)} | {len(beyond) if ship else '-'} |")

    print("\n## Table D - virtual best by subset size (isolated union)\n")
    print("| k | best fixed subset | isolated union |")
    print("|---:|---|---:|")
    names = sorted(arms)
    everything = len({i for a in arms.values() for i in a})
    for k in range(1, min(args.max_k, len(names)) + 1):
        best, best_union = None, -1
        for subset in itertools.combinations(names, k):
            union = len({i for s in subset for i in arms[s]})
            if union > best_union:
                best, best_union = subset, union
        print(f"| {k} | {' + '.join('`'+s+'`' for s in best)} | {best_union} |")
    print(f"| all {len(names)} | (oracle ceiling) | **{everything}** |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
