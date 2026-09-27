#!/usr/bin/env python3
"""Deterministic outcome/time/structure/resource P1 development and held-out panels.

Input paths are explicit so archived observations need not be stored in Git.
No benchmark name, family, or source text is inspected for classification.
Instance IDs are used only as opaque keys and for seeded tie breaking.
"""
import argparse
import csv
import hashlib
from pathlib import Path


def read(path, key):
    with path.open(newline="") as stream:
        return {row[key]: row for row in csv.DictReader(stream, delimiter="\t")}


def write(path, rows, header_note=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        if header_note:
            stream.write("# Selection rule: memory_heavy uses archived process RSS "
                         "(a resource outcome); lift_decline_probe uses elapsed-time strata.\n")
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("instance", "category", "selection_rule"))
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legs", type=Path, required=True)
    parser.add_argument("--census", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--heldout", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=19427)
    parser.add_argument("--heldout-seed", type=int, default=73013)
    args = parser.parse_args()
    arms = {n: read(args.legs / f"arm-{n}" / f"arm-{n}-cap60.tsv", "instance")
            for n in range(1, 8)}
    census = read(args.census, "id")
    universe = set(census).intersection(*(set(rows) for rows in arms.values()))
    picked = {}

    def order(ids, seed):
        return sorted(ids, key=lambda item: hashlib.sha256(
            f"{seed}:{item}".encode()).digest())

    def add(category, rule, ids, count=None, seed=None, destination=picked):
        candidates = order(set(ids) - set(destination), args.seed if seed is None else seed)
        for item in candidates[:count]:
            destination[item] = (category, rule)

    # All verified lift successes are retained, including the one whose result
    # is absent from the other six arms. No identity-specific rule is needed.
    add("lift_real", "arm7_REAL_all", (i for i in universe if arms[7][i]["result"] == "REALIZABLE"))
    # Select two from each direct-success wall-time decade, where available.
    for lo, hi in ((0, .1), (.1, 1), (1, 10), (10, 60)):
        add("direct_real", f"arm5_REAL_{lo:g}_to_{hi:g}s_seed{args.seed}",
            (i for i in universe if arms[5][i]["result"] == "REALIZABLE"
             and lo <= float(arms[5][i]["seconds"]) < hi), 2)
    add("opposite_unreal", f"census_yes_arm6_UNREAL_seed{args.seed}",
        (i for i in universe if census[i]["reducible"] == "yes"
         and arms[6][i]["result"] == "UNREALIZABLE"), 6)
    add("unsupported", f"census_not_reducible_seed{args.seed}",
        (i for i in universe if census[i]["reducible"] != "yes"), 6)
    # Historical lift stderr did not retain a per-instance decline stage.
    # Sample its elapsed-time strata; the opt-in records classify them later.
    for lo, hi in ((0, .1), (.1, 1), (1, 10), (10, 60)):
        add("lift_decline_probe", f"arm7_UNKNOWN_{lo:g}_to_{hi:g}s_seed{args.seed}",
            (i for i in universe if arms[7][i]["result"] == "UNKNOWN"
             and lo <= float(arms[7][i]["seconds"]) < hi), 2)
    # The archived scope-peak column is empty. High observed process RSS and
    # cap exhaustion are separate selection rules, not invented scope peaks.
    heavy = sorted((i for i in universe if max(int(arms[n][i]["max_process_rss_bytes"] or 0)
                                                for n in (5, 7)) >= 1 << 30),
                   key=lambda i: -max(int(arms[n][i]["max_process_rss_bytes"] or 0)
                                      for n in (5, 7)))
    add("memory_heavy", "arm5_or_7_process_RSS_ge_1GiB_top6", heavy[:6])
    add("near_cap_probe", f"arm5_or_7_TIMEOUT_seed{args.seed}",
        (i for i in universe if any(arms[n][i]["result"] in ("TIMEOUT", "MEMOUT")
                                     for n in (5, 7))), 4)

    held = {}
    groups = (
        ("direct_real", (i for i in universe if arms[5][i]["result"] == "REALIZABLE")),
        ("opposite_unreal", (i for i in universe if census[i]["reducible"] == "yes"
                              and arms[6][i]["result"] == "UNREALIZABLE")),
        ("unsupported", (i for i in universe if census[i]["reducible"] != "yes")),
        ("lift_decline_probe", (i for i in universe if arms[7][i]["result"] == "UNKNOWN")),
        ("near_cap_probe", (i for i in universe if any(
            arms[n][i]["result"] in ("TIMEOUT", "MEMOUT") for n in (5, 7)))),
    )
    for category, ids in groups:
        add(category, f"heldout_{category}_seed{args.heldout_seed}",
            set(ids) - set(picked), 4, args.heldout_seed, held)
    write(args.output, sorted((i, *value) for i, value in picked.items()), True)
    write(args.heldout, sorted((i, *value) for i, value in held.items()))
    print(f"development={len(picked)} heldout={len(held)}")


if __name__ == "__main__":
    main()
