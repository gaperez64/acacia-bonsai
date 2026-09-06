#!/usr/bin/env python3
"""Compare two full-corpus campaigns per instance, regressions first.

Totals hide the thing that decides a backend switch.  A configuration that gains
twelve and loses eleven nets +1 and looks like a wash; one that gains one and
loses nothing is a different proposition entirely, because the losses are what a
default flip would inflict on users while the gains are what it would buy.

This reports, in order:
  1. instances the candidate LOSES that the baseline decides   <- the blocker
  2. instances the candidate GAINS
  3. verdict disagreements on instances both decide            <- correctness
  4. net and totals

The ordering is deliberate: with the forward backend running on a flat vector and
a linear subsumption scan while the backward backend runs on a tuned posets
downset, a small margin either way says less than the absence of regressions does.
The data-structure headroom is larger than the current margin, so "does it lose
anything" is the question that should gate the decision.
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys

DECISIVE = {"REALIZABLE", "UNREALIZABLE"}


def load(path: pathlib.Path) -> dict[str, tuple[str, float]]:
    out: dict[str, tuple[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if row["decisive_result"] in DECISIVE:
                out[row["instance"]] = (row["decisive_result"],
                                        float(row["decisive_seconds"] or 0.0))
    return out


def family(instance: str) -> str:
    stem = instance.removesuffix(".ltl")
    marker = stem.find("_pb_")
    return stem[:marker] if marker != -1 else stem


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--candidate-label", default="candidate")
    args = parser.parse_args()

    base = load(args.baseline)
    cand = load(args.candidate)

    lost = sorted(set(base) - set(cand))
    gained = sorted(set(cand) - set(base))
    both = set(base) & set(cand)
    disagree = sorted(i for i in both if base[i][0] != cand[i][0])

    print(f"{args.baseline_label}: {len(base)} decisive")
    print(f"{args.candidate_label}: {len(cand)} decisive")
    print(f"net: {len(cand) - len(base):+d}\n")

    print(f"=== VERDICT DISAGREEMENTS (must be 0): {len(disagree)} ===")
    for i in disagree:
        print(f"  {i}: {args.baseline_label}={base[i][0]} {args.candidate_label}={cand[i][0]}")

    print(f"\n=== LOST by {args.candidate_label}: {len(lost)} ===")
    fams: dict[str, int] = {}
    for i in lost:
        fams[family(i)] = fams.get(family(i), 0) + 1
    for name, count in sorted(fams.items(), key=lambda kv: -kv[1]):
        print(f"  {name:<34} {count}")

    print(f"\n=== GAINED by {args.candidate_label}: {len(gained)} ===")
    fams = {}
    for i in gained:
        fams[family(i)] = fams.get(family(i), 0) + 1
    for name, count in sorted(fams.items(), key=lambda kv: -kv[1]):
        print(f"  {name:<34} {count}")

    if both:
        faster = sum(1 for i in both if cand[i][1] < base[i][1])
        print(f"\non the {len(both)} both decide: {args.candidate_label} faster on {faster}, "
              f"slower on {len(both) - faster}")
    return 1 if disagree else 0


if __name__ == "__main__":
    sys.exit(main())
