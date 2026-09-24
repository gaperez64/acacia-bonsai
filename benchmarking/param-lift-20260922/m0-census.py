#!/usr/bin/env python3
"""M0 census: which parametric families admit an exact per-conjunct GR(1) reduction.

For each family in m0-instances.tsv, lower its smallest instance with tlsf2ltl, split
the objective `A -> G` into top-level conjuncts (distributing G over conjunctions),
and record the Manna-Pnueli class of every conjunct (spot.mp_class). A family is
`dba_reducible` when every conjunct on both sides is at most recurrence class, i.e.
each conjunct has a deterministic Buchi monitor, so the objective is exactly a GR(1)
condition over the product of the game with one small monitor per conjunct.
The route-stats columns summarize tlsfcompose's current routing (m0-route-stats.tsv).

Needs Spot's Python bindings (python3.13 with PYTHONPATH=/usr/local/lib64/python3.13/site-packages).
"""
import collections
import csv
import json
import re
import subprocess
from pathlib import Path

import spot

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TLSF2LTL = ROOT / "subprojects" / "tlsf-tools" / "build_nospot" / "tlsf2ltl"
ORDER = "BGSORPT"


def conjuncts(f):
    if f.kind() == spot.op_And:
        for c in f:
            yield from conjuncts(c)
    elif f.kind() == spot.op_G and f[0].kind() == spot.op_And:
        for c in f[0]:
            yield from conjuncts(spot.formula.G(c))
    elif not f.is_tt():
        yield f


def index_template(f):
    return re.sub(r"_\d+", "_i", str(f))


def main():
    instances = list(csv.DictReader(open(HERE / "m0-instances.tsv"), delimiter="\t"))
    routes = collections.defaultdict(collections.Counter)
    for row in csv.DictReader(open(HERE / "m0-route-stats.tsv"), delimiter="\t"):
        routes[row["file"]][row["route"]] += 1

    by_family = collections.defaultdict(list)
    for row in instances:
        by_family[row["family_display"]].append(row)

    columns = ["family", "instances", "unsolved_120s", "route_mix", "smallest_instance",
               "smallest_parameters", "assume_classes", "guarantee_classes",
               "max_assume", "max_guarantee", "dba_reducible", "non_recurrence_templates"]
    with open(HERE / "m0-census.tsv", "w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for family, rows in sorted(by_family.items()):
            def size(row):
                values = json.loads(row["parameters"])
                return sum(values.values()) if values else 0

            smallest = min(rows, key=size)
            lowered = subprocess.run([str(TLSF2LTL), smallest["tlsf"]], cwd=ROOT,
                                     capture_output=True, text=True, check=True).stdout
            formula = spot.formula(lowered.strip())
            if formula.kind() == spot.op_Implies:
                assume, guarantee = formula[0], formula[1]
            else:
                assume, guarantee = spot.formula.tt(), formula
            classes = {}
            non_recurrence = []
            for side, part in (("A", assume), ("G", guarantee)):
                counter = collections.Counter()
                for c in conjuncts(part):
                    cls = spot.mp_class(c)
                    counter[cls] += 1
                    if cls in "PT":
                        non_recurrence.append(f"{side}: {index_template(c)}")
                classes[side] = counter
            route_mix = collections.Counter()
            for row in rows:
                route_mix.update(routes[row["tlsf"]].keys())
            max_a = max(classes["A"], key=ORDER.index, default="-")
            max_g = max(classes["G"], key=ORDER.index, default="-")
            writer.writerow({
                "family": family,
                "instances": len(rows),
                "unsolved_120s": sum(r["status_120s"] == "unsolved" for r in rows),
                "route_mix": ",".join(f"{k}:{v}" for k, v in sorted(route_mix.items())),
                "smallest_instance": smallest["logical_instance"],
                "smallest_parameters": smallest["parameters"],
                "assume_classes": json.dumps(dict(sorted(classes["A"].items()))),
                "guarantee_classes": json.dumps(dict(sorted(classes["G"].items()))),
                "max_assume": max_a,
                "max_guarantee": max_g,
                "dba_reducible": str(not non_recurrence).lower(),
                "non_recurrence_templates": " | ".join(dict.fromkeys(non_recurrence))[:400],
            })


if __name__ == "__main__":
    main()
