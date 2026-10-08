from __future__ import annotations

import csv
import importlib.util
import json
from pathlib import Path

import pytest


SCRIPT = Path(__file__).resolve().parents[2] / "benchmarking/weakening-census.py"


def reader():
    spec = importlib.util.spec_from_file_location("weakening_census", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def lifecycle(outcome="proof", *, fallback=False):
    events = [
        {"event": "worker_start"},
        {"event": "weakening_entry", "prepass": 1, "run_id": 0},
        {"event": "weakening_source", "source_fnv1a64": "current-input"},
        {"event": "weakening_binding", "kind": "simplified_original", "run_id": 0},
        {"event": "weakening_eligibility", "eligible": True, "reason": "eligible",
         "generated": 2},
        {"event": "weakening_candidate_generated", "run_id": 1},
        {"event": "weakening_binding", "kind": "candidate", "run_id": 1},
        {"event": "weakening_candidate_generated", "run_id": 2},
        {"event": "weakening_binding", "kind": "candidate", "run_id": 2},
        {"event": "weakening_attempt_start", "run_id": 1},
        {"event": "weakening_binding", "kind": "runner_objective", "run_id": 1},
        {"event": "weakening_attempt_end", "outcome": outcome, "observer": "worker",
         "wall_ns": 12, "cpu_ns": 8, "run_id": 1},
    ]
    if outcome == "proof":
        events.append({"event": "weakening_proof", "run_id": 1})
    events.append({"event": "weakening_end", "wall_ns": 20, "cpu_ns": 9})
    if fallback:
        events.append({"event": "weakening_fallback_start", "run_id": 0,
                       "remaining_ns": 77, "deadline_ns": 100})
    events.append({"event": "parent_terminal", "telemetry": "complete"})
    rows = [dict(e, worker_pid=123, prepass=e.get("prepass", 1), seq=i + 1,
                 emitter="123", dropped_records=0) for i, e in enumerate(events)]
    rows.extend([
        {"phase": "record_summary", "dropped_records": 0},
        {"event": "writer_summary", "failed_records": 0, "incomplete_packet": False},
    ])
    return rows


def test_success_counts_generated_separately_from_started():
    row, = reader().census(lifecycle())
    assert row["census"] == "complete"
    assert row["mode"] == row["effective_mode"] == "basic"
    assert (row["generated"], row["started"], row["completed"], row["successes"]) == (2, 1, 1, 1)
    assert row["attempt_wall_ns"] == 12 and row["attempt_cpu_ns"] == 8
    assert row["full_solver_started"] is False
    assert row["full_solver_starved"] is False


def test_inconclusive_fallback_budget():
    row, = reader().census(lifecycle("inconclusive", fallback=True))
    assert row["census"] == "complete"
    assert row["full_solver_started"] is True and row["full_solver_remaining_ns"] == 77


@pytest.mark.parametrize("damage", ["drop", "sequence", "writer", "terminal", "proof_binding",
                                    "attempt_end", "prepass_end", "duplicate", "prefix",
                                    "writer_fields"])
def test_missing_telemetry_never_establishes_zero(damage):
    rows = lifecycle()
    if damage == "drop":
        rows[-2]["dropped_records"] = 1
    elif damage == "sequence":
        rows[6]["seq"] += 1
    elif damage == "writer":
        rows.pop()
    elif damage == "writer_fields":
        rows[-1] = {"event": "writer_summary"}
    elif damage == "prefix":
        rows = rows[2:]
    elif damage == "duplicate":
        rows.insert(-3, dict(next(r for r in rows if r.get("event") == "weakening_attempt_end")))
    else:
        event = {"terminal": "parent_terminal", "proof_binding": "weakening_binding",
                 "attempt_end": "weakening_attempt_end", "prepass_end": "weakening_end"}[damage]
        rows = [r for r in rows if r.get("event") != event]
    row, = reader().census(rows)
    assert row["census"] == "incomplete"
    assert row["generated"] is None and row["started"] is None and row["successes"] is None
    assert row["cancellations"] is None and row["observed_cancellations"] == 0
    assert row["full_solver_started"] is None


def test_parent_cancel_keeps_wall_but_not_cpu_or_completion():
    rows = lifecycle("cancellation")
    end = next(r for r in rows if r.get("event") == "weakening_attempt_end")
    end.update(observer="parent", cpu_ns=None)
    rows = [r for r in rows if r.get("event") != "weakening_end"]
    # Producer sequences are independent of parent records.
    for i, r in enumerate(r for r in rows if "seq" in r):
        r["seq"] = i + 1
    row, = reader().census(rows)
    assert row["census"] == "complete"
    assert row["started"] == 1 and row["completed"] == 0 and row["cancellations"] == 1
    assert row["attempt_wall_ns"] == 12 and row["attempt_cpu_ns"] is None


def test_absent_prepass_is_unknown():
    row, = reader().census([{"event": "worker_start", "worker_pid": 123}])
    assert row["eligible"] is None and row["generated"] is None
    assert row["started"] is None and row["full_solver_started"] is None
    assert row["cancellations"] is None and row["observed_cancellations"] == 0
    assert row["observed_generated"] == 0 and row["observed_started"] == 0


def test_truncated_delivery_and_cli(tmp_path):
    rows = lifecycle("inconclusive", fallback=True)
    (tmp_path / "123.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows) + '{"event":')
    records, problems = reader().read_records(tmp_path)
    assert len(records) == len(rows) and len(problems) == 1
    row, = reader().census(records, problems)
    assert row["delivery"] == "incomplete" and row["full_solver_started"] is True
    assert row["full_solver_starved"] is None
    output = tmp_path / "census.tsv"
    assert reader().main([str(tmp_path), "--output", str(output)]) == 0
    with output.open() as stream:
        exported, = csv.DictReader(stream, delimiter="\t")
    assert exported["cancellations"] == "UNKNOWN"
    assert exported["observed_cancellations"] == "0"


@pytest.mark.parametrize("budget", [0, 77, None, "absent"])
@pytest.mark.parametrize("parent_reason", ["deadline", "exit"])
def test_fallback_starvation_uses_available_budget(budget, parent_reason):
    rows = lifecycle("inconclusive", fallback=True)
    fallback = next(r for r in rows if r.get("event") == "weakening_fallback_start")
    if budget == "absent":
        del fallback["remaining_ns"]
        del fallback["deadline_ns"]
    else:
        fallback["remaining_ns"] = budget
        if budget is None:
            fallback["deadline_ns"] = 0
    next(r for r in rows if r.get("event") == "parent_terminal")["reason"] = parent_reason
    row, = reader().census(rows)
    assert row["census"] == "complete" and row["started"] == 1
    assert row["full_solver_started"] is True
    assert row["full_solver_starved"] is (
        budget == 0 if type(budget) is int else False if budget is None else None)


def test_deadline_before_fallback_is_starvation():
    rows = lifecycle("cancellation")
    next(r for r in rows if r.get("event") == "parent_terminal")["reason"] = "deadline"
    row, = reader().census(rows)
    assert row["census"] == "complete" and row["full_solver_started"] is False
    assert row["full_solver_starved"] is True


@pytest.mark.parametrize("damage", ["unstarted_proof", "unstarted_proof_missing_binding",
                                    "ungenerated_start", "unstarted_end", "duplicate_generated",
                                    "duplicate_proof", "missing_run_id"])
def test_run_ids_must_join_generation_attempt_and_proof(damage):
    rows = lifecycle()
    if damage.startswith("unstarted_proof"):
        next(r for r in rows if r.get("event") == "weakening_proof")["run_id"] = 2
        binding = next(r for r in rows if r.get("kind") == "runner_objective")
        rows.insert(-3, dict(binding, run_id=2))
        if damage == "unstarted_proof_missing_binding":
            rows = [r for r in rows if not (r.get("kind") == "candidate" and r.get("run_id") == 1)]
    elif damage == "ungenerated_start":
        for row in rows:
            if row.get("run_id") == 1 and row.get("event") != "weakening_candidate_generated":
                row["run_id"] = 3
    elif damage == "unstarted_end":
        next(r for r in rows if r.get("event") == "weakening_attempt_end")["run_id"] = 2
    elif damage == "missing_run_id":
        del next(r for r in rows if r.get("event") == "weakening_proof")["run_id"]
    else:
        event = ("weakening_candidate_generated" if damage == "duplicate_generated"
                 else "weakening_proof")
        rows.insert(-3, dict(next(r for r in rows if r.get("event") == event)))
        if damage == "duplicate_generated":
            next(r for r in rows if r.get("event") == "weakening_eligibility")["generated"] = 3
    # Keep delivery intact so only the inconsistent lifecycle makes the census incomplete.
    for i, row in enumerate(r for r in rows if "worker_pid" in r):
        row["seq"] = i + 1
    row, = reader().census(rows)
    assert row["delivery"] == "complete" and row["census"] == "incomplete"
    assert row["successes"] is None and row["observed_successes"] == 1


def test_dropped_cancellation_retains_confirmed_observation():
    rows = lifecycle("cancellation")
    rows[-2]["dropped_records"] = 1
    row, = reader().census(rows)
    assert row["census"] == "incomplete" and row["cancellations"] is None
    assert row["observed_cancellations"] == 1
    assert row["observed_generated"] == 2 and row["observed_started"] == 1


@pytest.mark.parametrize("reason", ["deadline", "interrupted", "exit"])
def test_never_started_fallback_with_exhausted_budget(reason):
    rows = lifecycle("cancellation")
    end = next(r for r in rows if r.get("event") == "weakening_attempt_end")
    end.update(observer="parent", remaining_ns=0, deadline_ns=100)
    next(r for r in rows if r.get("event") == "parent_terminal")["reason"] = reason
    rows = [r for r in rows if r.get("event") != "weakening_end"]
    for i, r in enumerate(r for r in rows if "seq" in r):
        r["seq"] = i + 1
    row, = reader().census(rows)
    assert row["census"] == "complete"
    assert row["full_solver_started"] is False
    assert row["full_solver_starved"] is True


def test_external_interruption_during_prepass_is_starvation():
    rows = lifecycle("cancellation")
    next(r for r in rows if r.get("event") == "parent_terminal")["reason"] = "interrupted"
    row, = reader().census(rows)
    assert row["full_solver_starved"] is True


def test_winner_cancellation_is_not_budget_starvation():
    rows = lifecycle("cancellation")
    next(r for r in rows if r.get("event") == "parent_terminal")["reason"] = "winner_cancelled"
    row, = reader().census(rows)
    assert row["full_solver_starved"] is False


def test_exhausted_budget_starves_even_before_any_attempt():
    rows = lifecycle("inconclusive", fallback=True)
    rows = [r for r in rows if r.get("event") not in {
        "weakening_attempt_start", "weakening_attempt_end"}]
    next(r for r in rows if r.get("event") == "weakening_fallback_start")["remaining_ns"] = 0
    for i, r in enumerate(r for r in rows if "seq" in r):
        r["seq"] = i + 1
    row, = reader().census(rows)
    assert row["census"] == "complete" and row["started"] == 0
    assert row["full_solver_starved"] is True


def test_requested_mode_is_distinct_from_basic_automaton_route():
    rows = lifecycle("inconclusive", fallback=True)
    rows.insert(1, {"event": "weakening_requested_mode", "worker_pid": 123, "prepass": 0,
                    "mode": "extended", "emitter": "123", "seq": 2, "dropped_records": 0})
    for i, r in enumerate(r for r in rows if "seq" in r):
        r["seq"] = i + 1
    row, = reader().census(rows)
    assert row["census"] == "complete"
    assert row["mode"] == "extended" and row["effective_mode"] == "basic"
