"""Offline adjudication gates, replay and disclosure on synthetic recorded races."""

import argparse
import csv
import importlib.util
import json
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "adjudication_phase_helpers", ROOT / "tests/pytest/test_phase_record_comparison.py")
phase = importlib.util.module_from_spec(spec)
spec.loader.exec_module(phase)
helpers = phase.helpers
recycle = helpers.recycle
capcheck = helpers.capcheck
from recycle_adjudication import COLUMNS, adjudicate, sha256_file  # noqa: E402


def write_decision(path, row):
    with path.open("w") as stream:
        writer = csv.DictWriter(stream, COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerow({**row, "evidence": json.dumps(row["evidence"])})


@pytest.fixture
def panel(tmp_path):
    short = helpers.observation("input", "REALIZABLE")
    short["flags"] = "--arms real:small:backward,real:small:forward"
    long = {**short, "cap_s": "60"}
    first, second = tmp_path / "first", tmp_path / "second"
    phase.current_records(first, short)
    directory, files = phase.current_records(second, long, pid=200, clock=2000)
    terminal = next(e for e in files["199"] if e.get("event") == "parent_terminal")
    terminal["reason"] = "winner_cancelled"
    phase.write_current(directory, files)
    short_path, long_path = tmp_path / "short.tsv", tmp_path / "long.tsv"
    helpers.write_observations(short_path, [short])
    helpers.write_observations(long_path, [long])
    recycle.plan(argparse.Namespace(
        short=short_path, list=None, long_cap=60, deadline_margin=1,
        deadline_stage_regex="deadline", memory_max="8G", memory_swap_max="0",
        records=first, deterministic_error_list=None, out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1,
                                     out_prefix=tmp_path / "sample"))
    args = argparse.Namespace(plan=plan, sample=tmp_path / "sample-sample.json",
                              long=long_path, long_records=second, out=tmp_path / "verdict.json")
    strict, _ = recycle.evaluate_validation(args)
    assert len(strict["failures"]) == 1 and strict["failures"][0]["kind"] == "decision"
    support = tmp_path / "repetitions.json"
    support.write_text('{"recorded_repetitions": []}\n')
    sources = [support, short_path, long_path, *first.rglob("*.jsonl"),
               *second.rglob("*.jsonl")]
    row = {"series": "candidate", "instance": "input",
           "finding_class": "same-verdict-winner-variation",
           "mismatch": strict["failures"][0]["message"], "decision": "driver decision",
           "rationale": "Recorded same-cap winners vary with identical outcomes.",
           "evidence": [{"path": str(p), "sha256": sha256_file(p)} for p in sources]}
    args.adjudications = tmp_path / "adjudications.tsv"
    write_decision(args.adjudications, row)
    return args, row, short, long, support


def test_exact_match_validates_and_merge_retains_decision(panel, tmp_path, capsys):
    args, row, *_ = panel
    assert recycle.validate(args) == 0
    output = capsys.readouterr().out
    assert "## Adjudicated recycling mismatches" in output
    assert row["finding_class"] in output and row["rationale"] in output
    verdict = json.loads(args.out.read_text())
    assert verdict["status"] == "pass" and verdict["failures"] == []
    assert len(verdict["adjudicated_mismatches"]) == 1
    accepted = verdict["adjudicated_mismatches"][0]
    assert all(accepted[key] == value for key, value in row.items())
    assert accepted["outcome"]["result"] == "REALIZABLE"
    assert verdict["adjudications_sha256"] == sha256_file(args.adjudications)
    merge = argparse.Namespace(plan=args.plan, long=tmp_path / "plan-empty-long.tsv",
                               validation=args.out, output=tmp_path / "derived.tsv")
    assert recycle.merge(merge) == 0
    provenance = json.loads((tmp_path / "derived-provenance.json").read_text())
    assert provenance["adjudicated_mismatches"] == [accepted]


@pytest.mark.parametrize("field", ["mismatch", "instance", "series"])
def test_different_binding_keeps_mismatch(panel, field):
    args, row, *_ = panel
    row[field] += " changed"
    write_decision(args.adjudications, row)
    assert recycle.validate(args) == 1
    verdict = json.loads(args.out.read_text())
    assert verdict["adjudicated_mismatches"] == []
    assert verdict["failures"][0]["kind"] == "decision"


@pytest.mark.parametrize("change", ["missing-file", "changed-file", "wrong-hash", "empty",
                                    "missing-tsv-binding", "missing-phase-binding"])
def test_missing_or_changed_evidence_fails(panel, change):
    args, row, _, _, support = panel
    if change == "missing-file":
        support.unlink()
    elif change == "changed-file":
        support.write_text("changed repetitions\n")
    elif change == "wrong-hash":
        row["evidence"][0]["sha256"] = "0" * 64
    elif change == "empty":
        row["evidence"] = []
    else:
        suffix = ".tsv" if change == "missing-tsv-binding" else ".jsonl"
        row["evidence"].remove(next(e for e in row["evidence"] if e["path"].endswith(suffix)))
    write_decision(args.adjudications, row)
    with pytest.raises(recycle.RecycleError, match="adjudication rejected"):
        recycle.validate(args)


@pytest.mark.parametrize("field,value", [("result", "UNREALIZABLE"), ("exit_code", "1"),
                                         ("timed_out", "true"), ("resource_reason", "oom")])
def test_outcome_difference_with_exact_row_is_never_adjudicated(panel, field, value):
    args, row, short, long, _ = panel
    long[field] = value
    strict = capcheck.compare({"input": short}, {"input": long},
                             args.plan.parent / "first", args.long_records,
                             allow_race_truncation=True)[1]
    # Rebind evidence to the changed TSV, isolating the outcome gate from hash rejection.
    helpers.write_observations(args.long, [long])
    for item in row["evidence"]:
        item["sha256"] = sha256_file(item["path"])
    write_decision(args.adjudications, row)
    remaining, accepted = adjudicate(args.adjudications, "candidate", strict,
                                    {"input": short}, {"input": long})
    assert accepted == [] and remaining == strict
    assert any(f["kind"] == "outcome" for f in remaining)
    assert any(f["message"] == row["mismatch"] for f in remaining)
    assert recycle.validate(args) == 1
    assert json.loads(args.out.read_text())["adjudicated_mismatches"] == []


@pytest.mark.parametrize("kind", ["outcome", "work", "decline", "completion", "panel", "configuration", "proof_binding"])
def test_other_finding_kinds_remain_strict_even_with_exact_text(panel, kind):
    args, row, short, long, _ = panel
    failure = {"instance": "input", "kind": kind, "arm": None, "message": row["mismatch"]}
    remaining, accepted = adjudicate(args.adjudications, "candidate", [failure],
                                    {"input": short}, {"input": long})
    assert remaining == [failure] and accepted == []


def test_dropped_records_class_is_accepted_with_identical_outcomes(panel):
    args, row, short, long, _ = panel
    directory = recycle.phase_record_dir(args.long_records, "candidate", 60, "input", 0)
    path = directory / "199.jsonl"
    events = [json.loads(line) for line in path.read_text().splitlines()]
    terminals = [e for e in events if e.get("event") == "parent_terminal"]
    terminals[0]["reason"] = "exit"
    terminals[1].update(dropped_records=2, telemetry="dropped", seq=5)
    path.write_text("".join(json.dumps(e) + "\n" for e in events))
    failure = capcheck.compare({"input": short}, {"input": long},
                              args.plan.parent / "first", args.long_records,
                              allow_race_truncation=True)[1][0]
    row.update(finding_class="telemetry-dropped", mismatch=failure["message"])
    for item in row["evidence"]:
        item["sha256"] = sha256_file(item["path"])
    write_decision(args.adjudications, row)
    assert recycle.validate(args) == 0
    assert json.loads(args.out.read_text())["adjudicated_mismatches"][0]["kind"] == "records"


@pytest.mark.parametrize("change", ["evidence", "rationale", "table-deleted"])
def test_merge_replays_evidence_and_decision(panel, tmp_path, change):
    args, row, _, _, support = panel
    assert recycle.validate(args) == 0
    if change == "evidence":
        support.write_text("changed\n")
    elif change == "rationale":
        row["rationale"] += " changed"
        write_decision(args.adjudications, row)
    else:
        args.adjudications.unlink()
    with pytest.raises(recycle.RecycleError):
        recycle.merge(argparse.Namespace(plan=args.plan, long=tmp_path / "plan-empty-long.tsv",
                                         validation=args.out, output=tmp_path / "stale.tsv"))
    assert not (tmp_path / "stale.tsv").exists()


def test_independent_cli_uses_the_same_gate(panel, monkeypatch, capsys):
    args, row, *_ = panel
    argv = ["check-cap-independence.py", "--short", str(args.plan.parent / "short.tsv"),
            "--long", str(args.long), "--short-records", str(args.plan.parent / "first"),
            "--long-records", str(args.long_records), "--all-arms", "--all-rows", "--subset",
            "--allow-race-truncation", "--adjudications", str(args.adjudications),
            "--series", "candidate"]
    monkeypatch.setattr(sys, "argv", argv)
    capcheck.main()
    output = capsys.readouterr().out
    assert row["rationale"] in output and row["finding_class"] in output
    row["mismatch"] += " changed"
    write_decision(args.adjudications, row)
    with pytest.raises(SystemExit) as error:
        capcheck.main()
    assert error.value.code == 1


@pytest.mark.parametrize("variation", ["lifecycle", "drop"])
@pytest.mark.parametrize("strict_change,kind", [
    ("work", "work"), ("configuration", "configuration"),
    ("proof", "proof_binding"), ("outcome", "outcome"),
])
def test_adjudication_preserves_accompanying_strict_differences(
        panel, tmp_path, variation, strict_change, kind):
    args, row, *_ = panel
    directory = recycle.phase_record_dir(args.long_records, "candidate", 60, "input", 0)
    files = {p.stem: [json.loads(line) for line in p.read_text().splitlines()]
             for p in directory.glob("*.jsonl") if p.stem != "writer"}
    if variation == "drop":
        terminals = [e for e in files["199"] if e.get("event") == "parent_terminal"]
        terminals[0]["reason"] = "exit"
        terminals[1].update(dropped_records=2, telemetry="dropped", seq=5)
        row["finding_class"] = "telemetry-dropped"
    if strict_change == "work":
        next(e for e in files["200"] if e.get("key") == "search_queries")["value"] = "99"
    elif strict_change == "configuration":
        next(e for e in files["199"] if e.get("event") == "worker_spec")["transform"] = "dual"
    else:
        terminal = next(e for e in files["200"] if e.get("event") == "terminal_result")
        terminal["proof_sha256" if strict_change == "proof" else "exit_code"] = (
            "changed-proof" if strict_change == "proof" else 1)
    phase.write_current(directory, files)
    del args.adjudications
    strict, _ = recycle.evaluate_validation(args)
    eligible_kind = "records" if variation == "drop" else "decision"
    eligible = next(f for f in strict["failures"] if f["kind"] == eligible_kind)
    row["mismatch"] = eligible["message"]
    for item in row["evidence"]:
        item["sha256"] = sha256_file(item["path"])
    args.adjudications = tmp_path / "adjudications.tsv"
    write_decision(args.adjudications, row)
    assert recycle.validate(args) == 1
    verdict = json.loads(args.out.read_text())
    assert verdict["status"] == "fail"
    assert [f["kind"] for f in verdict["failures"]] == [kind]
    assert len(verdict["adjudicated_mismatches"]) == 1
    assert all(d["category"] == kind for d in verdict["failures"][0]["differences"])
    if strict_change == "work":
        assert "search_queries" in verdict["failures"][0]["message"]
        assert verdict["failures"][0]["differences"][0]["short"] == "6"
        assert verdict["failures"][0]["differences"][0]["long"] == "99"
    output = tmp_path / "rejected.tsv"
    merge = argparse.Namespace(plan=args.plan, long=tmp_path / "plan-empty-long.tsv",
                               validation=args.out, output=output)
    with pytest.raises(recycle.RecycleError, match="recycling was not validated"):
        recycle.merge(merge)
    assert not output.exists()
    # A forged pass must still fail when merge replays the strict comparison.
    verdict.update(status="pass", failures=[])
    args.out.write_text(json.dumps(verdict))
    with pytest.raises(recycle.RecycleError, match="differs from replay"):
        recycle.merge(merge)
    assert not output.exists()
