#!/usr/bin/env python3
"""Offline P6 TSV arithmetic and exact fixed-K comparison; never translates.

Run each arm with acacia-spot-provider-replay in a separate invocation, then:
  python benchmarking/spot-provider-replay.py c4.tsv c5.tsv
No graph/rank IDs or format_state strings participate in this comparison.
Language equivalence to another construction does not establish a fixed-K match.
"""

import argparse
import csv
import json
from pathlib import Path


# Exact construction/options and worker boundary, not just formula language.
CONSTRUCTION = (
    "worker_formula", "provider", "spot_version", "refined_rules", "wrapper",
    "initial_convention", "rank_domain", "partition", "ap_order", "k",
    "kmin", "kmax", "kinc", "k_schedule", "caps",
)


def count(value):
    """Unknown counters stay unknown; zero is a measured zero."""
    if value is None or value in ("NA", "unknown", "censored", ""):
        return None
    result = int(value)
    if result < 0 or str(result) != str(value):
        raise ValueError(f"invalid count: {value!r}")
    return result


def row_accounting(eager, after_search, after_verification):
    """Monotone complete-row snapshots, including eager work in the union."""
    values = [count(v) for v in (eager, after_search, after_verification)]
    if any(v is None for v in values):
        return dict(eager=values[0], search=None, verification_additional=None, union=None)
    before, search, verified = values
    if not before <= search <= verified:
        raise ValueError("complete row counts must be monotone")
    return dict(eager=before, search=search - before,
                verification_additional=verified - search, union=verified)


def request_accounting(search_sources, verification_sources):
    """Set arithmetic is for identifiers within ONE provider arena only."""
    search, verify = set(search_sources), set(verification_sources)
    return dict(search=len(search), search_only=len(search - verify),
                verification=len(verify), verification_additional=len(verify - search),
                union=len(search | verify))


def utilization(requested, total, total_status):
    """A partial/censored eager traversal is never a denominator."""
    requested = count(requested)
    denominator = count(total) if total_status == "complete" else None
    if requested is not None and denominator is not None and requested > denominator:
        raise ValueError("requested rows exceed the complete same-construction total")
    return dict(requested=requested, total=denominator,
                ratio=(requested / denominator
                       if requested is not None and denominator else None))


def equivalence(c4, c5):
    """Compare only certified fixed-K outcomes of the identical construction.

    This intentionally makes no claims about rank regions or strategy sizes.
    Local integer IDs may differ. No state correspondence is needed to compare
    the initial outcome once the complete construction key is identical.
    """
    if c4.get("arm") != "c4" or c5.get("arm") != "c5":
        raise ValueError("expected one c4 record and one c5 record")
    for key in CONSTRUCTION:
        if key not in c4 or key not in c5 or c4[key] in (None, "NA") or c5[key] in (None, "NA"):
            return "incomparable"
        if c4[key] != c5[key]:
            return "incomparable"
    if c4.get("worker_pid") == c5.get("worker_pid") and c4.get("worker_pid") not in (None, "NA"):
        # Conservatively reject PID reuse too; it is not evidence of independence.
        return "incomparable"
    exact = {"WIN_K", "LOSE_K"}
    if any(row.get("status") not in exact or row.get("certificate") != "verified"
           for row in (c4, c5)):
        return "inconclusive"
    return "agree" if c4["status"] == c5["status"] else "disagree"


def summarize(c4, c5):
    comparison = equivalence(c4, c5)
    # Inconclusive games can still have a COMPLETE eager automaton total, but
    # mismatched construction/options cannot provide a denominator at all.
    total_status = c4.get("total_status") if comparison != "incomparable" else "unknown"
    generated = row_accounting(c5.get("eager_rows_generated"),
                               _sum(c5.get("eager_rows_generated"),
                                    c5.get("search_rows_generated_cumulative")),
                               c5.get("wrapper_rows_generated"))
    return dict(equivalence=comparison, c5_generation=generated,
                wrapper_utilization=utilization(c5.get("wrapper_rows_generated"),
                                                c4.get("total_wrapper_rows"), total_status),
                underlying_utilization=utilization(c5.get("underlying_rows_generated"),
                                                   c4.get("total_underlying_rows"), total_status))


def _sum(a, b):
    a, b = count(a), count(b)
    return None if a is None or b is None else a + b


def read_rows(path):
    with Path(path).open(newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("c4")
    parser.add_argument("c5")
    args = parser.parse_args()
    eager, lazy = read_rows(args.c4), read_rows(args.c5)
    indexed = {}
    for row in eager:
        key = tuple(row.get(k) for k in CONSTRUCTION)
        if key in indexed:
            parser.error("duplicate C4 construction/K record; compare repetitions separately")
        indexed[key] = row
    failed = False
    for row in lazy:
        match = indexed.get(tuple(row.get(k) for k in CONSTRUCTION))
        result = summarize(match, row) if match else {"equivalence": "incomparable"}
        print(json.dumps(dict(formula=row.get("worker_formula"), k=row.get("k"), **result), sort_keys=True))
        failed |= result["equivalence"] != "agree"
    return 2 if failed or not lazy else 0


if __name__ == "__main__":
    raise SystemExit(main())
