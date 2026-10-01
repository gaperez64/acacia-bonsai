"""Synthetic P4 recycle, join, lifting, and campaign-manifest checks."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import os
import pathlib
import subprocess
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
orchestrate = load("p4_orchestrate", "benchmarking/tools/campaign-orchestrate.py")
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
    write_observations(short, [observation("a", result)])
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


def test_orchestrator_manifest_resume_and_frozen_binary(tmp_path, monkeypatch):
    binary = tmp_path / "solver"
    binary.write_text("#!/bin/sh\nexit 0\n")
    binary.chmod(0o755)
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    (tmp_path / "all.list").write_text("a\n")
    (tmp_path / "map.tsv").write_text("instance\ttlsf\na\ta.tlsf\n")
    (tmp_path / "exceptions.tsv").write_text(
        "instance\tannotated_status\tcorrected_status\tevidence\n")
    runtime = tmp_path / "old-runtime"
    runtime.mkdir()
    sums = []
    for name in ("libspot", "libbddx"):
        target = runtime / f"{name}.so.0.0.0"
        target.write_text(name)
        (runtime / f"{name}.so.0").symlink_to(target.name)
        sums.append(f"{orchestrate.sha256_file(target)}  ./lib/{target.name}\n")
    sums_file = tmp_path / "SHA256SUMS"
    sums_file.write_text("".join(sums))
    monkeypatch.setattr(orchestrate, "RUNTIME_SHA256SUMS", sums_file)
    comparison = tmp_path / "comparison.json"
    spec = {"schema": 1, "memory_max": "8G", "memory_swap_max": "0",
            "list": "all.list", "tlsf_map": "map.tsv", "tlsf_corpus": ".",
            "status_exceptions": "exceptions.tsv",
            "series": [{"label": "leg", "kind": "arm", "arms": ["real:one:backend"],
                        "portfolio_size": 1,
                        "binary": "solver", "binary_sha256": orchestrate.sha256_file(binary),
                        "source_revision": "source", "runtime_lib_dir": "old-runtime",
                        "pre_spot_216": True},
                       {"label": "race", "kind": "race",
                        "arms": ["real:one:backend", "unreal:two:backend"],
                        "portfolio_size": 2,
                        "binary": "solver", "binary_sha256": orchestrate.sha256_file(binary),
                        "source_revision": "source"}]}
    comparison.write_text(json.dumps(spec))
    out = tmp_path / "out"
    frozen, commands = orchestrate.prepare(comparison, 17, out, dry_run=True)
    assert frozen["series"]["leg"]["runtime_identity"]["libraries"]["libspot.so.0"] == \
        orchestrate.sha256_file(runtime / "libspot.so.0")
    assert "unverified" in frozen["series"]["leg"]["runtime_identity"]["verification"]
    assert "unverified" in frozen["series"]["leg"]["pre_spot_216_verification"]
    assert len(commands) == 2 and not out.exists()
    assert "--binary-identity" in commands[0][0]
    assert "host sample accuracy are unverified" in frozen["verification"]
    orchestrate.prepare(comparison, 17, out)
    assert (out / "manifest.json").exists()
    with pytest.raises(orchestrate.CampaignError, match="resume"):
        orchestrate.prepare(comparison, 17, out)
    assert orchestrate.prepare(comparison, 17, out, resume=True)[0] == frozen
    wrapper = out / "wrappers/leg.sh"
    original_wrapper = wrapper.read_text()
    wrapper.write_text(original_wrapper + "# forged\n")
    with pytest.raises(orchestrate.CampaignError, match="wrapper changed"):
        orchestrate.prepare(comparison, 17, out, resume=True)
    wrapper.write_text(original_wrapper)
    runner = tmp_path / "runner.py"
    runner.write_text("# synthetic runner\n")
    monkeypatch.setattr(orchestrate, "NATIVE", runner)
    orchestrate.prepare(comparison, 17, tmp_path / "runner-out")
    runner.write_text("# forged synthetic runner\n")
    with pytest.raises(orchestrate.CampaignError, match="resume"):
        orchestrate.prepare(comparison, 17, tmp_path / "runner-out", resume=True)
    spec["series"][0]["runtime_lib_dir"] = "missing"
    comparison.write_text(json.dumps(spec))
    with pytest.raises(orchestrate.CampaignError, match="runtime_lib_dir"):
        orchestrate.prepare(comparison, 17, out, resume=True)
    (tmp_path / "empty-runtime").mkdir()
    spec["series"][0]["runtime_lib_dir"] = "empty-runtime"
    comparison.write_text(json.dumps(spec))
    with pytest.raises(orchestrate.CampaignError, match="preserved runtime missing"):
        orchestrate.prepare(comparison, 17, out, resume=True)
    spec["series"][0]["runtime_lib_dir"] = "old-runtime"
    comparison.write_text(json.dumps(spec))
    (runtime / "libspot.so.0.0.0").write_text("wrong")
    with pytest.raises(orchestrate.CampaignError, match="SHA-256"):
        orchestrate.prepare(comparison, 17, out, resume=True)
    (runtime / "libspot.so.0.0.0").write_text("libspot")
    binary.write_text("#!/bin/sh\nexit 1\n")
    with pytest.raises(orchestrate.CampaignError, match="SHA-256"):
        orchestrate.prepare(comparison, 17, out, resume=True)


@pytest.mark.parametrize("tool", ["acacia1x", "ltlsynt"])
def test_converted_pairs_and_syfco_are_pinned_for_resume(tmp_path, monkeypatch, tool):
    binary, syfco = tmp_path / "solver", tmp_path / "syfco"
    for path in (binary, syfco):
        path.write_text("#!/bin/sh\nexit 0\n")
        path.chmod(0o755)
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    (tmp_path / "all.list").write_text("a.ltl\n")
    (tmp_path / "map.tsv").write_text("instance\ttlsf\na.ltl\ta.tlsf\n")
    (tmp_path / "exceptions.tsv").write_text(
        "instance\tannotated_status\tcorrected_status\tevidence\n")
    pairs = tmp_path / "pairs"
    pairs.mkdir()
    (pairs / "a.ltl").write_text("G true\n")
    (pairs / "a.part").write_text("INPUTS: x\nOUTPUTS: y\n")
    (tmp_path / "failures.list").write_text("")
    (tmp_path / "semantics.tsv").write_text("instance\tsemantics\na.ltl\tMealy\n")
    runtime = tmp_path / "runtime"
    runtime.mkdir()
    sums = []
    for name in ("libspot", "libbddx"):
        target = runtime / f"{name}.so.0.0.0"
        target.write_text(name)
        (runtime / f"{name}.so.0").symlink_to(target.name)
        sums.append(f"{orchestrate.sha256_file(target)}  ./lib/{target.name}\n")
    sums_file = tmp_path / "SHA256SUMS"
    sums_file.write_text("".join(sums))
    monkeypatch.setattr(orchestrate, "RUNTIME_SHA256SUMS", sums_file)
    comparison = tmp_path / "comparison.json"
    spec = {"schema": 1, "memory_max": "8G", "memory_swap_max": "0",
            "list": "all.list", "tlsf_map": "map.tsv", "tlsf_corpus": ".",
            "status_exceptions": "exceptions.tsv", "series": [
                {"label": "legacy", "kind": "legacy", "tool": tool,
                 "portfolio_size": None,
                 "binary": "solver", "binary_sha256": orchestrate.sha256_file(binary),
                 "source_revision": "source", "runtime_lib_dir": "runtime",
                 "instances_dir": "pairs", "syfco_failures": "failures.list",
                 "syfco": str(syfco),
                 **({"semantics_map": "semantics.tsv"} if tool == "ltlsynt" else {})}]}
    comparison.write_text(json.dumps(spec))
    out = tmp_path / "out"
    frozen, commands = orchestrate.prepare(comparison, 17, out)
    entry = frozen["series"]["legacy"]
    assert entry["syfco_sha256"] == orchestrate.sha256_file(syfco)
    assert entry["converted_pairs"]["a.ltl"]["input"]["sha256"] == \
        orchestrate.sha256_file(pairs / "a.ltl")
    assert "unverified" in entry["conversion_verification"]
    assert ["--syfco", str(syfco)] == commands[0][0][
        commands[0][0].index("--syfco"):commands[0][0].index("--syfco") + 2]
    if tool == "ltlsynt":
        assert entry["semantics_map_sha256"] == \
            orchestrate.sha256_file(tmp_path / "semantics.tsv")
        assert "--semantics-map" in commands[0][0]
    assert orchestrate.prepare(comparison, 17, out, resume=True)[0] == frozen
    if tool == "ltlsynt":
        semantics = tmp_path / "semantics.tsv"
        semantics.write_text("instance\tsemantics\na.ltl\tMoore\n")
        with pytest.raises(orchestrate.CampaignError, match="resume"):
            orchestrate.prepare(comparison, 17, out, resume=True)
        semantics.write_text("instance\tsemantics\na.ltl\tMealy\n")
    (pairs / "a.ltl").write_text("F false\n")
    with pytest.raises(orchestrate.CampaignError, match="resume"):
        orchestrate.prepare(comparison, 17, out, resume=True)
    (pairs / "a.ltl").write_text("G true\n")
    syfco.write_text("#!/bin/sh\nexit 1\n")
    with pytest.raises(orchestrate.CampaignError, match="resume"):
        orchestrate.prepare(comparison, 17, out, resume=True)


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
    with pytest.raises(orchestrate.CampaignError, match="positive integer"):
        orchestrate.read_comparison(tmp_path / "unused.json", -1)


def test_cap_gate_default_matches_head_stdout_and_exit(tmp_path):
    old = ROOT / "benchmarking/tools/check-cap-independence-head.py"
    new = ROOT / "benchmarking/tools/check-cap-independence.py"
    short, long = tmp_path / "short.tsv", tmp_path / "long.tsv"
    short_root, long_root = tmp_path / "short-records", tmp_path / "long-records"
    arm = capcheck.ARM
    for changed in (False, True):
        short_root = tmp_path / f"short-records-{changed}"
        long_root = tmp_path / f"long-records-{changed}"
        first = observation("a", "REALIZABLE")
        second = observation("a", "REALIZABLE", cap=60)
        write_observations(short, [first])
        write_observations(long, [second])
        phase_dir(short_root, first, [{"arm": arm, "phase": "lift_method_x",
                                      "work_count": 3},
                                     {"arm": arm, "phase": "record_summary",
                                      "dropped_records": 0}])
        events = [{"arm": arm, "phase": "lift_method_y" if changed
                   else "lift_method_x", "work_count": 4 if changed else 3}]
        if not changed:
            events.append({"arm": arm, "phase": "record_summary", "dropped_records": 0})
        phase_dir(long_root, second, events)
        argv = ["--short", str(short), "--long", str(long),
                "--short-records", str(short_root), "--long-records", str(long_root)]
        results = [subprocess.run([sys.executable, "-s", str(script), *argv],
                                  capture_output=True, text=True, check=False)
                   for script in (old, new)]
        assert [(p.returncode, p.stdout) for p in results][:1] == \
            [(results[1].returncode, results[1].stdout)]
        assert results[0].returncode == (1 if changed else 0)


@pytest.mark.parametrize("building_name", ["DRIVER-BUILDING", "TRACKB-BUILDING"])
def test_campaign_marker_wait_collision_release_and_execute(tmp_path, building_name):
    from benchlib import timed_run_marker

    marker = tmp_path / "TIMED-RUN-ACTIVE"
    building = tmp_path / building_name
    building.write_text("build")
    waits = []

    def clear_build(_seconds):
        waits.append("wait")
        building.unlink()

    with timed_run_marker(tmp_path, "test", poll=0.01, max_wait=0.1,
                          sleep=clear_build):
        assert marker.exists() and waits
    assert not marker.exists()
    marker.write_text("other")

    def clear_collision(_seconds):
        marker.unlink()

    with timed_run_marker(tmp_path, "test", blocking=(), poll=0.01,
                          max_wait=0.1, sleep=clear_collision):
        assert marker.exists()
    assert not marker.exists()
    (tmp_path / "out").mkdir()
    frozen = {"series": {"x": {"label": "x"}}, "cap_s": 17, "instances": 1}

    def fake_run(_cmd, **_kwargs):
        assert marker.exists()
        write_observations(tmp_path / "out/x-17s.tsv", [observation("a", "REALIZABLE")])
        return argparse.Namespace(returncode=0)

    orchestrate.execute(frozen, [(["fake"], tmp_path / "out/x-17s.tsv")],
                        tmp_path / "out", invoke=fake_run, marker_dir=tmp_path)
    assert not marker.exists()
    with pytest.raises(orchestrate.CampaignError):
        orchestrate.execute(frozen, [(["fake"], tmp_path / "out/x-17s.tsv")],
                            tmp_path / "out", invoke=lambda *_a, **_k:
                            argparse.Namespace(returncode=1), marker_dir=tmp_path)
    assert not marker.exists()


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
    fixture = ROOT / "build_scratch/p4tools/review-r2-native-flag"
    first = coverage.load_output(fixture / "short.tsv")[0]
    assert first["flags"] == "--arms=real:small:backward"
    assert recycle.arms_from_flags(first["flags"]) == ["real:small:backward"]
    prefix = tmp_path / "plan"
    recycle.plan(argparse.Namespace(short=fixture / "short.tsv", list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=None,
                                    deterministic_error_list=None, out_prefix=prefix))
    plan = tmp_path / "plan-plan.json"
    assert json.loads(plan.read_text())["leg_kind"] == "native"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1,
                                      out_prefix=tmp_path / "draw"))
    with pytest.raises(recycle.RecycleError, match="requires short phase records"):
        recycle.validate(argparse.Namespace(plan=plan, sample=tmp_path / "draw-sample.json",
                                          long=fixture / "sample.tsv", long_records=None,
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


def test_review_manifest_subset_cannot_cover_full_observation(tmp_path):
    fixture = ROOT / "build_scratch/p4tools/review-r2-manifest-subset"
    manifest = tmp_path / "manifest.json"
    frozen = json.loads((fixture / "manifest.json").read_text())
    frozen["cap_s"] = 17
    manifest.write_text(json.dumps(frozen))
    args = argparse.Namespace(list=fixture / "full.list", cap=17,
                              series=[f"old={fixture / 'old.tsv'}",
                                      f"new={fixture / 'new.tsv'}"], internal=[], new="new",
                              legacy_audit=[], untimed_conversion=[],
                              tlsf_map=fixture / "map.tsv", tlsf_corpus=fixture,
                              status_exceptions=fixture / "exceptions.tsv",
                              noise_floor=None, noise_floor_source="",
                              manifest=[manifest], out=tmp_path)
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
        sums.append(f"{orchestrate.sha256_file(target)}  ./lib/{target.name}\n")
    sums_file = tmp_path / "SHA256SUMS"
    sums_file.write_text("".join(sums))
    monkeypatch.setattr(orchestrate, "RUNTIME_SHA256SUMS", sums_file)
    five = ["real:small:backward", "real:small:forward", "unreal:formula:forward",
            "unreal:automaton:forward", "real:gr1:oxidd"]
    six = five + ["unreal:gr1:oxidd"]
    options = tmp_path / "intro-buildoptions.json"
    options.write_text(json.dumps([{"name": "acacia_default_arms", "value": ",".join(six)}]))
    shared = {"binary": "solver", "binary_sha256": orchestrate.sha256_file(binary),
              "source_revision": "source"}
    spec = {"schema": 1, "memory_max": "8G", "memory_swap_max": "0",
            "list": "all.list", "tlsf_map": "map.tsv", "tlsf_corpus": ".",
            "status_exceptions": "exceptions.tsv", "series": [
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
                 "preset": "otf_sparse_formula_gr1_lift", "arms": six,
                 "use_default_arms": True, "build_options": options.name}]}
    comparison = tmp_path / "comparison.json"
    comparison.write_text(json.dumps(spec))
    return comparison, spec, options


def test_mixed_portfolio_sizes_and_preset_default_arms(tmp_path, monkeypatch):
    comparison, spec, options = mixed_comparison_fixture(tmp_path, monkeypatch)
    frozen, commands = orchestrate.prepare(comparison, 17, tmp_path / "out", dry_run=True)
    assert [frozen["series"][label]["portfolio_size"] for label in
            ("ltlsynt", "TACAS23", "E5", "final")] == [None, None, 5, 6]
    e5_flags = commands[2][0][commands[2][0].index("--flags") + 1]
    final_flags = commands[3][0][commands[3][0].index("--flags") + 1]
    assert e5_flags.startswith("--arms ") and final_flags == ""
    assert frozen["series"]["final"]["build_options_sha256"] == \
        orchestrate.sha256_file(options)
    assert not (tmp_path / "out").exists()
    assert "portfolio_size" not in spec


def test_mixed_campaign_recycle_and_derived_join(tmp_path, monkeypatch):
    comparison, _, _ = mixed_comparison_fixture(tmp_path, monkeypatch)
    sums = tmp_path / "SHA256SUMS"
    monkeypatch.setattr(recycle.campaign, "RUNTIME_SHA256SUMS", sums)
    monkeypatch.setattr(threeway.campaign, "RUNTIME_SHA256SUMS", sums)
    (tmp_path / "all.list").write_text("a.ltl\nb.ltl\n")
    (tmp_path / "b.tlsf").write_text("// STATUS: REALIZABLE\n")
    (tmp_path / "map.tsv").write_text(
        "instance\ttlsf\na.ltl\ta.tlsf\nb.ltl\tb.tlsf\n")
    (tmp_path / "pairs/b.ltl").write_text("G true\n")
    (tmp_path / "pairs/b.part").write_text("INPUTS: x\nOUTPUTS: y\n")
    (tmp_path / "semantics.tsv").write_text(
        "instance\tsemantics\na.ltl\tMealy\nb.ltl\tMealy\n")

    short_dir = tmp_path / "short"
    short_manifest, short_commands = orchestrate.prepare(comparison, 17, short_dir)
    labels = ("ltlsynt", "TACAS23", "E5", "final")
    assert tuple(short_manifest["series"]) == labels
    plans = tmp_path / "plans"
    plans.mkdir()
    derived_dir = tmp_path / "derived"
    derived_dir.mkdir()
    long_manifests = []
    derived_paths = {}

    for label, (short_command, short_path) in zip(labels, short_commands):
        entry = short_manifest["series"][label]
        native = entry["kind"] != "legacy"
        flags = short_command[short_command.index("--flags") + 1]
        assert flags == (orchestrate.native_flags(entry) if native else entry["flags"])

        def row(name, result, cap):
            item = observation(name, result, cap=cap, label=label,
                               seconds="17" if result == "TIMEOUT" else "1")
            item.update(binary_sha256=entry["binary_sha256"],
                        acacia_sha=entry["source_revision"], flags=flags,
                        preset=entry["preset"] if native else entry["tool"],
                        tlsf_file=name.replace(".ltl", ".tlsf") if native else name,
                        expectation_source="status" if native else "none")
            return item

        short_rows = [row("a.ltl", "REALIZABLE", 17), row("b.ltl", "TIMEOUT", 17)]
        write_observations(short_path, short_rows)
        records = short_dir / "phase-records" if native else None
        if native:
            events = [{"arm": entry["arms"][0], "phase": "lift_method_x", "work_count": 3},
                      {"arm": entry["arms"][0], "phase": "record_summary",
                       "dropped_records": 0}]
            phase_dir(records, short_rows[0], events)
        prefix = plans / label
        assert recycle.plan(argparse.Namespace(
            short=short_path, manifest=short_dir / "manifest.json", list=None,
            long_cap=60, deadline_margin=1, deadline_stage_regex="deadline",
            memory_max="8G", memory_swap_max="0", records=records,
            deterministic_error_list=None, out_prefix=prefix)) == 0
        plan = plans / f"{label}-plan.json"
        rerun_list = plans / f"{label}-rerun.list"
        assert rerun_list.read_text() == "b.ltl\n"
        assert json.loads(plan.read_text())["arms"] == entry["arms"]

        long_dir = tmp_path / f"long-{label}"
        long_manifest, long_commands = orchestrate.prepare(
            comparison, 60, long_dir, selected=[label], subset_list=rerun_list)
        long_manifests.append(long_dir / "manifest.json")
        long_flags = long_commands[0][0][long_commands[0][0].index("--flags") + 1]
        assert long_flags == flags
        if label == "final":
            assert flags == "" and len(entry["arms"]) == 6
        if label == "E5":
            assert flags.startswith("--arms ") and len(entry["arms"]) == 5
        write_observations(long_commands[0][1], [row("b.ltl", "REALIZABLE", 60)])

        assert recycle.sample(argparse.Namespace(
            plan=plan, seed=7, size=1, out_prefix=plans / f"{label}-sample")) == 0
        sample_dir = tmp_path / f"sample-{label}"
        _, sample_commands = orchestrate.prepare(
            comparison, 60, sample_dir, selected=[label],
            subset_list=plans / f"{label}-sample-sample.list")
        assert sample_commands[0][0][sample_commands[0][0].index("--flags") + 1] == flags
        sample_row = row("a.ltl", "REALIZABLE", 60)
        write_observations(sample_commands[0][1], [sample_row])
        sample_records = sample_dir / "phase-records" if native else None
        if native:
            phase_dir(sample_records, sample_row, events)
        validation = plans / f"{label}-validation.json"
        assert recycle.validate(argparse.Namespace(
            plan=plan, sample=plans / f"{label}-sample-sample.json",
            long=sample_commands[0][1], long_records=sample_records,
            out=validation)) == 0
        derived_paths[label] = derived_dir / f"{label}-60s.tsv"
        assert recycle.merge(argparse.Namespace(
            plan=plan, long=long_commands[0][1], validation=validation,
            output=derived_paths[label])) == 0
        assert [item["provenance"] for item in coverage.load_output(derived_paths[label])] == \
            ["recycled-17s", "measured-60s"]
        assert long_manifest["series"][label]["portfolio_size"] == entry["portfolio_size"]

    args = argparse.Namespace(
        list=tmp_path / "all.list", cap=60,
        series=[f"{label}={derived_paths[label]}" for label in
                ("ltlsynt", "TACAS23", "final")],
        internal=[f"E5={derived_paths['E5']}"], new="final",
        legacy_audit=[], untimed_conversion=[], tlsf_map=tmp_path / "map.tsv",
        tlsf_corpus=tmp_path, status_exceptions=tmp_path / "exceptions.tsv",
        noise_floor=None, noise_floor_source="",
        manifest=[short_dir / "manifest.json", *long_manifests])
    report, _ = threeway.join(args)
    assert set(report["series"]) == set(labels)
    assert all(report["series"][label]["derived"] and
               report["series"][label]["recycled_rows"] == 1 and
               len(report["series"][label]["manifest_identity"]) == 2 for label in labels)


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
    with pytest.raises(orchestrate.CampaignError, match=expected):
        orchestrate.read_comparison(comparison, 17)


def test_capless_review_reproduction_is_rejected(tmp_path):
    fixture = ROOT / "build_scratch/p4tools/review-r2-manifest-subset"
    capless = ROOT / "build_scratch/p4tools/review-r3-capless-manifest/manifest.json"
    args = argparse.Namespace(list=fixture / "full.list", cap=17,
                              series=[f"old={fixture / 'old.tsv'}",
                                      f"new={fixture / 'new.tsv'}"], internal=[], new="new",
                              legacy_audit=[], untimed_conversion=[],
                              tlsf_map=fixture / "map.tsv", tlsf_corpus=fixture,
                              status_exceptions=fixture / "exceptions.tsv",
                              noise_floor=None, noise_floor_source="", manifest=[capless],
                              out=tmp_path)
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


def test_full_manifest_rebuild_rejects_forged_binary_and_revision(tmp_path):
    binary = tmp_path / "solver"
    binary.write_text("#!/bin/sh\nexit 0\n")
    binary.chmod(0o755)
    (tmp_path / "a.tlsf").write_text("// STATUS: REALIZABLE\n")
    listing = tmp_path / "all.list"
    listing.write_text("a\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\na\ta.tlsf\n")
    exceptions = tmp_path / "exceptions.tsv"
    exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n")
    comparison = tmp_path / "comparison.json"
    comparison.write_text(json.dumps({
        "schema": 1, "memory_max": "8G", "memory_swap_max": "0", "list": "all.list",
        "tlsf_map": "map.tsv", "tlsf_corpus": ".", "status_exceptions": "exceptions.tsv",
            "series": [{"label": "new", "kind": "arm", "arms": ["synthetic"],
                        "portfolio_size": 1,
                        "binary": "solver", "binary_sha256": orchestrate.sha256_file(binary),
                    "source_revision": "source"}]}))
    out = tmp_path / "campaign"
    orchestrate.prepare(comparison, 17, out)
    manifest = out / "manifest.json"
    old = tmp_path / "old.tsv"
    write_observations(old, [observation("a", "REALIZABLE", label="old")])
    new = out / "new-17s.tsv"
    original_row = observation("a", "REALIZABLE", label="new")
    original_row["binary_sha256"] = orchestrate.sha256_file(binary)
    original_row["preset"] = ""
    original_row["expectation_source"] = "status"
    write_observations(new, [original_row])
    args = argparse.Namespace(list=listing, cap=17, series=[f"old={old}", f"new={new}"],
                              internal=[], new="new", legacy_audit=[], untimed_conversion=[],
                              tlsf_map=mapping, tlsf_corpus=tmp_path,
                              status_exceptions=exceptions, noise_floor=None,
                              noise_floor_source="", manifest=[manifest])
    report, _ = threeway.join(args)
    assert report["series"]["new"]["manifest_identity"][0]["verification"].startswith(
        "campaign specification")
    assert "unverified" in report["verification"]
    assert "unverified" in json.loads(manifest.read_text())["series"]["new"][
        "source_revision_verification"]
    alternate_exceptions = tmp_path / "alternate-exceptions.tsv"
    alternate_exceptions.write_text(exceptions.read_text() + "\n")
    args.status_exceptions = alternate_exceptions
    with pytest.raises(threeway.JoinError, match="join inputs differ"):
        threeway.join(args)
    args.status_exceptions = exceptions
    alternate_map = tmp_path / "alternate-map.tsv"
    alternate_map.write_text(mapping.read_text() + "\n")
    args.tlsf_map = alternate_map
    with pytest.raises(threeway.JoinError, match="join inputs differ"):
        threeway.join(args)
    args.tlsf_map = mapping
    for field, forged in (("flags", "--arms other"), ("preset", "other"),
                          ("memory_max", "4G"), ("memory_swap_max", "1G")):
        write_observations(new, [dict(original_row, **{field: forged})])
        with pytest.raises(threeway.JoinError, match="treatment differs"):
            threeway.join(args)
    for field, forged in (("tlsf_file", "other.tlsf"),
                          ("expectation_source", "exception")):
        write_observations(new, [dict(original_row, **{field: forged})])
        with pytest.raises(threeway.JoinError, match="TLSF or expected-status label"):
            threeway.join(args)
    write_observations(new, [original_row])
    for field, forged in (("binary_sha256", "b" * 64), ("source_revision", "forged")):
        row = dict(original_row)
        if field == "binary_sha256":
            row["binary_sha256"] = forged
        else:
            row["acacia_sha"] = forged
        write_observations(new, [row])
        record = json.loads(manifest.read_text())
        record["series"]["new"][field] = forged
        manifest.write_text(json.dumps(record))
        with pytest.raises(threeway.JoinError, match="campaign manifest differs"):
            threeway.join(args)
        original, _ = orchestrate.read_comparison(comparison, 17)
        manifest.write_text(json.dumps(original))
    forged_manifest = json.loads(manifest.read_text())
    forged_manifest["series"]["new"]["arms"] = ["other"]
    manifest.write_text(json.dumps(forged_manifest))
    write_observations(new, [dict(original_row, flags="--arms other")])
    with pytest.raises(recycle.RecycleError, match="campaign manifest differs"):
        recycle.plan(argparse.Namespace(short=new, manifest=manifest, leg_kind=None,
                                        list=None, long_cap=60, deadline_margin=1,
                                        deadline_stage_regex="deadline", memory_max="8G",
                                        memory_swap_max="0", records=None,
                                        deterministic_error_list=None,
                                        out_prefix=tmp_path / "forged-plan"))
    original, _ = orchestrate.read_comparison(comparison, 17)
    forged_output = dict(original_row, binary_sha256="c" * 64)

    def fake_runner(_cmd, **_kwargs):
        write_observations(new, [forged_output])
        return argparse.Namespace(returncode=0)

    with pytest.raises(orchestrate.CampaignError, match="observation identity differs"):
        orchestrate.execute(original, [(["fake"], new)], out, invoke=fake_runner,
                            marker_dir=tmp_path)
