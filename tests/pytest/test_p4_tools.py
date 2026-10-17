"""Synthetic P4 recycle, join, lifting, and campaign-manifest checks."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import os
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


coverage = load("p4_coverage", "benchmarking/run-syntcomp26-coverage.py")
recycle = load("p4_recycle", "benchmarking/tools/recycle-cap.py")
threeway = load("p4_threeway", "benchmarking/tools/threeway-join.py")
lifting = load("p4_lifting", "benchmarking/tools/lifting-report.py")
capcheck = load("p4_capcheck", "benchmarking/tools/check-cap-independence.py")


def observation(name, result, *, cap=17, seconds="1", label="candidate", exit_code=None):
    exits = {"REALIZABLE": "0", "UNREALIZABLE": "1", "UNKNOWN": "2",
             "TIMEOUT": "-15", "MEMOUT": "-9", "ERROR": "2", "CRASH": "-11",
             "SYFCO-FAIL": "-1"}
    return {**dict.fromkeys(coverage.OUTPUT_COLUMNS, ""),
            "solver_label": label, "instance": name, "tlsf_file": f"{name}.tlsf",
            "cap_s": str(cap), "result": result, "seconds": str(seconds),
            "exit_code": exit_code or exits[result],
            "timed_out": str(result == "TIMEOUT").lower(),
            "resource_reason": "", "expectation_source": "none", "run_index": "0",
            "acacia_sha": "source", "binary_sha256": "a" * 64,
            "preset": "preset", "flags": "--arms synthetic", "memory_max": "8G",
            "memory_swap_max": "0", "collect_rusage": "false"}


def write_observations(path, rows):
    coverage.atomic_write_tsv(path, coverage.OUTPUT_COLUMNS, rows)


def phase_dir(root, row, events):
    directory = recycle.phase_record_dir(root, row["solver_label"], row["cap_s"],
                                          row["instance"], row["run_index"])
    directory.mkdir(parents=True)
    (directory / "1.jsonl").write_text("".join(json.dumps(event) + "\n" for event in events))
    return directory


@pytest.mark.parametrize("result,seconds,reason", [
    ("REALIZABLE", "0.3", "conclusive"),
    ("UNREALIZABLE", "2", "conclusive"),
    ("TIMEOUT", "17", "timeout"),
    ("MEMOUT", "4", "memout-same-scope"),
    ("MEMOUT", "16.9", "memout-same-scope"),
    ("ERROR", "1", "error-unverified"),
    ("CRASH", "1", "crash"),
    ("SYFCO-FAIL", "0", "syfco-fail"),
    ("UNKNOWN", "2", "decline-unverifiable"),
    ("UNKNOWN", "16.5", "deadline-near-cap"),
])
def test_recycle_classifies_every_status(result, seconds, reason):
    row = observation("x", result, seconds=seconds)
    kind, found, _ = recycle.classify_row(row, cap=17, margin=1)
    assert found == reason
    assert kind == ("rerun" if reason in {"timeout", "deadline-near-cap",
                                      "decline-unverifiable",
                                      "error-unverified", "crash"} else "recycle")


def test_recycle_certified_deterministic_error():
    row = observation("x", "ERROR", seconds="16.9")
    assert recycle.classify_row(row, cap=17, margin=1,
                                deterministic_errors={"x"})[:2] == \
        ("recycle", "deterministic-error")


def test_reviewed_error_label_is_disclosed_as_unverified(tmp_path):
    short = tmp_path / "short.tsv"
    write_observations(short, [observation("x", "ERROR")])
    reviewed = tmp_path / "reviewed.list"
    reviewed.write_text("x\n")
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=None,
                                    deterministic_error_list=reviewed,
                                    out_prefix=tmp_path / "plan"))
    plan = json.loads((tmp_path / "plan-plan.json").read_text())
    assert plan["counts"] == {"recycle:deterministic-error": 1}
    assert "unverified declarations" in plan["verification"]


@pytest.mark.parametrize("phase,stage,expected", [
    ("lift_decline_seed", "", "cap-independent-decline"),
    ("reduce_decline_target", "", "cap-independent-decline"),
    ("budget_decline", "budget-construction", "cap-independent-decline"),
    ("budget_decline", "budget-by-deadline", "deadline-stage"),
    ("lift_decline_deadline", "", "deadline-stage"),
])
def test_recycle_reads_decline_stage(tmp_path, phase, stage, expected):
    row = observation("x", "UNKNOWN")
    events = [{"arm": "lift", "phase": phase, "stage": stage},
              {"arm": "lift", "phase": "record_summary", "dropped_records": 0}]
    directory = phase_dir(tmp_path, row, events)
    kind, reason, stages = recycle.classify_row(
        row, cap=17, margin=1, records=directory,
        stage_pattern=recycle.re.compile("deadline"))
    assert reason == expected
    assert "lift:" in stages
    assert kind == ("rerun" if expected == "deadline-stage" else "recycle")


def test_recycle_uncertain_decline_and_external_kill(tmp_path):
    row = observation("x", "UNKNOWN")
    assert recycle.classify_row(row, cap=17, margin=1, records=tmp_path / "absent")[1] == \
        "decline-unverifiable"
    directory = phase_dir(tmp_path, row, [
        {"arm": "lift", "phase": "lift_seed_window", "work_count": 1},
        {"arm": "lift", "phase": "record_summary", "dropped_records": 0}])
    assert recycle.classify_row(row, cap=17, margin=1, records=directory)[1] == \
        "decline-unverifiable"
    row = observation("x", "CRASH")
    row["resource_reason"] = "signal:9"
    assert recycle.classify_row(row, cap=17, margin=1)[1] == "external-kill"


def test_recycle_plan_freezes_phase_records(tmp_path):
    row = observation("x", "UNKNOWN")
    short = tmp_path / "short.tsv"
    write_observations(short, [row])
    records = tmp_path / "records"
    directory = phase_dir(records, row, [
        {"arm": "lift", "phase": "lift_decline_seed"},
        {"arm": "lift", "phase": "record_summary", "dropped_records": 0}])
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=records,
                                    deterministic_error_list=None,
                                    out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.load_plan(plan)
    with (directory / "1.jsonl").open("a") as stream:
        stream.write('{"arm":"lift","phase":"changed"}\n')
    with pytest.raises(recycle.RecycleError, match="phase records changed"):
        recycle.load_plan(plan)


def test_recycle_plan_sample_validate_merge_and_mismatch(tmp_path):
    short = tmp_path / "short.tsv"
    measured = tmp_path / "measured.tsv"
    sample_run = tmp_path / "sample.tsv"
    rows = [observation("a", "REALIZABLE", seconds="1"),
            observation("b", "TIMEOUT", seconds="17")]
    write_observations(short, rows)
    write_observations(measured, [observation("b", "UNREALIZABLE", cap=60, seconds="30")])
    write_observations(sample_run, [observation("a", "REALIZABLE", cap=60, seconds="1.1")])
    short_records, long_records = tmp_path / "short-records", tmp_path / "long-records"
    events = [{"arm": "synthetic", "phase": "lift_method_x", "work_count": 3},
              {"arm": "synthetic", "phase": "record_summary", "dropped_records": 0}]
    phase_dir(short_records, rows[0], events)
    phase_dir(long_records, observation("a", "REALIZABLE", cap=60), events)
    prefix = tmp_path / "plan"
    assert recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                          deadline_margin=1, deadline_stage_regex="deadline",
                                          memory_max="8G", memory_swap_max="0",
                                          records=short_records, deterministic_error_list=None,
                                          out_prefix=prefix)) == 0
    plan = tmp_path / "plan-plan.json"
    assert (tmp_path / "plan-rerun.list").read_text() == "b\n"
    assert recycle.sample(argparse.Namespace(plan=plan, seed=19, size=1,
                                            out_prefix=tmp_path / "sample")) == 0
    disclosure = tmp_path / "sample-sample.json"
    validation = tmp_path / "validation.json"
    assert recycle.validate(argparse.Namespace(plan=plan, sample=disclosure, long=sample_run,
                                              long_records=long_records, out=validation)) == 0
    derived = tmp_path / "derived.tsv"
    assert recycle.merge(argparse.Namespace(plan=plan, long=measured, validation=validation,
                                           output=derived)) == 0
    merged = coverage.load_output(derived)
    assert [(row["instance"], row["provenance"], row["source_cap_s"], row["seconds"])
            for row in merged] == [("a", "recycled-17s", "17", "1"),
                                  ("b", "measured-60s", "60", "30")]
    assert all(row["binary_sha256"] == "a" * 64 and row["source_run_sha256"] for row in merged)
    provenance = json.loads((tmp_path / "derived-provenance.json").read_text())
    assert provenance["validation_mode"] == "phase-records"
    assert provenance["row_validation_modes"] == {"a": "phase-records"}
    long_phase = recycle.phase_record_dir(long_records, "candidate", "60", "a", "0") / \
        "1.jsonl"
    with long_phase.open("a") as stream:
        stream.write(json.dumps({"arm": "synthetic", "phase": "changed"}) + "\n")
    with pytest.raises(recycle.RecycleError, match="validation records changed"):
        recycle.merge(argparse.Namespace(plan=plan, long=measured, validation=validation,
                                         output=tmp_path / "stale.tsv"))
    bad = tmp_path / "bad.tsv"
    write_observations(bad, [observation("a", "UNKNOWN", cap=60)])
    verdict = tmp_path / "bad.json"
    assert recycle.validate(argparse.Namespace(plan=plan, sample=disclosure, long=bad,
                                              long_records=long_records, out=verdict)) == 1
    assert json.loads(verdict.read_text())["status"] == "fail"
    with pytest.raises(recycle.RecycleError, match="not validated"):
        recycle.merge(argparse.Namespace(plan=plan, long=measured, validation=verdict,
                                         output=tmp_path / "invalid.tsv"))


def outcome_only_inputs(tmp_path):
    short_row = observation("a", "REALIZABLE")
    short = tmp_path / "short.tsv"
    sample_run = tmp_path / "sample.tsv"
    write_observations(short, [short_row])
    write_observations(sample_run, [observation("a", "REALIZABLE", cap=60)])
    records = tmp_path / "short-records"
    phase_dir(records, short_row, [
        {"arm": "synthetic", "phase": "lift_method_x", "work_count": 3},
        {"arm": "synthetic", "phase": "record_summary", "dropped_records": 0}])
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=records,
                                    deterministic_error_list=None, out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1,
                                      out_prefix=tmp_path / "draw"))
    return plan, tmp_path / "draw-sample.json", sample_run


def test_outcome_only_validation_and_merge_skip_phase_records(tmp_path, monkeypatch):
    plan, disclosure, sample_run = outcome_only_inputs(tmp_path)
    reason = "legacy race arms have no shared completed records"
    args = argparse.Namespace(plan=plan, sample=disclosure, long=sample_run,
                              long_records=None, outcome_only=True, reason=reason,
                              out=tmp_path / "verdict.json")

    def no_phase_reads(*_args, **_kwargs):
        raise AssertionError("outcome-only validation read phase records")

    monkeypatch.setattr(recycle, "load_phase_records", no_phase_reads)
    monkeypatch.setattr(recycle, "phase_records_digest", no_phase_reads)
    assert recycle.validate(args) == 0
    verdict = json.loads(args.out.read_text())
    assert verdict["verification"] == "outcome-only"
    assert verdict["reason"] == reason
    assert "Per-arm decisions, decline stages and work were not compared" in verdict["limitation"]
    assert reason in verdict["limitation"]
    assert verdict["long_records"] == ""
    assert verdict["long_records_sha256"] == ""

    empty_long = tmp_path / "plan-empty-long.tsv"
    derived = tmp_path / "derived.tsv"
    assert recycle.merge(argparse.Namespace(plan=plan, long=empty_long,
                                           validation=args.out, output=derived)) == 0
    assert coverage.load_output(derived)[0]["provenance"] == "recycled-17s"
    provenance = json.loads((tmp_path / "derived-provenance.json").read_text())
    assert provenance["validation_mode"] == "outcome-only"
    assert provenance["row_validation_modes"] == {"a": "outcome-only"}
    assert provenance["limitation"] == verdict["limitation"]
    forged = {**verdict, "reason": "unreviewed exception"}
    args.out.write_text(json.dumps(forged))
    with pytest.raises(recycle.RecycleError, match="verdict differs from replay"):
        recycle.merge(argparse.Namespace(plan=plan, long=empty_long,
                                         validation=args.out,
                                         output=tmp_path / "forged.tsv"))


@pytest.mark.parametrize("field,value", [
    ("result", "UNREALIZABLE"),
    ("exit_code", "1"),
    ("timed_out", "true"),
    ("resource_reason", "signal:9"),
])
def test_outcome_only_rejects_each_outcome_mismatch(tmp_path, field, value):
    plan, disclosure, sample_run = outcome_only_inputs(tmp_path)
    changed = observation("a", "REALIZABLE", cap=60)
    changed[field] = value
    write_observations(sample_run, [changed])
    verdict = tmp_path / "verdict.json"
    assert recycle.validate(argparse.Namespace(
        plan=plan, sample=disclosure, long=sample_run, long_records=None,
        outcome_only=True, reason="phase records unavailable", out=verdict)) == 1
    failures = json.loads(verdict.read_text())["failures"]
    assert any(failure["kind"] == "outcome" and field in failure["message"]
               for failure in failures)


def test_outcome_only_requires_reason_and_refuses_long_records(tmp_path):
    plan, disclosure, sample_run = outcome_only_inputs(tmp_path)
    args = argparse.Namespace(plan=plan, sample=disclosure, long=sample_run,
                              long_records=None, outcome_only=True, reason=None,
                              out=tmp_path / "verdict.json")
    with pytest.raises(recycle.RecycleError, match="requires a nonempty --reason"):
        recycle.validate(args)
    args.reason = "  "
    with pytest.raises(recycle.RecycleError, match="requires a nonempty --reason"):
        recycle.validate(args)
    args.reason = "no per-arm records"
    args.long_records = tmp_path / "long-records"
    with pytest.raises(recycle.RecycleError, match="cannot be combined"):
        recycle.validate(args)
    assert not args.out.exists()
    parsed = recycle.build_parser().parse_args([
        "validate", "--plan", str(plan), "--sample", str(disclosure),
        "--long", str(sample_run), "--out", str(args.out),
        "--outcome-only", "--reason", args.reason])
    assert parsed.outcome_only and parsed.reason == args.reason


def test_sampler_rejects_changed_work_count_on_same_outcome(tmp_path):
    short = observation("a", "UNKNOWN")
    long = observation("a", "UNKNOWN", cap=60)
    first = tmp_path / "first"
    second = tmp_path / "second"
    for root, row, count in ((first, short, 3), (second, long, 4)):
        phase_dir(root, row, [{"arm": "lift", "phase": "lift_seed_window",
                              "work_count": count},
                             {"arm": "lift", "phase": "record_summary",
                              "dropped_records": 0}])
    checked, failures, _ = capcheck.compare({"a": short}, {"a": long}, first, second,
                                             max_long_seconds=None)
    assert checked == 1
    assert [(failure["arm"], failure["kind"]) for failure in failures] == \
        [("lift", "work")]


@pytest.mark.parametrize("result", ["REALIZABLE", "TIMEOUT"])
def test_merge_handles_empty_side_of_recycling(tmp_path, result):
    short = tmp_path / "short.tsv"
    row = {**observation("a", result), "scope_memory_peak_bytes": "4096",
           "scope_memory_peak_source": "cgroup-v2/memory.peak", "memory_cgroup": "/invocation"}
    write_observations(short, [row])
    coverage.write_memory_sidecar(short, [row])
    prefix = tmp_path / "plan"
    short_records, long_records = tmp_path / "short-records", tmp_path / "long-records"
    events = [{"arm": "synthetic", "phase": "lift_method_x", "work_count": 3},
              {"arm": "synthetic", "phase": "record_summary", "dropped_records": 0}]
    if result == "REALIZABLE":
        phase_dir(short_records, observation("a", result), events)
        phase_dir(long_records, observation("a", result, cap=60), events)
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=short_records,
                                    deterministic_error_list=None, out_prefix=prefix))
    manifest = json.loads((tmp_path / "plan-plan.json").read_text())
    if result == "REALIZABLE":
        long = pathlib.Path(manifest["empty_long"])
        validation = tmp_path / "validation.json"
        write_observations(tmp_path / "sample.tsv",
                           [observation("a", result, cap=60)])
        recycle.sample(argparse.Namespace(plan=tmp_path / "plan-plan.json", seed=4,
                                          size=1, out_prefix=tmp_path / "sample"))
        assert recycle.validate(argparse.Namespace(plan=tmp_path / "plan-plan.json",
                                                  sample=tmp_path / "sample-sample.json",
                                                  long=tmp_path / "sample.tsv",
                                                  long_records=long_records, out=validation)) == 0
    else:
        long = tmp_path / "long.tsv"
        write_observations(long, [observation("a", "UNREALIZABLE", cap=60)])
        validation = None
    output = tmp_path / "merged.tsv"
    assert recycle.merge(argparse.Namespace(plan=tmp_path / "plan-plan.json", long=long,
                                           validation=validation, output=output)) == 0
    assert coverage.load_output(output)[0]["source_cap_s"] == \
        ("17" if result == "REALIZABLE" else "60")
    if result == "REALIZABLE":
        assert coverage.load_output(output)[0]["memory_cgroup"] == "/invocation"
        assert threeway.load_series("candidate", output, 60, ["a"])["rows"]["a"][
            "scope_memory_peak_source"] == "cgroup-v2/memory.peak"
    if result == "REALIZABLE":
        forged = json.loads(validation.read_text())
        forged["notes"].append("forged pass detail")
        validation.write_text(json.dumps(forged))
        with pytest.raises(recycle.RecycleError, match="verdict differs from replay"):
            recycle.merge(argparse.Namespace(plan=tmp_path / "plan-plan.json", long=long,
                                             validation=validation,
                                             output=tmp_path / "forged.tsv"))


def test_threeway_hand_calculated_scores(tmp_path):
    names = ["a", "b"]
    instance_list = tmp_path / "all.list"
    instance_list.write_text("a\nb\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\na\ta.tlsf\nb\tb.tlsf\n")
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    (tmp_path / "b.tlsf").write_text("// STATUS: UNREALIZABLE\n")
    exceptions = tmp_path / "exceptions.tsv"
    exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n")
    data = {"ltl": [("REALIZABLE", "1"), ("TIMEOUT", "17")],
            "tacas": [("TIMEOUT", "17"), ("UNREALIZABLE", "2")],
            "new": [("REALIZABLE", "0.5"), ("UNREALIZABLE", "3")]}
    for label, values in data.items():
        write_observations(tmp_path / f"{label}.tsv", [
            observation(name, result, seconds=seconds, label=label)
            for name, (result, seconds) in zip(names, values)])
    args = argparse.Namespace(list=instance_list, cap=17,
                              series=[f"{label}={tmp_path / f'{label}.tsv'}" for label in data],
                              internal=[], new="new", legacy_audit=[], untimed_conversion=[],
                              tlsf_map=mapping, tlsf_corpus=tmp_path,
                              status_exceptions=exceptions, noise_floor=12.074,
                              noise_floor_source=("documented spread cap=17 corpus=" +
                                                  threeway.corpus_digest(names,
                                                      coverage.read_tlsf_map(mapping), tmp_path)),
                              manifest=None)
    report, views = threeway.join(args)
    assert [(report["series"][label]["solved"], report["series"][label]["par2_total_s"])
            for label in data] == [(1, 35), (1, 36), (2, 3.5)]
    assert report["series"]["new"]["par2_mean_s"] == 1.75
    assert report["paired"]["ltl"]["gains"] == ["b"]
    assert report["paired"]["tacas"]["gains"] == ["a"]
    assert report["conflicts"] == []
    assert "declared noise floor remain unverified" in report["verification"]
    threeway.write_outputs(report, views, tmp_path / "joined", list(report["series"]))
    with (tmp_path / "joined/threeway-17s-gains-losses.tsv").open(newline="") as stream:
        pairs = list(csv.DictReader(stream, delimiter="\t"))
    assert pairs[0]["new_provenance"] == "measured-17s"
    assert pairs[0]["other_source_cap_s"] == "17"
    args.noise_floor_source = "cap=60 corpus=" + report["corpus_sha256"]
    with pytest.raises(threeway.JoinError, match="matching cap"):
        threeway.join(args)


def test_lifting_work_and_target_attribution(tmp_path):
    row = observation("a", "REALIZABLE")
    events = [{"arm": "lift", "phase": "lift_seed_window", "work_count": 3,
               "wall_ns": 10, "cpu_ns": 8},
              {"arm": "lift", "phase": "lift_seed_solve", "work_count": 1,
               "wall_ns": 20, "cpu_ns": 15},
              {"arm": "lift", "phase": "lift_target_reduce", "work_count": 5,
               "wall_ns": 30, "cpu_ns": 22},
              {"arm": "lift", "phase": "lift_method_certificate"},
              {"arm": "lift", "phase": "outer_check", "work_count": 1,
               "wall_ns": 40, "cpu_ns": 31},
              {"arm": "lift", "phase": "record_summary", "dropped_records": 0}]
    phase_dir(tmp_path, row, events)
    summary, detail = lifting.report([row], tmp_path, "lift", single_arm_leg=True)
    assert summary["seed_probes"] == 3
    assert summary["seed_solves"] == 1
    assert summary["target_work"] == 6
    assert summary["target_solve_attributed"] == ["a"]
    assert detail[0]["check_method"] == "lift_method_certificate"


def test_native_real_lift_routes_checks_declines_and_tsv(tmp_path):
    arm = "both:gr1-real-lift:oxidd"
    r_row = observation("r", "REALIZABLE")
    r_row["winner"] = "lifting"
    direct_row = observation("direct", "UNREALIZABLE")
    decline_row = observation("decline", "TIMEOUT")
    phase_dir(tmp_path, r_row, [
        {"arm": arm, "phase": "trusted_prepare", "wall_ns": 20},
        {"arm": arm, "phase": "combined_call", "wall_ns": 100},
        {"arm": arm, "phase": "route_R"},
        {"arm": arm, "phase": "seed_REAL", "work_count": 2},
        {"arm": arm, "phase": "seed_cache", "work_count": 9},
        {"arm": arm, "phase": "target_reductions", "work_count": 1},
        {"arm": arm, "phase": "final_checks", "work_count": 1},
        {"arm": arm, "phase": "proof_binding"},
        {"arm": arm, "phase": "record_summary", "dropped_records": 0},
    ])
    phase_dir(tmp_path, direct_row, [
        {"arm": arm, "phase": "route_direct"},
        {"arm": arm, "phase": "seed_none"},
        {"arm": arm, "phase": "seed_cache", "work_count": 3},
        {"arm": arm, "phase": "target_reductions", "work_count": 1},
        {"arm": arm, "phase": "final_checks", "work_count": 2},
        {"arm": arm, "phase": "proof_binding"},
        {"arm": arm, "phase": "record_summary", "dropped_records": 0},
    ])
    phase_dir(tmp_path, decline_row, [
        {"arm": arm, "phase": "route_direct"},
        {"arm": arm, "phase": "seed_cache", "work_count": 4},
        {"arm": arm, "phase": "target_reductions", "work_count": 1},
        {"arm": arm, "phase": "final_checks", "work_count": 0},
        {"arm": arm, "phase": "decline_seed_window"},
        {"arm": arm, "phase": "record_summary", "dropped_records": 0},
    ])
    summary, detail = lifting.report([r_row, direct_row, decline_row], tmp_path, arm)
    assert summary["seed_work"] == 16
    assert summary["seed_solves"] == 2
    assert summary["seed_probes"] is None
    assert summary["target_work"] == 6
    assert summary["target_checks"] == 3
    assert summary["routes"] == {"R": 1, "direct": 2}
    assert summary["route_solves"] == {"R": 1, "direct": 1}
    assert summary["declines"] == {"decline_seed_window": 1}
    assert summary["unclassified_phase_work"] == 0
    assert summary["target_solve_attributed"] == ["r"]
    assert detail[1]["route_solve"] and not detail[1]["target_solve_attributed"]
    assert not detail[2]["target_checked"]
    lifting.write_report(summary, detail, tmp_path / "report")
    with (tmp_path / "report/lifting.tsv").open(newline="") as stream:
        tsv = list(csv.DictReader(stream, delimiter="\t"))
    assert len(tsv) == 3
    assert tsv[0]["arm"] == arm and tsv[0]["seed_work"] == "9"
    assert tsv[0]["target_work"] == "2"
    assert json.loads(tsv[0]["phase_work"])["seed_REAL"] == 2
    assert tsv[2]["declines"] == "decline_seed_window"


def test_reporting_requires_explicit_provenance(tmp_path):
    native = [{"series": {"kind": "arm", "tool": "acacia"},
               "verification": "campaign specification and input bytes verified"}]
    converted = [{"series": {"kind": "legacy", "tool": "ltlsynt"},
                  "verification": "campaign specification and input bytes verified"}]
    assert "TLSF parsed" in threeway.conversion_boundary("new", [], native)
    assert "SyFCo conversion" in threeway.conversion_boundary("old", [], converted)
    assert "unverified" in threeway.conversion_boundary("old", [], [])
    assert "unverified" in threeway.conversion_boundary(
        "old", [], [{"series": {"kind": "legacy", "tool": "ltlsynt"},
                     "verification": "partial: no campaign specification"}])
    with pytest.raises(threeway.JoinError, match="conflicts with manifest runner"):
        threeway.conversion_boundary("new", ["new"], native)
    row = observation("a", "REALIZABLE", cap=60)
    row.update(provenance="recycled-fake", source_cap_s="17")
    with pytest.raises(ValueError, match="invalid derived provenance"):
        lifting.report([row], tmp_path, "synthetic")


def test_manifest_join_checks_list_corpus_and_row_provenance(tmp_path):
    listing = tmp_path / "all.list"
    listing.write_text("a\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\na\ta.tlsf\n")
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    exceptions = tmp_path / "exceptions.tsv"
    exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n")
    for label in ("old", "new"):
        write_observations(tmp_path / f"{label}-17s.tsv", [
            observation("a", "REALIZABLE", label=label)])
    manifest = tmp_path / "manifest.json"
    frozen = {"frozen_list_sha256": threeway.sha256_file(listing), "cap_s": 17,
              "inputs": {"list": {"path": str(listing), "sha256": threeway.sha256_file(listing)}},
              "corpus_sha256": threeway.corpus_digest(
                  ["a"], coverage.read_tlsf_map(mapping), tmp_path),
              "series": {"new": {"binary_sha256": "a" * 64,
                                 "source_revision": "source"}}}
    manifest.write_text(json.dumps(frozen))
    args = argparse.Namespace(list=listing, cap=17,
                              series=[f"old={tmp_path / 'old-17s.tsv'}",
                                      f"new={tmp_path / 'new-17s.tsv'}"],
                              internal=[], new="new", legacy_audit=[], untimed_conversion=[],
                              tlsf_map=mapping, tlsf_corpus=tmp_path,
                              status_exceptions=exceptions, noise_floor=None,
                              noise_floor_source="", manifest=[manifest])
    report, views = threeway.join(args)
    assert report["series"]["new"]["manifest_identity"]
    assert "partial" in report["series"]["new"]["manifest_identity"][0]["verification"]
    threeway.write_outputs(report, views, tmp_path / "join", list(report["series"]))
    with (tmp_path / "join/new-17s-scored.csv").open(newline="") as stream:
        row = next(csv.DictReader(stream))
    assert (row["source_cap_s"], row["provenance"], row["source_run"]) == \
        ("17", "measured-17s", str(tmp_path / "new-17s.tsv"))
    frozen["corpus_sha256"] = "0" * 64
    manifest.write_text(json.dumps(frozen))
    with pytest.raises(threeway.JoinError, match="corpus SHA-256"):
        threeway.join(args)
    frozen["corpus_sha256"] = report["corpus_sha256"]
    frozen["frozen_list_sha256"] = "0" * 64
    manifest.write_text(json.dumps(frozen))
    with pytest.raises(threeway.JoinError, match="frozen list SHA-256"):
        threeway.join(args)
    frozen["frozen_list_sha256"] = threeway.sha256_file(listing)
    frozen.pop("cap_s")
    manifest.write_text(json.dumps(frozen))
    with pytest.raises(threeway.JoinError, match="valid positive cap_s"):
        threeway.join(args)
    frozen["cap_s"] = 17
    misplaced = tmp_path / "misplaced"
    misplaced.mkdir()
    wrong_manifest = misplaced / "manifest.json"
    wrong_manifest.write_text(json.dumps(frozen))
    args.manifest = [wrong_manifest]
    with pytest.raises(threeway.JoinError, match="observation path differs"):
        threeway.join(args)


@pytest.mark.parametrize("floor", [-1, float("nan"), float("inf")])
def test_join_rejects_invalid_noise_floor(tmp_path, floor):
    listing = tmp_path / "all.list"
    listing.write_text("a\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\na\ta.tlsf\n")
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    args = argparse.Namespace(list=listing, tlsf_map=mapping, tlsf_corpus=tmp_path,
                              noise_floor=floor, noise_floor_source="cap=17 corpus=x", cap=17)
    with pytest.raises(threeway.JoinError, match="finite and nonnegative"):
        threeway.join(args)


def test_campaign_and_join_reject_invalid_cap(tmp_path):
    with pytest.raises(threeway.JoinError, match="positive integer"):
        threeway.join(argparse.Namespace(cap=0))
    with pytest.raises(coverage.plan_CampaignError, match="positive integer"):
        coverage.plan_read_comparison(tmp_path / "unused.json", -1)


def test_validate_changed_native_work_refuses_merge(tmp_path):
    first = observation("a", "UNKNOWN", seconds="2")
    second = observation("a", "UNKNOWN", cap=60, seconds="2")
    short, long = tmp_path / "short.tsv", tmp_path / "sample.tsv"
    write_observations(short, [first])
    write_observations(long, [second])
    short_records, long_records = tmp_path / "short-records", tmp_path / "long-records"
    for root, row, work in ((short_records, first, 3), (long_records, second, 4)):
        phase_dir(root, row, [
            {"arm": "synthetic", "phase": "lift_method_x", "work_count": work},
            {"arm": "synthetic", "phase": "lift_decline_seed_solve"},
            {"arm": "synthetic", "phase": "record_summary", "dropped_records": 0}])
    prefix = tmp_path / "plan"
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0",
                                    records=short_records, deterministic_error_list=None,
                                    out_prefix=prefix))
    plan = tmp_path / "plan-plan.json"
    assert recycle.read_tsv(tmp_path / "plan-classification.tsv")[0]["class"] == "recycle"
    recycle.sample(argparse.Namespace(plan=plan, seed=7, size=1,
                                      out_prefix=tmp_path / "sample"))
    verdict = tmp_path / "verdict.json"
    assert recycle.validate(argparse.Namespace(plan=plan,
                                              sample=tmp_path / "sample-sample.json",
                                              long=long, long_records=long_records,
                                              out=verdict)) == 1
    assert json.loads(verdict.read_text())["failures"][0]["kind"] == "work"
    with pytest.raises(recycle.RecycleError, match="not validated"):
        recycle.merge(argparse.Namespace(plan=plan,
                                         long=tmp_path / "plan-empty-long.tsv",
                                         validation=verdict, output=tmp_path / "derived.tsv"))


def test_native_validation_requires_records(tmp_path):
    first = observation("a", "REALIZABLE")
    short, long = tmp_path / "short.tsv", tmp_path / "long.tsv"
    write_observations(short, [first])
    write_observations(long, [observation("a", "REALIZABLE", cap=60)])
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=None,
                                    deterministic_error_list=None, out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1,
                                      out_prefix=tmp_path / "sample"))
    with pytest.raises(recycle.RecycleError, match="requires short phase records"):
        recycle.validate(argparse.Namespace(plan=plan, sample=tmp_path / "sample-sample.json",
                                          long=long, long_records=None,
                                          out=tmp_path / "verdict.json"))


def test_native_validation_rejects_empty_decision_records(tmp_path):
    first = observation("a", "REALIZABLE")
    second = observation("a", "REALIZABLE", cap=60)
    short, long = tmp_path / "short.tsv", tmp_path / "long.tsv"
    write_observations(short, [first])
    write_observations(long, [second])
    short_records, long_records = tmp_path / "short-records", tmp_path / "long-records"
    for root, row in ((short_records, first), (long_records, second)):
        phase_dir(root, row, [{"phase": "record_summary", "dropped_records": 0}])
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0",
                                    records=short_records, deterministic_error_list=None,
                                    out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1,
                                      out_prefix=tmp_path / "sample"))
    verdict = tmp_path / "verdict.json"
    assert recycle.validate(argparse.Namespace(plan=plan,
                                              sample=tmp_path / "sample-sample.json",
                                              long=long, long_records=long_records,
                                              out=verdict)) == 1
    assert "no shared completed arm records" in verdict.read_text()


def test_recycle_rejects_decline_from_unlisted_arm(tmp_path):
    row = observation("a", "UNKNOWN", seconds="2")
    short = tmp_path / "short.tsv"
    write_observations(short, [row])
    records = tmp_path / "records"
    phase_dir(records, row, [
        {"arm": "forged", "phase": "lift_decline_seed_solve"},
        {"arm": "forged", "phase": "record_summary", "dropped_records": 0}])
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=records,
                                    deterministic_error_list=None, out_prefix=tmp_path / "plan"))
    classification = recycle.read_tsv(tmp_path / "plan-classification.tsv")
    assert (classification[0]["class"], classification[0]["reason"]) == \
        ("rerun", "decline-unverifiable")


def test_review_native_equals_flag_requires_phase_records(tmp_path):
    first = observation("a", "REALIZABLE")
    first["flags"] = "--arms=real:small:backward"
    short, long = tmp_path / "short.tsv", tmp_path / "sample.tsv"
    write_observations(short, [first])
    write_observations(long, [{**observation("a", "REALIZABLE", cap=60),
                               "flags": first["flags"]}])
    first = coverage.load_output(short)[0]
    assert first["flags"] == "--arms=real:small:backward"
    assert recycle.arms_from_flags(first["flags"]) == ["real:small:backward"]
    prefix = tmp_path / "plan"
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=None,
                                    deterministic_error_list=None, out_prefix=prefix))
    plan = tmp_path / "plan-plan.json"
    assert json.loads(plan.read_text())["leg_kind"] == "native"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1,
                                      out_prefix=tmp_path / "draw"))
    with pytest.raises(recycle.RecycleError, match="requires short phase records"):
        recycle.validate(argparse.Namespace(plan=plan, sample=tmp_path / "draw-sample.json",
                                          long=long, long_records=None,
                                          out=tmp_path / "verdict.json"))


def test_native_manifest_identity_does_not_depend_on_flag_spelling(tmp_path):
    row = observation("a", "REALIZABLE")
    row["flags"] = ""
    short = tmp_path / "candidate-17s.tsv"
    write_observations(short, [row])
    selected = tmp_path / "selected.list"
    selected.write_text("a\n")
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({
        "cap_s": 17,
        "inputs": {"list": {"path": str(selected),
                            "sha256": recycle.sha256_file(selected)}},
        "series": {"candidate": {"kind": "arm", "arms": ["synthetic"],
                                 "binary_sha256": "a" * 64,
                                 "source_revision": "source"}}}))
    with pytest.raises(recycle.RecycleError, match="leg kind is unknown"):
        recycle.leg_identity(argparse.Namespace(), {"a": row})
    assert recycle.leg_identity(argparse.Namespace(leg_kind="legacy"), {"a": row})[0] == \
        "legacy"
    kind, arms, _, digest = recycle.leg_identity(
        argparse.Namespace(manifest=manifest, short=short), {"a": row})
    assert (kind, arms, digest) == ("native", ["synthetic"], recycle.sha256_file(manifest))


def manifest_subset_fixture(tmp_path):
    listing = tmp_path / "full.list"
    listing.write_text("a\nb\n")
    selected = tmp_path / "selected.list"
    selected.write_text("a\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\na\ta.tlsf\nb\tb.tlsf\n")
    for name in ("a", "b"):
        (tmp_path / f"{name}.tlsf").write_text("// STATUS: REALIZABLE\n")
    exceptions = tmp_path / "exceptions.tsv"
    exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n")
    for label in ("old", "new"):
        write_observations(tmp_path / f"{label}.tsv", [
            observation(name, "REALIZABLE", label=label) for name in ("a", "b")])
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({
        "frozen_list_sha256": threeway.sha256_file(listing), "cap_s": 17,
        "inputs": {"list": {"path": str(selected), "sha256": threeway.sha256_file(selected)}},
        "corpus_sha256": threeway.corpus_digest(["a"],
                                                coverage.read_tlsf_map(mapping), tmp_path),
        "series": {"new": {"binary_sha256": "a" * 64,
                           "source_revision": "source"}},
    }))
    args = argparse.Namespace(list=listing, cap=17,
                              series=[f"old={tmp_path / 'old.tsv'}",
                                      f"new={tmp_path / 'new.tsv'}"], internal=[], new="new",
                              legacy_audit=[], untimed_conversion=[],
                              tlsf_map=mapping, tlsf_corpus=tmp_path,
                              status_exceptions=exceptions,
                              noise_floor=None, noise_floor_source="",
                              manifest=[manifest], out=tmp_path)
    return args, manifest


def test_review_manifest_subset_cannot_cover_full_observation(tmp_path):
    args, _ = manifest_subset_fixture(tmp_path)
    with pytest.raises(threeway.JoinError, match="selected panel differs"):
        threeway.join(args)


def derived_manifest_fixture(tmp_path):
    listing = tmp_path / "full.list"
    listing.write_text("a\nb\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\na\ta.tlsf\nb\tb.tlsf\n")
    for name in ("a", "b"):
        (tmp_path / f"{name}.tlsf").write_text("// STATUS: REALIZABLE\n")
    exceptions = tmp_path / "exceptions.tsv"
    exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n")
    short_dir, long_dir = tmp_path / "short", tmp_path / "long"
    short_dir.mkdir()
    long_dir.mkdir()
    short_source = short_dir / "new-17s.tsv"
    long_source = long_dir / "new-60s.tsv"
    write_observations(short_source, [observation(name, "REALIZABLE", label="new")
                                      for name in ("a", "b")])
    write_observations(long_source, [observation("b", "REALIZABLE", cap=60, label="new")])
    write_observations(tmp_path / "old.tsv", [observation(name, "REALIZABLE", cap=60,
                                                           label="old") for name in ("a", "b")])
    derived = tmp_path / "derived.tsv"
    rows = []
    for name, source, source_cap, provenance in (
            ("a", short_source, 17, "recycled-17s"),
            ("b", long_source, 60, "measured-60s")):
        row = observation(name, "REALIZABLE", cap=60, label="new")
        row.update(provenance=provenance, source_run=os.path.relpath(source, tmp_path),
                   source_run_sha256=threeway.sha256_file(source), source_cap_s=str(source_cap))
        rows.append(row)
    coverage.atomic_write_tsv(derived, coverage.OUTPUT_COLUMNS + coverage.DERIVED_COLUMNS, rows)

    def source_manifest(directory, cap, selected):
        panel = directory / "selected.list"
        panel.write_text("".join(f"{name}\n" for name in selected))
        path = directory / "manifest.json"
        path.write_text(json.dumps({
            "frozen_list_sha256": threeway.sha256_file(listing), "cap_s": cap,
            "inputs": {"list": {"path": str(panel), "sha256": threeway.sha256_file(panel)}},
            "corpus_sha256": threeway.corpus_digest(selected,
                                                    coverage.read_tlsf_map(mapping), tmp_path),
            "series": {"new": {"binary_sha256": "a" * 64,
                               "source_revision": "source"}}}))
        return path

    short_manifest = source_manifest(short_dir, 17, ["a", "b"])
    short_frozen = json.loads(short_manifest.read_text())
    short_frozen["series"]["old"] = {"binary_sha256": "a" * 64,
                                      "source_revision": "source"}
    short_manifest.write_text(json.dumps(short_frozen))
    long_manifest = source_manifest(long_dir, 60, ["b"])
    args = argparse.Namespace(list=listing, cap=60,
                              series=[f"old={tmp_path / 'old.tsv'}", f"new={derived}"],
                              internal=[], new="new", legacy_audit=[], untimed_conversion=[],
                              tlsf_map=mapping, tlsf_corpus=tmp_path,
                              status_exceptions=exceptions, noise_floor=None,
                              noise_floor_source="", manifest=[short_manifest, long_manifest])
    return args, rows, source_manifest, short_source, long_source, derived


def test_derived_manifest_panels_match_actual_source_rows(tmp_path):
    args, rows, source_manifest, short_source, long_source, derived = \
        derived_manifest_fixture(tmp_path)
    long_dir = long_source.parent
    short_manifest, long_manifest = args.manifest
    report, _ = threeway.join(args)
    assert len(report["series"]["new"]["manifest_identity"]) == 2
    assert "manifest_identity" not in report["series"]["old"]
    args.manifest = []
    with pytest.raises(threeway.JoinError, match="needs its 17 s and rerun manifests"):
        threeway.join(args)
    args.manifest = [short_manifest, long_manifest]
    source_manifest(long_dir, 60, ["a", "b"])
    with pytest.raises(threeway.JoinError, match="source run panel differs from manifest"):
        threeway.join(args)
    source_manifest(long_dir, 60, ["b"])
    for field, changed in (("result", "TIMEOUT"), ("seconds", "9")):
        forged = [dict(row) for row in rows]
        forged[1][field] = changed
        coverage.atomic_write_tsv(
            derived, coverage.OUTPUT_COLUMNS + coverage.DERIVED_COLUMNS, forged)
        with pytest.raises(threeway.JoinError, match=f"derived {field} differs from source run"):
            threeway.join(args)
    forged = [dict(row) for row in rows]
    forged[1]["source_run"] = os.path.relpath(short_source, tmp_path)
    forged[1]["source_run_sha256"] = threeway.sha256_file(short_source)
    coverage.atomic_write_tsv(derived, coverage.OUTPUT_COLUMNS + coverage.DERIVED_COLUMNS,
                              forged)
    with pytest.raises(threeway.JoinError, match="source cap differs"):
        threeway.join(args)
    forged = [dict(row) for row in rows]
    forged[1]["provenance"] = "recycled-17s"
    coverage.atomic_write_tsv(derived, coverage.OUTPUT_COLUMNS + coverage.DERIVED_COLUMNS,
                              forged)
    with pytest.raises(threeway.JoinError, match="invalid derived provenance"):
        threeway.join(args)


@pytest.mark.parametrize("change,expected", [
    ("missing_17", "source run panel differs from manifest"),
    ("extra_60", "source run panel differs from manifest"),
    ("duplicate_17", "duplicate source instance"),
    ("rerun_selection", "rerun manifest selected panel differs"),
])
def test_derived_join_rejects_inexact_source_panels(tmp_path, change, expected):
    args, rows, source_manifest, short_source, long_source, derived = \
        derived_manifest_fixture(tmp_path)
    if change == "missing_17":
        write_observations(short_source, [observation("a", "REALIZABLE", label="new")])
        rows[0]["source_run_sha256"] = threeway.sha256_file(short_source)
    elif change == "extra_60":
        write_observations(long_source, [observation(name, "REALIZABLE", cap=60,
                                                     label="new") for name in ("a", "b")])
        rows[1]["source_run_sha256"] = threeway.sha256_file(long_source)
    elif change == "duplicate_17":
        write_observations(short_source, [observation(name, "REALIZABLE", label="new")
                                          for name in ("a", "b", "b")])
        rows[0]["source_run_sha256"] = threeway.sha256_file(short_source)
    else:
        source_manifest(long_source.parent, 60, ["a", "b"])
        write_observations(long_source, [observation(name, "REALIZABLE", cap=60,
                                                     label="new") for name in ("a", "b")])
        rows[1]["source_run_sha256"] = threeway.sha256_file(long_source)
    coverage.atomic_write_tsv(derived, coverage.OUTPUT_COLUMNS + coverage.DERIVED_COLUMNS,
                              rows)
    with pytest.raises(threeway.JoinError, match=expected):
        threeway.join(args)


def mixed_comparison_fixture(tmp_path, monkeypatch):
    binary = tmp_path / "solver"
    binary.write_text("#!/bin/sh\nexit 0\n")
    binary.chmod(0o755)
    (tmp_path / "all.list").write_text("a.ltl\n")
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    (tmp_path / "map.tsv").write_text("instance\ttlsf\na.ltl\ta.tlsf\n")
    (tmp_path / "exceptions.tsv").write_text(
        "instance\tannotated_status\tcorrected_status\tevidence\n")
    pairs = tmp_path / "pairs"
    pairs.mkdir()
    (pairs / "a.ltl").write_text("G true\n")
    (pairs / "a.part").write_text("INPUTS: x\nOUTPUTS: y\n")
    (tmp_path / "failures.list").write_text("")
    (tmp_path / "semantics.tsv").write_text("instance\tsemantics\na.ltl\tMealy\n")
    runtime = tmp_path / "old-runtime"
    runtime.mkdir()
    sums = []
    for name in ("libspot", "libbddx"):
        target = runtime / f"{name}.so.0.0.0"
        target.write_text(name)
        (runtime / f"{name}.so.0").symlink_to(target.name)
        sums.append(f"{coverage.plan_sha256_file(target)}  ./lib/{target.name}\n")
    sums_file = tmp_path / "SHA256SUMS"
    sums_file.write_text("".join(sums))
    five = ["real:small:backward", "real:small:forward", "unreal:formula:forward",
            "unreal:automaton:forward", "real:gr1:oxidd"]
    six = five + ["unreal:gr1:oxidd"]
    options = tmp_path / "intro-buildoptions.json"
    options.write_text(json.dumps([{"name": "acacia_default_arms", "value": ",".join(six)}]))
    shared = {"binary": "solver", "binary_sha256": coverage.plan_sha256_file(binary),
              "source_revision": "source"}
    spec = {"schema": 1, "memory_max": "8G", "memory_swap_max": "0",
            "list": "all.list", "tlsf_map": "map.tsv", "tlsf_corpus": ".",
            "status_exceptions": "exceptions.tsv", "runtime_sha256s": "SHA256SUMS",
            "series": [
                {**shared, "label": "ltlsynt", "kind": "legacy", "tool": "ltlsynt",
                 "portfolio_size": None, "instances_dir": "pairs",
                 "syfco_failures": "failures.list", "semantics_map": "semantics.tsv",
                 "syfco": str(binary)},
                {**shared, "label": "TACAS23", "kind": "legacy", "tool": "acacia1x",
                 "portfolio_size": None, "runtime_lib_dir": runtime.name,
                 "instances_dir": "pairs", "syfco_failures": "failures.list",
                 "syfco": str(binary)},
                {**shared, "label": "E5", "kind": "race", "portfolio_size": 5,
                 "arms": five},
                {**shared, "label": "final", "kind": "race", "portfolio_size": 6,
                 "preset": "frozen_fixture", "arms": six,
                 "use_default_arms": True, "build_options": options.name}]}
    comparison = tmp_path / "comparison.json"
    comparison.write_text(json.dumps(spec))
    return comparison, spec, options


@pytest.mark.parametrize("change,expected", [
    ("missing_size", "portfolio_size must be declared"),
    ("wrong_five", "portfolio_size must match launched arms"),
    ("wrong_six", "portfolio_size must match launched arms"),
    ("legacy_size", "legacy tool needs portfolio_size null"),
    ("wrong_defaults", "build default arms differ"),
    ("override", "put --arms in the arms array"),
    ("override_short", "flags cannot change the launched arms"),
])
def test_mixed_comparison_rejects_wrong_sizes_and_launches(tmp_path, monkeypatch,
                                                           change, expected):
    comparison, spec, options = mixed_comparison_fixture(tmp_path, monkeypatch)
    series = {item["label"]: item for item in spec["series"]}
    if change == "missing_size":
        series["E5"].pop("portfolio_size")
    elif change == "wrong_five":
        series["E5"]["portfolio_size"] = 6
    elif change == "wrong_six":
        series["final"]["portfolio_size"] = 5
    elif change == "legacy_size":
        series["ltlsynt"]["portfolio_size"] = 5
    elif change == "wrong_defaults":
        options.write_text(json.dumps([{"name": "acacia_default_arms", "value": "a,b"}]))
    elif change == "override":
        series["final"]["flags"] = "--arms fake"
    else:
        series["final"]["flags"] = "-rsmall"
    comparison.write_text(json.dumps(spec))
    with pytest.raises(coverage.plan_CampaignError, match=expected):
        coverage.plan_read_comparison(comparison, 17)


def test_capless_review_reproduction_is_rejected(tmp_path):
    args, manifest = manifest_subset_fixture(tmp_path)
    frozen = json.loads(manifest.read_text())
    del frozen["cap_s"]
    manifest.write_text(json.dumps(frozen))
    with pytest.raises(threeway.JoinError, match="valid positive cap_s"):
        threeway.join(args)


def test_plan_recomputes_classification_after_consistent_hash_forgery(tmp_path):
    short = tmp_path / "short.tsv"
    write_observations(short, [observation("a", "TIMEOUT", seconds="17")])
    prefix = tmp_path / "plan"
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=None,
                                    deterministic_error_list=None, out_prefix=prefix))
    plan = tmp_path / "plan-plan.json"
    record = json.loads(plan.read_text())
    table = tmp_path / "plan-classification.tsv"
    forged = recycle.read_tsv(table)
    forged[0]["class"] = "recycle"
    forged[0]["reason"] = "conclusive"
    recycle.write_tsv(table, recycle.CLASSIFICATION_COLUMNS, forged)
    (tmp_path / "plan-rerun.list").write_text("")
    record.update(classification_sha256=recycle.sha256_file(table), rerun=0,
                  rerun_list_sha256=recycle.sha256_file(tmp_path / "plan-rerun.list"),
                  counts={"recycle:conclusive": 1})
    plan.write_text(json.dumps(record))
    with pytest.raises(recycle.RecycleError, match="classification differs"):
        recycle.load_plan(plan)


def test_sample_recomputes_seeded_selection_after_hash_forgery(tmp_path):
    short = tmp_path / "short.tsv"
    rows = [observation(name, "REALIZABLE") for name in ("a", "b")]
    for row in rows:
        row["flags"] = ""
    write_observations(short, rows)
    recycle.plan(argparse.Namespace(short=short, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=None,
                                    manifest=None, leg_kind="legacy",
                                    deterministic_error_list=None, out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.sample(argparse.Namespace(plan=plan, seed=7, size=1,
                                      out_prefix=tmp_path / "sample"))
    disclosure = tmp_path / "sample-sample.json"
    record = json.loads(disclosure.read_text())
    listing = tmp_path / "sample-sample.list"
    listing.write_text("b\n" if listing.read_text() == "a\n" else "a\n")
    record["sample_list_sha256"] = recycle.sha256_file(listing)
    disclosure.write_text(json.dumps(record))
    with pytest.raises(recycle.RecycleError, match="differs from seeded selection"):
        recycle.evaluate_validation(argparse.Namespace(
            plan=plan, sample=disclosure, long=tmp_path / "not-needed.tsv",
            long_records=None))
