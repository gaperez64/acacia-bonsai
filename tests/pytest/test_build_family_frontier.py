"""Synthetic tests for the W1 per-instance family-frontier join."""

import importlib.util
import sys
from pathlib import Path


SCRIPT = (
    Path(__file__).resolve().parents[2]
    / "benchmarking"
    / "witness-lifting-20260918"
    / "opening"
    / "build-family-frontier.py"
)
SPEC = importlib.util.spec_from_file_location("build_family_frontier", SCRIPT)
frontier_builder = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = frontier_builder
SPEC.loader.exec_module(frontier_builder)


def family(instance):
    return {
        "logical_instance": instance,
        "family_key": f"family:{instance}",
        "family_display": instance.removesuffix(".ltl"),
        "origin": f"direct:{instance}",
        "parameter_confidence": "none",
        "parameter_names": "",
        "parameter_values_json": "{}",
        "inputs": "2",
        "outputs": "1",
        "semantics": "Mealy",
        "effective_target": "Mealy",
        "source_sha256": "abc123",
    }


def solved(instance, result="REALIZABLE", seconds="1.25"):
    return {
        "instance": instance,
        "decisive_result": result,
        "decisive_seconds": seconds,
        "still_unsolved_at_max_cap": "false",
        "failure_kind_at_max_cap": result,
    }


def unsolved(instance, failure_kind="TIMEOUT"):
    return {
        "instance": instance,
        # Deliberately populate stale decisive cells: the unsolved marker must
        # make the frontier ignore both of them.
        "decisive_result": "REALIZABLE",
        "decisive_seconds": "99.0",
        "still_unsolved_at_max_cap": "true",
        "failure_kind_at_max_cap": failure_kind,
    }


def build(instances, b_120, s_120):
    family_rows = [family(instance) for instance in instances]
    b_17 = [solved(instance, seconds="0.5") for instance in instances]
    s_17 = [solved(instance, seconds="0.6") for instance in instances]
    return frontier_builder.build_frontier(family_rows, b_120, s_120, b_17, s_17)


def test_join_classifies_required_120s_transitions():
    instances = ["real.ltl", "unsolved.ltl", "gain.ltl", "conflict.ltl"]
    rows = build(
        instances,
        [
            solved("real.ltl"),
            unsolved("unsolved.ltl"),
            unsolved("gain.ltl"),
            solved("conflict.ltl", "REALIZABLE"),
        ],
        [
            solved("real.ltl"),
            unsolved("unsolved.ltl", "RESOURCE_LIMIT"),
            solved("gain.ltl", "UNREALIZABLE"),
            solved("conflict.ltl", "UNREALIZABLE"),
        ],
    )

    by_id = {row["logical_instance"]: row for row in rows}
    assert by_id["real.ltl"]["verdict_transition"] == "stable-realizable"
    assert by_id["unsolved.ltl"]["verdict_transition"] == "stable-unsolved"
    assert by_id["gain.ltl"]["verdict_transition"] == "s-solved-b-not"
    assert by_id["conflict.ltl"]["verdict_transition"] == "verdict-conflict"


def test_unsolved_rows_use_failure_kind_and_blank_seconds():
    row = build(
        ["timeout.ltl"],
        [unsolved("timeout.ltl", "TIMEOUT")],
        [unsolved("timeout.ltl", "")],
    )[0]

    assert row["b_120s_result"] == "TIMEOUT"
    assert row["b_120s_seconds"] == ""
    assert row["s_120s_result"] == "UNSOLVED"
    assert row["s_120s_seconds"] == ""


def test_output_schema_has_no_h_leg_columns():
    assert tuple(frontier_builder.OUTPUT_COLUMNS) == (
        "logical_instance",
        "family_key",
        "family_display",
        "origin",
        "parameter_confidence",
        "parameter_names",
        "parameter_values_json",
        "inputs",
        "outputs",
        "semantics",
        "effective_target",
        "source_sha256",
        "b_120s_result",
        "b_120s_seconds",
        "s_120s_result",
        "s_120s_seconds",
        "b_17s_result",
        "b_17s_seconds",
        "s_17s_result",
        "s_17s_seconds",
        "verdict_transition",
        "winning_witness_reference",
    )
    assert not any(column.startswith("h_") for column in frontier_builder.OUTPUT_COLUMNS)
