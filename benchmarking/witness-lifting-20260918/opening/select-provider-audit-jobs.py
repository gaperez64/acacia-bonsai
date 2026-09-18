#!/usr/bin/env python3
"""Freeze the closure-buchi-provider mechanism audit's job selection
(plan section 4.3, item 1). Selection only -- runs no solver and inspects
no provider behavior; that happens later, in a separate replay pass, on
this frozen list.

Rule, exactly as specified:
  - Reuse the existing 22 P2 targets (untouched; this script adds to them,
    never replaces them).
  - Add at most 64 NEW jobs from previously unsolved (old 17s baseline),
    exact-parametric families not already touched by P2 or P4.
  - Limit to two parameter points per family (smallest and largest unsolved
    parameter value, for a size spread) before adding more families:
    implemented as a breadth-first pass -- one point for every eligible
    family first, then a second point for every family that still has
    budget, so a tight job budget sacrifices per-family depth before
    family coverage.
  - Orientation: prefer the corpus's own trusted STATUS verdict, read via
    run-syntcomp26-coverage.py's expected_verdict() (the same mechanism the
    coverage runner itself uses for conflict detection) -- never guessed
    from a filename. When no trusted verdict exists, both orientations are
    retained as separately identified jobs, exactly as instructed, which is
    why the job count exceeds the instance count.

"Previously unsolved" is read from the demand-sparse sprint's frozen 17s
baseline (benchmarking/demand-sparse-20260916/closing/acacia-baseline.csv);
labelled explicitly in the output so nobody mistakes it for fresh W1 data.
"""
import collections
import csv
import importlib.util
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
BUDGET = 64


def load_coverage_module():
    spec = importlib.util.spec_from_file_location(
        "coverage_for_audit_selection", ROOT / "benchmarking" / "run-syntcomp26-coverage.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parameter_value(row: dict) -> object:
    values = json.loads(row["parameter_values_json"])
    return next(iter(values.values())) if values else 0


def select_jobs(rows, baseline, tlsf_sources, already_used_ids, expected_verdict_fn, budget=BUDGET):
    """Pure selection logic (plan section 4.3 item 1), no I/O.

    rows: family-instances.tsv rows (dicts with logical_instance, family_key,
      parameter_confidence, parameter_values_json).
    baseline: {instance: result} from the frozen 17s baseline CSV.
    tlsf_sources: {instance: tlsf_file}.
    already_used_ids: P2 | P4 instance IDs, excluded by family.
    expected_verdict_fn: tlsf_file -> "REALIZABLE" | "UNREALIZABLE" | None,
      e.g. a partial application of run-syntcomp26-coverage.py's own
      expected_verdict() bound to the corpus directory.

    Returns the frozen job list, one dict per job (an unsolved instance may
    contribute one job, when a trusted verdict picks its orientation, or two,
    one per orientation, when it does not).
    """
    by_id = {r["logical_instance"]: r for r in rows}
    used_families = {by_id[iid]["family_key"] for iid in already_used_ids if iid in by_id}
    solved_statuses = {"REALIZABLE", "UNREALIZABLE"}

    candidates = collections.defaultdict(list)
    for row in rows:
        iid = row["logical_instance"]
        if row["parameter_confidence"] != "exact" or row["family_key"] in used_families:
            continue
        result = baseline.get(iid)
        if result in solved_statuses or result is None:
            continue
        candidates[row["family_key"]].append(row)

    ranked = collections.defaultdict(list)
    for family_key in sorted(candidates):
        members = sorted(candidates[family_key], key=parameter_value)
        picks = members if len(members) <= 2 else [members[0], members[-1]]
        for rank, row in enumerate(picks):
            ranked[rank].append((family_key, row))
    ordered_instances = ranked[0] + ranked[1]

    jobs = []
    for family_key, row in ordered_instances:
        iid = row["logical_instance"]
        expected = expected_verdict_fn(tlsf_sources[iid])
        if expected == "REALIZABLE":
            orientations = ["real"]
        elif expected == "UNREALIZABLE":
            orientations = ["unreal"]
        else:
            orientations = ["real", "unreal"]
        if len(jobs) + len(orientations) > budget:
            continue
        for orientation in orientations:
            jobs.append({
                "family_key": family_key,
                "logical_instance": iid,
                "orientation": orientation,
                "trusted_status": expected or "unknown",
                "baseline_17s_result": baseline[iid],
                "parameter_values_json": row["parameter_values_json"],
            })
    return jobs


def main() -> int:
    coverage = load_coverage_module()

    p2_ids = set((ROOT / "benchmarking/symbolic-rows-20260917/targets/p2.list").read_text().split())
    p4_ids = set((ROOT / "benchmarking/symbolic-rows-20260917/targets/p4.list").read_text().split())
    already_used_ids = p2_ids | p4_ids

    with open(ROOT / "benchmarking/syntcomp26-family-instances.tsv", newline="") as fh:
        rows = list(csv.DictReader(fh, delimiter="\t"))

    with open(ROOT / "benchmarking/demand-sparse-20260916/closing/acacia-baseline.csv") as fh:
        baseline = {r["instance"]: r["result"] for r in csv.DictReader(fh)}

    with open(ROOT / "tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv", newline="") as fh:
        tlsf_sources = {r["instance"]: r["tlsf"] for r in csv.DictReader(fh, delimiter="\t")}

    corpus = ROOT / "tlsf-corpus"

    def expected_verdict_fn(tlsf_file):
        return coverage.expected_verdict(corpus / tlsf_file)

    jobs = select_jobs(rows, baseline, tlsf_sources, already_used_ids, expected_verdict_fn)

    out_path = ROOT / "benchmarking/witness-lifting-20260918/opening/provider-audit-jobs.tsv"
    columns = ["family_key", "logical_instance", "orientation", "trusted_status",
               "baseline_17s_result", "parameter_values_json"]
    with out_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(jobs)

    instances = {(j["family_key"], j["logical_instance"]) for j in jobs}
    families = {f for f, _ in instances}
    print(f"wrote {out_path}")
    print(f"  jobs: {len(jobs)} (budget {BUDGET})")
    print(f"  instances: {len(instances)}")
    print(f"  families: {len(families)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
