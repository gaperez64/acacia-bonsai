"""Unit tests for the closure-buchi-provider mechanism audit's job selection
(plan section 4.3 item 1). Pure logic only -- no filesystem, no solver."""

import importlib.util
import json
import sys
from pathlib import Path

import pytest


SCRIPT = (Path(__file__).resolve().parents[2] / "benchmarking" / "witness-lifting-20260918" /
          "opening" / "select-provider-audit-jobs.py")
SPEC = importlib.util.spec_from_file_location("select_provider_audit_jobs", SCRIPT)
selector = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = selector
SPEC.loader.exec_module(selector)


def row(instance, family, n, confidence="exact"):
    return {
        "logical_instance": instance,
        "family_key": family,
        "parameter_confidence": confidence,
        "parameter_values_json": json.dumps({"n": n}),
    }


def always_unknown(_tlsf_file):
    return None


def test_excludes_families_already_touched_by_p2_or_p4():
    rows = [
        row("used_pb_2.ltl", "fam_used", 2),
        row("used_pb_5.ltl", "fam_used", 5),
        row("new_pb_2.ltl", "fam_new", 2),
    ]
    baseline = {"used_pb_2.ltl": "TIMEOUT", "used_pb_5.ltl": "TIMEOUT", "new_pb_2.ltl": "TIMEOUT"}
    tlsf_sources = {r["logical_instance"]: r["logical_instance"].replace(".ltl", ".tlsf") for r in rows}
    already_used_ids = {"used_pb_2.ltl"}  # only one member of fam_used is "already used"

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, already_used_ids, always_unknown)
    families = {j["family_key"] for j in jobs}
    assert families == {"fam_new"}  # the whole family is excluded, not just the cited instance


def test_excludes_solved_instances():
    rows = [row("solved.ltl", "fam", 2), row("unsolved.ltl", "fam", 3)]
    baseline = {"solved.ltl": "REALIZABLE", "unsolved.ltl": "TIMEOUT"}
    tlsf_sources = {"solved.ltl": "solved.tlsf", "unsolved.ltl": "unsolved.tlsf"}

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown)
    assert {j["logical_instance"] for j in jobs} == {"unsolved.ltl"}


def test_excludes_missing_baseline_entries():
    # An instance the frozen baseline never covered is not "previously
    # unsolved" -- it is unknown, and must not be silently included.
    rows = [row("no_baseline.ltl", "fam", 2)]
    jobs = selector.select_jobs(rows, {}, {"no_baseline.ltl": "x.tlsf"}, set(), always_unknown)
    assert jobs == []


def test_excludes_heuristic_parameter_confidence():
    rows = [row("heuristic.ltl", "fam", 2, confidence="heuristic")]
    baseline = {"heuristic.ltl": "TIMEOUT"}
    jobs = selector.select_jobs(rows, baseline, {"heuristic.ltl": "x.tlsf"}, set(), always_unknown)
    assert jobs == []


def test_at_most_two_points_per_family_smallest_and_largest():
    rows = [row(f"fam_pb_{n}.ltl", "fam", n) for n in (2, 5, 10, 20)]
    baseline = {r["logical_instance"]: "TIMEOUT" for r in rows}
    tlsf_sources = {r["logical_instance"]: "x.tlsf" for r in rows}

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown)
    picked_ns = sorted(json.loads(j["parameter_values_json"])["n"] for j in jobs
                       if j["orientation"] == "real")
    assert picked_ns == [2, 20]  # smallest and largest, not the middle ones


def test_family_with_one_unsolved_instance_gets_exactly_that_one():
    rows = [row("only.ltl", "fam", 7)]
    baseline = {"only.ltl": "TIMEOUT"}
    jobs = selector.select_jobs(rows, baseline, {"only.ltl": "x.tlsf"}, set(), always_unknown)
    assert {j["logical_instance"] for j in jobs} == {"only.ltl"}


def test_orientation_uses_trusted_status_when_available():
    rows = [row("real_one.ltl", "fam_r", 2), row("unreal_one.ltl", "fam_u", 2)]
    baseline = {"real_one.ltl": "TIMEOUT", "unreal_one.ltl": "TIMEOUT"}
    tlsf_sources = {"real_one.ltl": "real.tlsf", "unreal_one.ltl": "unreal.tlsf"}

    def verdict(tlsf_file):
        return {"real.tlsf": "REALIZABLE", "unreal.tlsf": "UNREALIZABLE"}[tlsf_file]

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, set(), verdict)
    by_instance = {j["logical_instance"]: j for j in jobs}
    assert len(jobs) == 2  # one job each, not two
    assert by_instance["real_one.ltl"]["orientation"] == "real"
    assert by_instance["unreal_one.ltl"]["orientation"] == "unreal"


def test_orientation_retains_both_when_no_trusted_status():
    rows = [row("unknown_one.ltl", "fam", 2)]
    baseline = {"unknown_one.ltl": "TIMEOUT"}
    jobs = selector.select_jobs(rows, baseline, {"unknown_one.ltl": "x.tlsf"}, set(), always_unknown)
    assert {j["orientation"] for j in jobs} == {"real", "unreal"}
    assert len(jobs) == 2
    assert all(j["logical_instance"] == "unknown_one.ltl" for j in jobs)
    assert all(j["trusted_status"] == "unknown" for j in jobs)


def test_never_exceeds_the_job_budget():
    rows = [row(f"fam{i}_pb_{n}.ltl", f"fam{i}", n) for i in range(40) for n in (2, 3)]
    baseline = {r["logical_instance"]: "TIMEOUT" for r in rows}
    tlsf_sources = {r["logical_instance"]: "x.tlsf" for r in rows}

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown, budget=64)
    assert len(jobs) == 64


def test_breadth_first_never_gives_a_later_family_more_than_an_earlier_one():
    # Sorted-order families are processed breadth-first: every family's
    # first point is offered before any family's second point. Under a
    # tight budget that cannot even cover one point per family (here: 40
    # families x 2 unknown-orientation jobs each > 64), later families in
    # sort order legitimately get zero -- that is budget exhaustion mid-pass,
    # not a bug. What must hold is monotonicity: no family gets more points
    # than a family that sorts before it.
    rows = [row(f"fam{i:02d}_pb_{n}.ltl", f"fam{i:02d}", n) for i in range(40) for n in (2, 3)]
    baseline = {r["logical_instance"]: "TIMEOUT" for r in rows}
    tlsf_sources = {r["logical_instance"]: "x.tlsf" for r in rows}

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown, budget=64)
    per_family = {}
    for j in jobs:
        per_family.setdefault(j["family_key"], set()).add(j["logical_instance"])
    counts = [len(per_family.get(f"fam{i:02d}", set())) for i in range(40)]
    for earlier, later in zip(counts, counts[1:]):
        assert later <= earlier, f"counts {counts} violate breadth-first monotonicity"
    # and budget exhaustion is exact: 32 families x 1 point x 2 jobs = 64
    assert sum(counts) == 32
    assert counts.count(1) == 32
    assert counts.count(0) == 8


def test_budget_boundary_never_admits_a_partial_job_pair():
    # A pending instance whose two orientation jobs would overflow the
    # remaining budget must be skipped entirely, not truncated to one job.
    rows = [row(f"fam{i}_pb_2.ltl", f"fam{i}", 2) for i in range(33)]  # 33 unknown-orientation singles
    baseline = {r["logical_instance"]: "TIMEOUT" for r in rows}
    tlsf_sources = {r["logical_instance"]: "x.tlsf" for r in rows}

    jobs = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown, budget=64)
    # 32 instances x 2 jobs = 64 exactly; the 33rd instance's pair (2 jobs)
    # would make 66 > 64, so it must be entirely excluded, not half-included.
    instances = {j["logical_instance"] for j in jobs}
    assert len(jobs) == 64
    assert len(instances) == 32
    for iid in instances:
        assert sum(1 for j in jobs if j["logical_instance"] == iid) == 2


def test_deterministic_across_runs():
    rows = [row(f"fam{i}_pb_{n}.ltl", f"fam{i}", n) for i in range(10) for n in (2, 3, 5)]
    baseline = {r["logical_instance"]: "TIMEOUT" for r in rows}
    tlsf_sources = {r["logical_instance"]: "x.tlsf" for r in rows}

    jobs1 = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown)
    jobs2 = selector.select_jobs(rows, baseline, tlsf_sources, set(), always_unknown)
    assert jobs1 == jobs2


@pytest.mark.parametrize("values,expected", [
    ({"n": 5}, 5),
    ({"N": 2, "M": 4}, 2),  # first declared parameter, for multi-parameter families
    ({}, 0),
])
def test_parameter_value_extraction(values, expected):
    assert selector.parameter_value({"parameter_values_json": json.dumps(values)}) == expected
