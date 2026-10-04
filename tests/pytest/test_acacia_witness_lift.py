"""Focused tests for the cold, deadline-bounded W7 research wrapper."""

import importlib.util
import os
import re
import sys
import time
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "witness-lifting-20260918" / "acacia-witness-lift.py"
SPEC = importlib.util.spec_from_file_location("acacia_witness_lift", SCRIPT)
wrapper = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = wrapper
SPEC.loader.exec_module(wrapper)

ADAPTER_SCRIPT = (
    ROOT
    / "benchmarking"
    / "witness-lifting-20260918"
    / "acacia-witness-lift-coverage-adapter.py"
)
ADAPTER_SPEC = importlib.util.spec_from_file_location(
    "acacia_witness_lift_coverage_adapter", ADAPTER_SCRIPT
)
adapter = importlib.util.module_from_spec(ADAPTER_SPEC)
sys.modules[ADAPTER_SPEC.name] = adapter
ADAPTER_SPEC.loader.exec_module(adapter)


def test_shared_deadline_stops_a_slow_stage_and_reaps_it():
    deadline = wrapper.Deadline.start(0.04)
    started = time.monotonic()
    outcome = wrapper.run_process(
        [sys.executable, "-c", "import time; time.sleep(10)"],
        deadline,
        "fake_slow_stage",
    )

    assert outcome.timed_out
    assert outcome.timeout_kind == "invocation"
    assert time.monotonic() - started < 1.0
    with pytest.raises(ProcessLookupError):
        os.kill(outcome.pid, 0)


def test_in_process_stage_uses_the_same_absolute_deadline():
    deadline = wrapper.Deadline.start(0.02)
    with pytest.raises(wrapper.BudgetExhausted) as caught:
        wrapper.run_callable(
            lambda: time.sleep(1),
            deadline,
            "fake_proposal",
            stage_limit_s=0.5,
        )
    assert caught.value.stage == "fake_proposal"
    assert caught.value.when == "during"


def test_target_check_that_never_runs_cannot_be_decisive(tmp_path, capsys):
    evidence_path = tmp_path / "ineligible.json"
    exit_code = wrapper.main(
        [
            "--family-id",
            "param:tlsf/arbiters_zoo/parametric/arbiter.tlsf",
            "--target",
            "not_the_selected_target.ltl",
            "--budget",
            "1",
            "--output-dir",
            str(tmp_path / "runs"),
            "--evidence-out",
            str(evidence_path),
        ]
    )
    lines = capsys.readouterr().out.splitlines()

    assert exit_code == 2
    assert lines == ["UNKNOWN eligibility family_target_not_selected"]
    assert re.fullmatch(r"UNKNOWN [A-Za-z0-9_.-]+ [A-Za-z0-9_.-]+", lines[0])
    evidence = __import__("json").loads(evidence_path.read_text(encoding="utf-8"))
    assert evidence["target_check_ran"] is False
    assert evidence["target_verified"] is False
    assert evidence["result"]["verdict"] == "UNKNOWN"

    with pytest.raises(ValueError, match="VERIFIED target certificate"):
        wrapper.PipelineResult("REALIZABLE", "target", "wrong", tmp_path / "unchecked.aag")


def test_stdout_verdict_protocol_is_exactly_one_machine_parseable_line(tmp_path, capsys):
    exit_code = wrapper.main(
        [
            "--target",
            "missing.ltl",
            "--budget",
            "1",
            "--output-dir",
            str(tmp_path),
        ]
    )
    captured = capsys.readouterr()

    assert exit_code == 2
    assert captured.err == ""
    assert captured.out.count("\n") == 1
    assert re.fullmatch(
        r"UNKNOWN [A-Za-z0-9_.-]+ [A-Za-z0-9_.-]+\n", captured.out
    )


def test_coverage_adapter_preserves_acacia_result_and_exit_convention():
    assert adapter.adapt_line("REALIZABLE /tmp/checked.aag", 0) == ("REALIZABLE", 0)
    assert adapter.adapt_line("UNREALIZABLE /tmp/checked.aag", 1) == (
        "UNREALIZABLE",
        1,
    )
    assert adapter.adapt_line("UNKNOWN target budget_exhausted", 2) == ("UNKNOWN", 2)
    assert adapter.adapt_line("REALIZABLE /tmp/unchecked.aag", 2) == ("UNKNOWN", 2)


def test_two_cold_invocations_never_share_a_workspace_or_artifact(tmp_path):
    first_id, first = wrapper.create_cold_workspace(tmp_path, "family/example.tlsf")
    marker = first / "fresh-seeds" / "seed_n2.aag"
    marker.parent.mkdir(parents=True)
    marker.write_text("first invocation only\n", encoding="utf-8")

    second_id, second = wrapper.create_cold_workspace(tmp_path, "family/example.tlsf")

    assert first_id != second_id
    assert first != second
    assert marker.is_file()
    assert not (second / "fresh-seeds" / "seed_n2.aag").exists()
    assert not hasattr(wrapper._parser().parse_args([]), "warm")


@pytest.mark.parametrize(
    ("family_id", "target", "expected_target"),
    [
        (
            "param:tlsf/arbiters_zoo/parametric/arbiter.tlsf",
            "arbiter_pb_10_pe_.ltl",
            10,
        ),
        (
            "param:tlsf/round_robin_arbiter/parametric/round_robin_arbiter.tlsf",
            "round_robin_arbiter_pb_10_pe_.ltl",
            10,
        ),
    ],
)
def test_real_family_selection_is_manifest_driven(family_id, target, expected_target):
    request = wrapper.resolve_family_request(
        wrapper.DEFAULT_MANIFEST, family_id, target, None
    )
    assert request.family_id == family_id
    assert request.target_logical_instance == target
    assert request.seeds == (2, 3)
    assert request.sanity == 4
    assert request.target == expected_target


def test_unreal_manifest_loads_but_declines_the_unsupported_player():
    with pytest.raises(wrapper.Ineligible, match="REAL-controller grammar"):
        wrapper.resolve_family_request(
            wrapper.DEFAULT_MANIFEST,
            (
                "param:tlsf/round_robin_arbiter_unreal/parametric/"
                "round_robin_arbiter_unreal2.tlsf"
            ),
            "round_robin_arbiter_unreal2_pb_7_pe_.ltl",
            None,
        )
