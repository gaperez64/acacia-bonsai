"""Offline checks for PID-bound legacy/current recycling comparisons."""

import argparse
import copy
import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("phase_p4_helpers", ROOT /
                                            "tests/pytest/test_p4_tools.py")
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
capcheck = helpers.capcheck
recycle = helpers.recycle


def current_records(root, row, *, pid=100, clock=1000, stage_id=1, extra_timing=False):
    directory = recycle.phase_record_dir(root, row["solver_label"], row["cap_s"],
                                        row["instance"], row["run_index"])
    directory.mkdir(parents=True)
    parent = [{"arm": "legacy_parent", "phase": "source_snapshot", "work_count": 3}]
    for worker in (0, 1):
        parent.extend([
            {"event": "worker_spawn", "worker": worker, "worker_pid": pid + worker,
             "requested_backend": "backward" if worker == 0 else "forward", "seq": 1,
             "mono_ns": clock},
            {"event": "worker_spec", "worker": worker, "worker_pid": pid + worker,
             "kind": "legacy", "requested_polarity": "REAL", "translation": "small",
             "transform": "real", "provider": "frozen-graph"},
        ])
    child = [
        {"event": "worker_start", "worker": 0},
        {"arm": "legacy", "phase": "child_startup", "wall_ns": clock, "work_count": 0},
        {"event": "stage_entry", "stage_id": stage_id, "stage": "search", "k": 4,
         "subjob": 1, "run_id": 0},
        {"event": "stage_metric", "stage_id": stage_id, "key": "states", "value": "8"},
        {"event": "stage_metric", "stage_id": stage_id, "key": "search_rows_requested",
         "value": "5"},
        {"event": "stage_metric", "stage_id": stage_id, "key": "search_queries",
         "value": "6"},
        {"event": "stage_metric", "stage_id": stage_id, "key": "row_generation_ms",
         "value": str(clock)},
        {"event": "stage_completion", "stage_id": stage_id, "stage": "search", "k": 4,
         "wall_ns": clock, "cpu_ns": clock // 2, "peak_rss_kb": clock * 2},
        {"event": "terminal_result", "worker": 0, "exit_code": 0, "signal": 0,
         "reason": "verified", "route": "backward"},
        {"arm": "legacy", "phase": "record_summary", "dropped_records": 0},
    ]
    if extra_timing:
        child.insert(3, {"event": "stage_metric", "stage_id": stage_id,
                         "key": "bdd_gc_wall_ns", "value": str(clock)})
    seq = 1
    for event in child:
        if "event" in event:
            seq += 1
            event.update(worker_pid=pid, seq=seq, mono_ns=clock)
    parent.extend([
        {"event": "parent_terminal", "worker": 0, "worker_pid": pid, "seq": seq + 1,
         "signal": 0, "exit_code": 0, "reason": "exit", "telemetry": "complete"},
        {"event": "parent_winner", "worker": 0, "worker_pid": pid, "seq": seq + 2,
         "signal": 0, "exit_code": 0, "reason": "accepted", "telemetry": "complete"},
        {"event": "parent_terminal", "worker": 1, "worker_pid": pid + 1, "seq": 3,
         "signal": 9, "exit_code": -1, "reason": "winner_cancelled",
         "telemetry": "incomplete"},
        {"arm": "legacy_parent", "phase": "record_summary", "dropped_records": 0},
    ])
    loser = [{"event": "worker_start", "worker": 1, "worker_pid": pid + 1,
              "seq": 2, "mono_ns": clock, "states": clock}]
    files = {str(pid - 1): parent, str(pid): child, str(pid + 1): loser}
    write_current(directory, files)
    return directory, files


def write_current(directory, files):
    for path in directory.glob("*.jsonl"):
        path.unlink()
    for name, events in files.items():
        (directory / f"{name}.jsonl").write_text(
            "".join(json.dumps(event) + "\n" for event in events))
    (directory / "writer.jsonl").write_text(json.dumps({
        "event": "writer_summary", "delivered_records": sum(map(len, files.values())),
        "failed_records": 0, "incomplete_packet": False}) + "\n")


def compare_pair(tmp_path, mutate=None):
    short = helpers.observation("input", "REALIZABLE")
    long = helpers.observation("input", "REALIZABLE", cap=60)
    first, second = tmp_path / "first", tmp_path / "second"
    current_records(first, short)
    directory, files = current_records(second, long, pid=200, clock=9000, stage_id=17,
                                       extra_timing=True)
    if mutate:
        mutate(files)
        write_current(directory, files)
    return capcheck.compare({"input": short}, {"input": long}, first, second,
                            allow_race_truncation=True)


def test_current_pids_clocks_sequences_stage_ids_and_race_work_validate(tmp_path):
    checked, failures, notes = compare_pair(tmp_path)
    assert checked == 1 and failures == []
    assert any("real:small:forward race-truncated" in note for note in notes)


@pytest.mark.parametrize("change,field,kind", [
    ("winner", "winning_arm", "decision"),
    ("verdict", "exit_code", "outcome"),
    ("reason", "reason", "lifecycle"),
    ("k", "k", "work"),
    ("states", "value", "work"),
    ("search_rows_requested", "value", "work"),
    ("search_queries", "value", "work"),
])
def test_changed_decision_or_deterministic_work_fails(tmp_path, change, field, kind):
    def mutate(files):
        if change == "winner":
            winner = next(e for e in files["199"] if e.get("event") == "parent_winner")
            winner.update(worker=1, worker_pid=201, seq=4)
        elif change in {"verdict", "reason"}:
            terminal = next(e for e in files["200"] if e.get("event") == "terminal_result")
            terminal["exit_code" if change == "verdict" else "reason"] = (
                1 if change == "verdict" else "declined")
        elif change == "k":
            next(e for e in files["200"] if e.get("event") == "stage_entry")["k"] = 5
        else:
            next(e for e in files["200"] if e.get("key") == change)["value"] = "99"
    checked, failures, _ = compare_pair(tmp_path, mutate)
    assert checked == 1
    assert any(f["kind"] == kind and field in f["message"] for f in failures)


def test_completed_arm_missing_child_records_fails(tmp_path):
    checked, failures, _ = compare_pair(tmp_path, lambda files: files.pop("200"))
    assert checked == 1
    assert any(f["kind"] == "records" and "completed worker has no child records" in
               f["message"] for f in failures)


@pytest.mark.parametrize("missing", ["stage_metric", "worker_spec", "parent_terminal"])
def test_missing_work_spec_or_terminal_fails(tmp_path, missing):
    def mutate(files):
        group = files["200" if missing == "stage_metric" else "199"]
        group.remove(next(e for e in group if e.get("event") == missing))
    _, failures, _ = compare_pair(tmp_path, mutate)
    assert failures


@pytest.mark.parametrize("field,value", [("failed_records", 1), ("incomplete_packet", True),
                                         ("delivered_records", 0)])
def test_writer_integrity_is_checked(tmp_path, field, value):
    row = helpers.observation("input", "REALIZABLE")
    directory, _ = current_records(tmp_path, row)
    path = directory / "writer.jsonl"
    writer = json.loads(path.read_text())
    writer[field] = value
    path.write_text(json.dumps(writer) + "\n")
    with pytest.raises(ValueError):
        capcheck.comparison_records(directory)


def test_unknown_counter_is_retained(tmp_path):
    def mutate(files):
        next(e for e in files["200"] if e.get("event") == "stage_completion")[
            "new_work_counter"] = 7
    _, failures, _ = compare_pair(tmp_path, mutate)
    assert any("new_work_counter" in f["message"] for f in failures)


def test_current_format_passes_native_recycle_validation(tmp_path):
    short = helpers.observation("input", "REALIZABLE")
    short["flags"] = "--arms real:small:backward,real:small:forward"
    long = {**short, "cap_s": "60"}
    short_path, long_path = tmp_path / "short.tsv", tmp_path / "long.tsv"
    helpers.write_observations(short_path, [short])
    helpers.write_observations(long_path, [long])
    first, second = tmp_path / "first", tmp_path / "second"
    current_records(first, short)
    current_records(second, long, pid=200, clock=9000, extra_timing=True)
    recycle.plan(argparse.Namespace(short=short_path, list=None, long_cap=60,
                                    deadline_margin=1, deadline_stage_regex="deadline",
                                    memory_max="8G", memory_swap_max="0", records=first,
                                    deterministic_error_list=None, out_prefix=tmp_path / "plan"))
    plan = tmp_path / "plan-plan.json"
    recycle.sample(argparse.Namespace(plan=plan, seed=1, size=1, out_prefix=tmp_path / "sample"))
    args = argparse.Namespace(plan=plan, sample=tmp_path / "sample-sample.json",
                              long=long_path, long_records=second, out=tmp_path / "verdict.json")
    assert recycle.validate(args) == 0
    assert json.loads(args.out.read_text())["checked"] == 1
    args = copy.copy(args)
    directory = recycle.phase_record_dir(second, "candidate", 60, "input", 0)
    path = directory / "200.jsonl"
    path.write_text(path.read_text().replace('"value": "8"', '"value": "9"'))
    assert recycle.validate(args) == 1


@pytest.mark.parametrize("field", sorted(capcheck.VOLATILE_FIELDS))
def test_each_documented_volatile_field_is_excluded(tmp_path, field):
    short = helpers.observation("input", "REALIZABLE")
    long = helpers.observation("input", "REALIZABLE", cap=60)
    first, second = tmp_path / "first", tmp_path / "second"
    for root, row, value in ((first, short, 1), (second, long, 2)):
        helpers.phase_dir(root, row, [
            {"arm": "synthetic", "phase": "search", "work_count": 5, field: value},
            {"arm": "synthetic", "phase": "record_summary", "dropped_records": 0},
        ])
    assert capcheck.compare({"input": short}, {"input": long}, first, second)[1] == []


@pytest.mark.parametrize("key", sorted(capcheck.VOLATILE_METRICS))
def test_each_documented_timing_metric_is_excluded(tmp_path, key):
    def mutate(files):
        next(e for e in files["200"] if e.get("key") == "row_generation_ms")["key"] = key
    _, failures, _ = compare_pair(tmp_path, mutate)
    assert failures == []


def test_native_kind_and_parent_completion_without_arm_summary(tmp_path):
    row = helpers.observation("input", "REALIZABLE")
    directory, files = current_records(tmp_path, row)
    native = "real:gr1:oxidd"
    spec = next(e for e in files["99"] if e.get("event") == "worker_spec")
    spec.update(kind=native, provider="native", translation="native", transform="exact")
    spawn = next(e for e in files["99"] if e.get("event") == "worker_spawn")
    spawn["requested_backend"] = "oxidd"
    files["100"] = [e for e in files["100"] if e.get("phase") != "record_summary"]
    for event in files["100"]:
        if "arm" in event:
            event["arm"] = native
    write_current(directory, files)
    records = capcheck.comparison_records(directory)
    assert records.arm_finished(native)
    assert capcheck.solver_arms(records) == {native, "real:small:forward"}


def test_dropped_completed_worker_cannot_be_skipped_as_race_truncated(tmp_path):
    def mutate(files):
        terminal = next(e for e in files["199"] if e.get("event") == "parent_terminal")
        terminal.update(dropped_records=2, telemetry="dropped")
    _, failures, _ = compare_pair(tmp_path, mutate)
    assert any(f["kind"] == "records" and "dropped_records" in f["message"]
               for f in failures)


def test_missing_winner_in_both_conclusive_runs_fails(tmp_path):
    short = helpers.observation("input", "REALIZABLE")
    long = helpers.observation("input", "REALIZABLE", cap=60)
    first, second = tmp_path / "first", tmp_path / "second"
    for root, row in ((first, short), (second, long)):
        directory, files = current_records(root, row)
        files["99"] = [e for e in files["99"] if e.get("event") != "parent_winner"]
        write_current(directory, files)
    _, failures, _ = capcheck.compare({"input": short}, {"input": long}, first, second,
                                      allow_race_truncation=True)
    assert any(f["kind"] == "records" and "parent_winner" in f["message"] for f in failures)


def test_both_runs_transport_drops_are_retained_separately(tmp_path):
    short = helpers.observation("input", "REALIZABLE")
    long = helpers.observation("input", "REALIZABLE", cap=60)
    first, second = tmp_path / "first", tmp_path / "second"
    for root, row, count in ((first, short, 2), (second, long, 3)):
        directory, files = current_records(root, row)
        loser = next(e for e in files["99"] if e.get("event") == "parent_terminal"
                     and e["worker"] == 1)
        loser.update(dropped_records=count, telemetry="dropped", seq=5)
        write_current(directory, files)
    checked, failures, notes = capcheck.compare({"input": short}, {"input": long}, first, second,
                                               allow_race_truncation=True)
    assert checked == 1 and len(failures) == 2
    assert [f["differences"][0]["count"] for f in failures] == [2, 3]
    assert all(f["differences"][0]["worker"] == 1 for f in failures)
    assert any("work remains unknown" in note for note in notes)


def test_losing_worker_drop_does_not_suppress_parent_winner_facts(tmp_path):
    def mutate(files):
        loser = next(e for e in files["199"] if e.get("event") == "parent_terminal"
                     and e["worker"] == 1)
        loser.update(dropped_records=2, telemetry="dropped", seq=5)
        winner = next(e for e in files["199"] if e.get("event") == "parent_winner")
        winner.update(worker=1, worker_pid=201, seq=6)
    _, failures, _ = compare_pair(tmp_path, mutate)
    assert {f["kind"] for f in failures} == {"records", "decision"}
    assert any(f["arm"] == "legacy_parent" and "winning_arm" in f["message"] for f in failures)
