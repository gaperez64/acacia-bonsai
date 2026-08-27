#!/usr/bin/env python3
"""Evaluate Structural Gate T over a translation census.

Reads the TSV written by ``translation-census.py`` and compares, per worker
(one instance in one orientation), the counting core of ``B-native`` against
``S-current``.  The counting core is the number of automaton states that
receive numeric rank coordinates in Acacia, i.e. ``bool_threshold``, so it is
the dimension of the solver's downset vectors.

Gate T passes if either holds on the unbiased corpus:

  1. B-native core <= 80% of S-current on >= 20% of workers, covering >= 2
     families, with action signatures up by at most 25% on those workers; or
  2. B-native core <= 70% of S-current on >= 20 hard/timeout workers,
     covering >= 2 families.

Optionally cross-tabulates against a solved/failed verdict CSV so the gate can
be read against the instances that actually matter.
"""
from __future__ import annotations

import argparse
import collections
import csv
import pathlib
import re
import statistics
import sys

FORMS = ("S-current", "B-native", "G-native", "B-from-G", "S-from-G")


def family(name: str) -> str:
    """Collapse an instance name to its benchmark family."""
    stem = re.sub(r"\.ltl$", "", name)
    stem = re.sub(r"_pb_[0-9_]+_pe_$", "", stem)
    stem = re.sub(r"[0-9]+$", "", stem)
    return stem or "(unnamed)"


SCHEMA2 = (
    "schema_version name schedule orientation preference inputs outputs "
    "formula_nodes states edges acceptance_sets sccs state_acc deterministic "
    "complete universal hoa form local_core global_core accepting_sccs "
    "action_signatures"
).split()


def load(paths: list[pathlib.Path]) -> dict[tuple[str, str], dict[str, dict]]:
    """Read census rows, tolerating a file written without its header.

    ``acacia-automata-study`` suppressed the header whenever ``--hoa -`` was
    given, including in all-forms mode where no HOA reaches stdout.  That is
    fixed in the tool, but censuses collected before the fix are still valid
    data, so fall back to the known schema-2 column order.
    """
    workers: dict[tuple[str, str], dict[str, dict]] = collections.defaultdict(dict)
    for path in paths:
        lines = path.read_text().splitlines()
        if not lines:
            continue
        has_header = lines[0].split("\t")[0] == "schema_version"
        fields = lines[0].split("\t") if has_header else SCHEMA2
        body = lines[1:] if has_header else lines
        for line in body:
            cells = line.split("\t")
            if len(cells) != len(fields):
                continue  # TIMEOUT / ERROR marker rows
            row = dict(zip(fields, cells))
            if not row.get("form"):
                continue
            workers[(row["name"], row["orientation"])][row["form"]] = row
    return workers


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("census", nargs="+", help="census TSV(s) from translation-census.py")
    p.add_argument("--verdicts", action="append", default=[],
                   help="run-subset CSV with instance,result (repeatable)")
    args = p.parse_args()

    verdict = {}
    for path in args.verdicts:
        for row in csv.DictReader(open(path)):
            verdict[row["instance"]] = row["result"]
    solved = lambda name: verdict.get(name) in ("REALIZABLE", "UNREALIZABLE")

    workers = load([pathlib.Path(p) for p in args.census])
    complete = {k: v for k, v in workers.items() if {"S-current", "B-native"} <= v.keys()}
    print(f"workers with both S-current and B-native: {len(complete)} "
          f"(of {len(workers)} seen)\n")

    ratios, gained, gained_fams = [], [], set()
    action_blown = 0
    per_form_core = collections.defaultdict(list)
    for (name, orientation), forms in complete.items():
        s_core = int(forms["S-current"]["global_core"])
        b_core = int(forms["B-native"]["global_core"])
        s_act = int(forms["S-current"]["action_signatures"])
        b_act = int(forms["B-native"]["action_signatures"])
        for form in FORMS:
            if form in forms:
                per_form_core[form].append(int(forms[form]["global_core"]))
        if s_core == 0:
            continue
        ratio = b_core / s_core
        ratios.append(ratio)
        action_ratio = (b_act / s_act) if s_act else 1.0
        if ratio <= 0.80:
            if action_ratio <= 1.25:
                gained.append((name, orientation, s_core, b_core, ratio, action_ratio))
                gained_fams.add(family(name))
            else:
                action_blown += 1

    if not ratios:
        print("no comparable workers"); return 1

    print("B-native / S-current counting-core ratio")
    print(f"  workers compared : {len(ratios)}")
    print(f"  median           : {statistics.median(ratios):.3f}")
    print(f"  mean             : {statistics.fmean(ratios):.3f}")
    print(f"  <= 0.80          : {sum(r <= 0.80 for r in ratios)} "
          f"({100*sum(r <= 0.80 for r in ratios)/len(ratios):.1f}%)")
    print(f"  <= 0.70          : {sum(r <= 0.70 for r in ratios)}")
    print(f"  == 1.00          : {sum(abs(r-1) < 1e-9 for r in ratios)}")
    print(f"  >  1.00 (worse)  : {sum(r > 1 for r in ratios)}")
    print(f"  rejected for action blowup > 25%: {action_blown}\n")

    print("median counting core by form")
    for form in FORMS:
        if per_form_core[form]:
            print(f"  {form:<10} {statistics.median(per_form_core[form]):>8.1f}"
                  f"   (n={len(per_form_core[form])})")

    share = len(gained) / len(ratios)
    print(f"\nGATE T criterion 1: core <= 80% and actions <= +25%")
    print(f"  qualifying workers : {len(gained)} / {len(ratios)} = {100*share:.1f}%  "
          f"(need >= 20%)")
    print(f"  families covered   : {len(gained_fams)}  (need >= 2)")
    passed = share >= 0.20 and len(gained_fams) >= 2
    print(f"  => {'PASS' if passed else 'FAIL'}")

    if verdict:
        hard = [g for g in gained if not solved(g[0])]
        hard_fams = {family(g[0]) for g in hard}
        print(f"\ncross-tab against verdicts ({len(verdict)} known)")
        print(f"  qualifying workers on instances Acacia FAILS: {len(hard)}"
              f"  families {len(hard_fams)}  (criterion 2 needs >= 20 and >= 2)")

    print("\nlargest core reductions:")
    for name, orientation, s, b, r, a in sorted(gained, key=lambda g: g[4])[:12]:
        mark = "" if not verdict else ("  [solved]" if solved(name) else "  [FAILS]")
        print(f"  {r:5.2f}  {name:<40} {orientation:<16} {s:>5} -> {b:<5} "
              f"actions x{a:.2f}{mark}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
