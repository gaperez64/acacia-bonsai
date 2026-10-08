from __future__ import annotations

import copy
import csv
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


SCRIPT = Path(__file__).resolve().parents[2] / "benchmarking/attribution-completeness.py"
spec = importlib.util.spec_from_file_location("attribution_completeness", SCRIPT)
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


@pytest.fixture
def lifecycle():
    context = dict(worker=0, worker_pid=20, requested_backend="oxidd", effective_backend="oxidd",
                   original_polarity="REAL", proof_polarity="REAL", route="R", stage="R",
                   reason="none", r_prepass=True, equivariance=False, mono_ns=100,
                   dropped_records=0, telemetry="pending")

    def event(name, seq, **fields):
        return {**context, "event": name, "seq": seq, **fields}

    spawn = event("worker_spawn", 1, original_polarity="both")
    specification = dict(event="worker_spec", worker=0, worker_pid=20,
                         kind="both:gr1-real-lift:oxidd",
                         requested_polarity="both", translation="native", transform="exact",
                         provider="native", r_prepass=True, equivariance=False)
    child = [event("worker_start", 1, original_polarity="both"), event("route_selected", 2),
             event("route_start", 3),
             event("terminal_result", 4, exit_code=0, signal=0)]
    parent = [spawn, specification,
              {**event("parent_terminal", 5), "exit_code": 0, "signal": 0,
               "reason": "exit", "telemetry": "complete"},
              {**event("parent_winner", 6), "exit_code": 0, "signal": 0,
               "reason": "accepted", "telemetry": "complete"},
              dict(arm="legacy_parent", phase="record_summary", dropped_records=0)]
    child += [dict(arm="native", phase="record_summary", dropped_records=0)]
    return {10: parent, 20: child}


def write_records(directory, files, *, failed_records=0, incomplete_packet=False):
    directory.mkdir(parents=True, exist_ok=True)
    for pid, records in files.items():
        (directory / f"{pid}.jsonl").write_text(
            "".join(json.dumps(record) + "\n" for record in records), encoding="utf-8")
    writer = dict(event="writer_summary", failed_records=failed_records,
                  incomplete_packet=incomplete_packet,
                  delivered_records=sum(len(records) for records in files.values()))
    (directory / "11.jsonl").write_text(json.dumps(writer) + "\n", encoding="utf-8")


def row(**fields):
    return dict(solver_label="a/b", instance="input/name.ltl", cap_s="17", run_index="0",
                result="REALIZABLE", timed_out="false",
                **{"flags": "--arms both:gr1-real-lift:oxidd --equivariance off", **fields})


def reasons(result, dimension):
    return {issue["reason"] for issue in result["issues"] if issue["dimension"] == dimension}


def test_complete_lifecycle_and_attribution_reuse(tmp_path, lifecycle):
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert result["complete"]
    assert result["delivery"] == result["producer"] == "complete"
    assert result["accepted_winners"][0]["route"] == "R"
    assert result["workers"][0]["spec"][0]["requested_polarity"] == "both"
    assert result["checkpoint_phase"] == "unknown"
    assert reader.summarize([result])["delivery"]["complete"] == 1
    assert reader.WORKER.read_tsv.__module__ == "attribution_worker_phases"
    assert reader.DIAG.classify_target.__module__ == "attribution_diag_phases"


@pytest.mark.parametrize("event,dimension,reason", [
    ("writer_summary", "delivery", "missing_writer_summary"),
    ("parent_terminal", "delivery", "missing_parent_terminal"),
    ("parent_terminal", "producer", "missing_parent_terminal"),
    ("parent_winner", "delivery", "missing_accepted_winner"),
    ("worker_spec", "delivery", "missing_worker_spec"),
    ("worker_spawn", "delivery", "missing_worker_spawn"),
    ("worker_start", "delivery", "missing_worker_start"),
    ("terminal_result", "delivery", "missing_terminal_result"),
])
def test_missing_required_evidence(tmp_path, lifecycle, event, dimension, reason):
    files = {pid: [record for record in records if record.get("event") != event]
             for pid, records in lifecycle.items()}
    write_records(tmp_path, files)
    if event == "writer_summary":
        (tmp_path / "11.jsonl").unlink()
    result = reader.audit_row(row(), tmp_path)
    assert not result["complete"]
    assert reason in reasons(result, dimension)
    assert all(issue["evidence"] for issue in result["issues"])


@pytest.mark.parametrize("pid,offset", [(10, -1), (20, -1), (10, 2)])
def test_drop_counts_including_parent_summary(tmp_path, lifecycle, pid, offset):
    lifecycle[pid][offset]["dropped_records"] = 2
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert "producer_drops" in reasons(result, "delivery")
    assert "producer_drops" in reasons(result, "producer")


@pytest.mark.parametrize("failed,incomplete,reason", [
    (1, False, "writer_failed_records"), (0, True, "writer_incomplete_packet"),
])
def test_writer_loss(tmp_path, lifecycle, failed, incomplete, reason):
    write_records(tmp_path, lifecycle, failed_records=failed, incomplete_packet=incomplete)
    result = reader.audit_row(row(), tmp_path)
    assert reason in reasons(result, "delivery")
    assert result["producer"] == "complete"


@pytest.mark.parametrize("suffix,reason", [
    ('{"event":', "truncated_trailing_line"), ('{"event":\n', "malformed_json"),
    ('[]\n', "malformed_json"), ('{}', "truncated_trailing_line"),
])
def test_bad_json_retains_confirmed_records(tmp_path, lifecycle, suffix, reason):
    write_records(tmp_path, lifecycle)
    with (tmp_path / "20.jsonl").open("a") as stream:
        stream.write(suffix)
    result = reader.audit_row(row(), tmp_path)
    assert reason in reasons(result, "delivery")
    assert result["confirmed_records"] == 11
    assert result["accepted_winners"][0]["route"] == "R"
    assert len(result["workers"][0]["routes"]) == 2


def test_child_sequence_gap_and_independent_parent_spawn(tmp_path, lifecycle):
    lifecycle[20][1]["seq"] = 5
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert "sequence_gap" in reasons(result, "delivery")
    assert result["producer"] == "complete"


@pytest.mark.parametrize("cause", ["deadline", "winner_cancelled"])
def test_killed_worker_has_delivered_prefix_but_incomplete_producer(tmp_path, lifecycle, cause):
    lifecycle[20] = lifecycle[20][:3]
    terminal = lifecycle[10][2]
    terminal.update(seq=4, telemetry="incomplete", signal=9, exit_code=-1, reason=cause)
    lifecycle[10] = [r for r in lifecycle[10] if r.get("event") != "parent_winner"]
    write_records(tmp_path, lifecycle)
    campaign = {**row(), "result": "TIMEOUT", "timed_out": "true"}
    result = reader.audit_row(campaign, tmp_path)
    assert result["delivery"] == "complete"
    assert result["producer"] == "incomplete"
    assert reasons(result, "producer") == {"worker_final_event_not_emitted"}
    assert result["workers"][0]["producer_case"] == "documented_kill"
    assert result["workers"][0]["declines"] == []


def test_killed_before_start_identified_only_by_parent_metadata(tmp_path, lifecycle):
    lifecycle.pop(20)
    lifecycle[10][2].update(seq=1, telemetry="incomplete", signal=9, exit_code=-1,
                            route="unknown", stage="startup", reason="deadline")
    lifecycle[10] = [r for r in lifecycle[10] if r.get("event") != "parent_winner"]
    write_records(tmp_path, lifecycle)
    result = reader.audit_row({**row(), "result": "TIMEOUT"}, tmp_path)
    assert reasons(result, "delivery") == {"missing_worker_start"}
    assert reasons(result, "producer") == {"worker_final_event_not_emitted"}
    worker = result["workers"][0]
    assert worker["worker_pid"] == 20 and worker["parent_only"]
    assert worker["producer_case"] == "documented_kill"
    assert worker["last_observed"]["route"] == "unknown"
    assert worker["spec"][0]["kind"] == "both:gr1-real-lift:oxidd"
    assert worker["routes"] == worker["declines"] == []
    assert worker["stops"][0]["event"] == "parent_terminal"
    assert worker["stops"][0]["reason"] == "deadline"


def test_kill_between_route_selection_and_start(tmp_path, lifecycle):
    lifecycle[20] = lifecycle[20][:2]
    lifecycle[10][2].update(seq=3, telemetry="incomplete", signal=9, exit_code=-1,
                            reason="deadline")
    lifecycle[10] = [r for r in lifecycle[10] if r.get("event") != "parent_winner"]
    write_records(tmp_path, lifecycle)
    result = reader.audit_row({**row(), "result": "UNKNOWN"}, tmp_path)
    assert result["delivery"] == "complete"
    assert result["producer"] == "incomplete"


def test_declines_stops_and_existing_checkpoint_bucket(tmp_path, lifecycle):
    lifecycle[20].insert(3, {**lifecycle[20][2], "event": "decline", "seq": 4,
                            "reason": "seed_window", "stage": "seed_window"})
    lifecycle[20].insert(4, {**lifecycle[20][2], "event": "route_stopped", "seq": 5,
                            "reason": "resource", "stage": "checker_capacity"})
    lifecycle[20][-2]["seq"] = 6
    lifecycle[10][2]["seq"] = 7
    lifecycle[10][3]["seq"] = 8
    lifecycle[20].insert(0, dict(checkpoint="cpre-before-intersection", downset_ms="1"))
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert result["complete"]
    worker = result["workers"][0]
    assert worker["declines"][0]["reason"] == "seed_window"
    assert worker["stops"][0]["reason"] == "resource"
    assert result["checkpoint_phase"] == "fixpoint-bound"
    assert result["fixpoint_bucket"] == "downset-bound"


def test_requested_inventory_catches_fully_absent_worker(tmp_path, lifecycle):
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path, expected_workers=2)
    assert "worker_inventory_mismatch" in reasons(result, "delivery")
    assert "worker_inventory_unknown" in reasons(result, "producer")
    flags = "--arms real:small:backward,real:small:forward"
    assert reader.expected_worker_count(row(flags=flags)) == 2


def test_campaign_cli_counts_every_row_and_protects_inputs(tmp_path, lifecycle):
    root = tmp_path / "phases"
    rows = [row(), {**row(), "run_index": "1", "result": "TIMEOUT"}]
    for campaign in rows:
        write_records(reader.phase_directory(root, campaign), copy.deepcopy(lifecycle))
    incomplete = reader.phase_directory(root, rows[1])
    (incomplete / "11.jsonl").unlink()
    campaign = tmp_path / "campaign.tsv"
    with campaign.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, rows[0].keys(), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    output, report, summary = (tmp_path / name
                               for name in ("rows.tsv", "report.md", "summary.json"))
    command = [sys.executable, str(SCRIPT), str(campaign), "--phase-records-dir", str(root),
               "--output", str(output), "--report", str(report), "--summary", str(summary)]
    run = subprocess.run(command, text=True, capture_output=True)
    assert run.returncode == 0, run.stderr
    counts = json.loads(summary.read_text())
    assert counts["rows"] == 2 and counts["complete"] == 1 and counts["incomplete"] == 1
    assert counts["delivery"]["incomplete_by_reason"]["missing_writer_summary"] == 1
    with output.open() as stream:
        audits = list(csv.DictReader(stream, delimiter="\t"))
    assert len(audits) == 2
    assert json.loads(audits[0]["accepted_winners"])[0]["route"] == "R"
    assert "missing_writer_summary" in report.read_text()
    before = campaign.read_bytes()
    command[command.index("--output") + 1] = str(campaign)
    run = subprocess.run(command, text=True, capture_output=True)
    assert run.returncode == 1 and "overwrite campaign inputs" in run.stderr
    assert campaign.read_bytes() == before


@pytest.mark.parametrize("telemetry,reason", [
    ("unavailable", "producer_metadata_unavailable"),
    ("dropped", "producer_drops"),
    ({"invalid": True}, "invalid_telemetry"),
])
def test_producer_metadata_states(tmp_path, lifecycle, telemetry, reason):
    lifecycle[10][2]["telemetry"] = telemetry
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert result["producer"] == "incomplete"
    assert reason in reasons(result, "producer")


def test_missing_inventory_and_parent_summary(tmp_path):
    result = reader.audit_row(row(), tmp_path / "absent")
    assert result["delivery"] == result["producer"] == "incomplete"
    assert "worker_inventory_unknown" in reasons(result, "producer")
    assert result["accepted_winners"] == result["workers"] == []


def test_interleaved_parent_records_do_not_mix_child_sequences(tmp_path, lifecycle):
    sibling = [{**record, "worker": 1, "worker_pid": 21}
               if "worker" in record else copy.deepcopy(record)
               for record in lifecycle[20][:3]]
    sibling_parent = [{**lifecycle[10][0], "worker": 1, "worker_pid": 21},
                      {**lifecycle[10][1], "worker": 1, "worker_pid": 21},
                      {**lifecycle[10][2], "worker": 1, "worker_pid": 21,
                       "seq": 4, "signal": 9, "exit_code": -1,
                       "reason": "winner_cancelled", "telemetry": "incomplete"}]
    lifecycle[10][2:2] = sibling_parent[:2]
    lifecycle[10][-1:-1] = sibling_parent[2:]
    lifecycle[21] = sibling
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path, expected_workers=2)
    assert result["delivery"] == "complete"
    assert result["producer"] == "incomplete"
    assert result["accepted_winners"][0]["worker_pid"] == 20
    assert result["workers"][1]["producer_case"] == "documented_kill"


@pytest.mark.parametrize("destination", ["fifo", "device_symlink"])
def test_non_regular_destination_is_rejected_within_deadline(tmp_path, lifecycle, destination):
    write_records(tmp_path, lifecycle, failed_records=1)
    path = tmp_path / "10.jsonl"
    path.unlink()
    if destination == "fifo":
        os.mkfifo(path)
    else:
        path.symlink_to("/dev/zero")
    code = (
        "import importlib.util, json, pathlib, sys; "
        "s = importlib.util.spec_from_file_location('reader', sys.argv[1]); "
        "m = importlib.util.module_from_spec(s); s.loader.exec_module(m); "
        "print(json.dumps(m.audit_row(json.loads(sys.argv[3]), pathlib.Path(sys.argv[2]))))"
    )
    run = subprocess.run([sys.executable, "-c", code, str(SCRIPT), str(tmp_path),
                          json.dumps(row())], capture_output=True, text=True, timeout=2)
    assert run.returncode == 0, run.stderr
    result = json.loads(run.stdout)
    assert result["delivery"] == "incomplete"
    assert "non_regular_record_file" in reasons(result, "delivery")
    assert "writer_failed_records" in reasons(result, "delivery")
    assert any(str(path) in issue["evidence"] for issue in result["issues"]
               if issue["reason"] == "non_regular_record_file")


def test_oversize_regular_file_is_rejected(tmp_path, lifecycle):
    write_records(tmp_path, lifecycle)
    path = tmp_path / "10.jsonl"
    with path.open("r+b") as stream:
        stream.truncate(reader.MAX_RECORD_FILE_BYTES + 1)
    result = reader.audit_row(row(), tmp_path)
    assert "record_file_too_large" in reasons(result, "delivery")
    assert result["delivery"] == "incomplete"


@pytest.mark.parametrize("arms", ["real:small:forward", "both:gr1:oxidd"])
def test_same_count_wrong_arm_is_incomplete_through_campaign(tmp_path, lifecycle, arms):
    campaign_row = row(flags=f"--arms={arms} --equivariance off")
    campaign = tmp_path / "campaign.tsv"
    with campaign.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, campaign_row.keys(), delimiter="\t")
        writer.writeheader()
        writer.writerow(campaign_row)
    root = tmp_path / "phases"
    write_records(reader.phase_directory(root, campaign_row), lifecycle)
    result = reader.audit_campaign(campaign, root)[0]
    assert reader.expected_worker_count(campaign_row) == 1
    assert "worker_inventory_mismatch" not in reasons(result, "delivery")
    assert "worker_spec_mismatch" in reasons(result, "delivery")
    assert result["delivery"] == "incomplete" and not result["complete"]


@pytest.mark.parametrize("field", ["r_prepass", "equivariance"])
@pytest.mark.parametrize("event", ["worker_spec", "worker_spawn", "worker_start", "route_selected",
                                  "route_start", "terminal_result", "parent_terminal",
                                  "parent_winner"])
@pytest.mark.parametrize("value", [None, 0, 1, "true", "false"])
def test_runtime_context_requires_booleans(tmp_path, lifecycle, field, event, value):
    for records in lifecycle.values():
        for record in records:
            if record.get("event") == event:
                if value is None:
                    record.pop(field)
                else:
                    record[field] = value
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    context = "spec" if event == "worker_spec" else "event"
    assert f"invalid_{context}_context" in reasons(result, "delivery")
    if value is None:
        assert f"missing_{context}_context" in reasons(result, "delivery")
    assert result["delivery"] == "incomplete" and not result["complete"]


@pytest.mark.parametrize("field", ["r_prepass", "equivariance"])
@pytest.mark.parametrize("event", ["worker_spec", "worker_spawn", "parent_terminal",
                                  "worker_start", "route_start", "terminal_result"])
def test_valid_boolean_must_match_requested_context(tmp_path, lifecycle, field, event):
    for records in lifecycle.values():
        for record in records:
            if record.get("event") == event:
                record[field] = not record[field]
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert "worker_context_mismatch" in reasons(result, "delivery")
    assert result["delivery"] == "incomplete"


@pytest.mark.parametrize("wrong_default", [False, True])
def test_default_portfolio_specs_and_context_are_checked(tmp_path, lifecycle, monkeypatch,
                                                        wrong_default):
    defaults = dict(default_arms="both:gr1:oxidd" if wrong_default
                    else "both:gr1-real-lift:oxidd", enable_equivariant_solver=False,
                    translation_pref="small")
    options = {"options": {name: {"default": value} for name, value in defaults.items()},
               "families": {}}
    presets = {"presets": {"test-default": {}}}
    monkeypatch.setattr(reader.CONFIG, "load_registry", lambda: (options, presets))
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(flags="", preset="test-default"), tmp_path)
    assert result["complete"] is not wrong_default
    assert result["expected_worker_specs"][0]["kind"] == defaults["default_arms"]
    if wrong_default:
        assert "worker_spec_mismatch" in reasons(result, "delivery")


def test_worker_specs_are_matched_by_launch_index(tmp_path, lifecycle):
    sibling = [{**record, "worker": 1, "worker_pid": 21}
               if "worker" in record else copy.deepcopy(record)
               for record in lifecycle[20]]
    parent = [{**record, "worker": 1, "worker_pid": 21}
              for record in lifecycle[10][:3]]
    parent[1]["kind"] = "both:gr1:oxidd"
    lifecycle[10][2:2] = parent[:2]
    lifecycle[10][-1:-1] = parent[2:]
    lifecycle[21] = sibling
    flags = "--arms both:gr1-real-lift:oxidd,both:gr1:oxidd --equivariance off"
    write_records(tmp_path, lifecycle)
    assert reader.audit_row(row(flags=flags), tmp_path)["complete"]
    lifecycle[10][1]["kind"], lifecycle[10][3]["kind"] = (
        lifecycle[10][3]["kind"], lifecycle[10][1]["kind"])
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(flags=flags), tmp_path)
    assert "worker_spec_mismatch" in reasons(result, "delivery")
    assert "worker_inventory_mismatch" not in reasons(result, "delivery")


@pytest.mark.parametrize("field,value", [("kind", "legacy"), ("requested_polarity", "REAL"),
                                          ("translation", "small"), ("transform", "real"),
                                          ("provider", "frozen-graph")])
def test_each_worker_spec_field_matches_requested_arm(tmp_path, lifecycle, field, value):
    lifecycle[10][1][field] = value
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert "worker_spec_mismatch" in reasons(result, "delivery")
    assert any(field in issue["evidence"] for issue in result["issues"]
               if issue["reason"] == "worker_spec_mismatch")


def test_requested_backend_checked_without_rejecting_effective_coercion(tmp_path, lifecycle):
    for records in lifecycle.values():
        for record in records:
            if record.get("event") and record["event"] != "worker_spec":
                record["effective_backend"] = "backward"
    write_records(tmp_path, lifecycle)
    assert reader.audit_row(row(), tmp_path)["complete"]
    lifecycle[20][2]["requested_backend"] = "forward"
    write_records(tmp_path, lifecycle)
    result = reader.audit_row(row(), tmp_path)
    assert "worker_context_mismatch" in reasons(result, "delivery")


@pytest.mark.parametrize("replacement,reason", [("fifo", "non_regular_record_file"),
                                               ("device_symlink", "unreadable_record_file"),
                                               ("regular", "record_file_changed")])
def test_destination_replaced_after_lstat_is_rejected(tmp_path, lifecycle, monkeypatch,
                                                     replacement, reason):
    write_records(tmp_path, lifecycle)
    path = tmp_path / "10.jsonl"
    open_file = reader.os.open

    def replace_and_open(destination, flags):
        if destination == path:
            path.rename(tmp_path / "original")
            if replacement == "fifo":
                os.mkfifo(path)
            elif replacement == "device_symlink":
                path.symlink_to("/dev/zero")
            else:
                path.write_text('{}\n')
        return open_file(destination, flags)

    monkeypatch.setattr(reader.os, "open", replace_and_open)
    result = reader.audit_row(row(), tmp_path)
    assert reason in reasons(result, "delivery")
    assert result["delivery"] == "incomplete"


def test_record_size_limit_covers_growth_after_fstat(tmp_path, lifecycle, monkeypatch):
    write_records(tmp_path, lifecycle)
    path = tmp_path / "10.jsonl"
    descriptor_stat = reader.os.fstat
    maximum = 8192
    monkeypatch.setattr(reader, "MAX_RECORD_FILE_BYTES", maximum)

    def grow_after_stat(descriptor):
        snapshot = descriptor_stat(descriptor)
        if snapshot.st_ino == path.stat().st_ino:
            with path.open("r+b") as stream:
                stream.truncate(maximum + 1)
        return snapshot

    monkeypatch.setattr(reader.os, "fstat", grow_after_stat)
    result = reader.audit_row(row(), tmp_path)
    assert "record_file_too_large" in reasons(result, "delivery")
    assert result["delivery"] == "incomplete"
