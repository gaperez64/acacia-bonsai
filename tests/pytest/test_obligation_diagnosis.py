"""Evidence precedence and missing/censored telemetry must not invent stage costs."""
import importlib.util
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "obligation_diagnosis", ROOT / "benchmarking/tools/obligation-diagnosis.py")
DIAG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DIAG)


def test_completed_short_cap_is_retained_but_timeout_is_not():
    short = {"result": "REALIZABLE", "seconds": "2"}
    long = {"result": "TIMEOUT", "seconds": "60"}
    chosen, provenance = DIAG.select_outcome(short, None, long)
    assert chosen is short and "no new measurement" in provenance
    chosen, provenance = DIAG.select_outcome(short, long, short)
    assert chosen is long and provenance == "PMC5 measured 60s"
    chosen, provenance = DIAG.select_outcome({"result": "TIMEOUT"}, None, long)
    assert chosen is long and "fallback" in provenance


def test_absent_telemetry_is_unknown():
    result = DIAG.summarize([])
    assert result["call_status"] == "unknown"
    assert result["call_completed_s"] == ""
    assert result["eligibility_observed_s"] == ""
    assert result["accepted_backend"] == "unknown"


def test_cancelled_call_has_censoring_envelope_not_completed_cost():
    rows = [
        {"event": "invocation_start", "mono_ns": 1_000_000_000},
        {"event": "obligation_start", "mono_ns": 2_000_000_000, "worker_pid": 42},
        {"event": "stage_entry", "stage": "obligation_call", "worker_pid": 42,
         "mono_ns": 3_000_000_000},
        {"event": "obligation_completion", "mono_ns": 8_000_000_000, "worker_pid": 42,
         "outcome": "cancelled", "reason": "winner_cancelled", "evidence": "fixture:4"},
    ]
    result = DIAG.summarize(rows)
    assert result["call_status"] == "censored"
    assert result["call_completed_s"] == ""
    assert result["call_censored_elapsed_to_terminal_s"] == 5
    assert result["helper_reason"] == "winner_cancelled"


def test_parent_acceptance_and_return_are_separate():
    rows = [
        {"event": "invocation_start", "mono_ns": 0},
        {"event": "obligation_start", "worker_pid": 7, "mono_ns": 1},
        {"event": "stage_entry", "stage": "obligation_call", "worker_pid": 7,
         "mono_ns": 2_000_000_000},
        {"event": "stage_completion", "stage": "obligation_call", "worker_pid": 7,
         "mono_ns": 3_000_000_000},
        {"event": "parent_winner", "effective_backend": "obligation", "mono_ns": 4_000_000_000,
         "evidence": "fixture:5"},
    ]
    result = DIAG.summarize(rows)
    assert result["call_completed_s"] == 1
    assert result["accepted_s"] == 4
    assert result["helper_outcome"] == "unknown"
    assert result["telemetry_status"] == "no reported drops"


def test_old_binary_only_supplies_spawn_proxy():
    result = DIAG.summarize([
        {"event": "worker_spawn", "mono_ns": 10_000_000_000},
        {"event": "obligation_eligibility", "mono_ns": 12_000_000_000},
    ])
    assert result["eligibility_from_first_spawn_proxy_s"] == 2
    assert result["eligibility_observed_s"] == ""
    assert result["helper_started"] == "unknown"


@pytest.mark.parametrize("name", ["failed_records", "dropped_records", "incomplete_packet"])
def test_missing_transport_is_visible(name):
    assert DIAG.summarize([{name: 1}])["telemetry_status"] == "incomplete"


def test_archived_trace_without_objective_is_unknown(tmp_path):
    pytest.importorskip("spot")
    import csv
    import json
    from types import SimpleNamespace

    (tmp_path / "input.ltl.json").write_text(json.dumps({
        "objective": "1", "normalized_formula": "G (i <-> o)",
        "outputs": ["o", "unused"], "effective_timing": "Mealy"}))
    (tmp_path / "input.ltl.obl1-byp1.err").write_text("there are 1 subformulas\n")
    listing = tmp_path / "panel.list"
    listing.write_text("input.ltl\n")
    output = tmp_path / "comparison.tsv"
    args = SimpleNamespace(list=listing, captures=tmp_path, traces=tmp_path, out=output)
    DIAG.compare(args)
    row = next(csv.DictReader(output.open(), delimiter="\t"))
    assert row["objective_equal"] == row["ltlsynt_api_output_set"] == "unknown"


def test_explicit_reduction_preserves_unused_output_partition(tmp_path):
    pytest.importorskip("spot")
    import csv
    import json
    from types import SimpleNamespace

    (tmp_path / "input.ltl.json").write_text(json.dumps({
        "objective": "1", "normalized_formula": "G (i <-> o)",
        "outputs": ["o", "unused"], "effective_timing": "Mealy"}))
    (tmp_path / "input.ltl.obl1-byp1.err").write_text(
        "  o := i\nnew formula: 1\nthere are 1 subformulas\n")
    listing = tmp_path / "panel.list"
    listing.write_text("input.ltl\n")
    output = tmp_path / "comparison.tsv"
    DIAG.compare(SimpleNamespace(list=listing, captures=tmp_path, traces=tmp_path, out=output))
    row = next(csv.DictReader(output.open(), delimiter="\t"))
    assert row["objective_equal"] == "True"
    assert json.loads(row["ltlsynt_api_output_set"]) == ["unused"]
    assert json.loads(row["arm_api_outputs"]) == ["o", "unused"]
    assert "indices unobserved" in row["registration_policy_comparison"]


def test_multi_component_trace_does_not_claim_global_objective_equality(tmp_path):
    pytest.importorskip("spot")
    import csv
    import json
    from types import SimpleNamespace

    (tmp_path / "input.ltl.json").write_text(json.dumps({
        "objective": "G a & F b", "normalized_formula": "G a & F b",
        "outputs": ["a", "b"], "effective_timing": "Mealy"}))
    (tmp_path / "input.ltl.obl1-byp1.err").write_text(
        "there are 2 subformulas\nworking on subformula G a\n"
        "working on subformula F b\n")
    listing = tmp_path / "panel.list"
    listing.write_text("input.ltl\n")
    output = tmp_path / "comparison.tsv"
    DIAG.compare(SimpleNamespace(list=listing, captures=tmp_path, traces=tmp_path, out=output))
    row = next(csv.DictReader(output.open(), delimiter="\t"))
    assert row["objective_equal"] == row["ltlsynt_api_output_set"] == "unknown"
    assert json.loads(row["component_objectives"]) == ["G a", "F b"]


def test_fixed_helper_memory_threshold_is_reported_without_inventing_old_values():
    result = DIAG.summarize([{'event': 'helper_memory_share', 'policy': 'alone',
                             'workers': 6, 'rss_share_bytes': 4_294_967_296}])
    assert result['helper_memory_policy'] == 'alone'
    assert result['helper_memory_workers'] == 6
    assert result['helper_rss_share_bytes'] == 4_294_967_296
    old = DIAG.summarize([])
    assert old['helper_memory_policy'] == 'unknown' and old['helper_rss_share_bytes'] == ''
