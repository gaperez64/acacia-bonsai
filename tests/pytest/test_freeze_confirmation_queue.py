"""Synthetic tests for freezing the W2 confirmation queue."""

import importlib.util
import sys
from pathlib import Path

import pytest


SCRIPT = (
    Path(__file__).resolve().parents[2]
    / "benchmarking"
    / "witness-lifting-20260918"
    / "opening"
    / "freeze-confirmation-queue.py"
)
SPEC = importlib.util.spec_from_file_location("freeze_confirmation_queue", SCRIPT)
queue_builder = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = queue_builder
SPEC.loader.exec_module(queue_builder)


def raw(instance, rss, cap=17):
    return {
        "cap_s": str(cap),
        "instance": instance,
        "max_process_rss_bytes": "" if rss is None else str(rss),
    }


def difference(instance, kind, epoch=1, cap=17):
    return {
        "cap": str(cap),
        "epoch": str(epoch),
        "instance": instance,
        "kind": kind,
    }


def build(differences, baseline, candidate, declared=(), controls=()):
    return queue_builder.build_confirmation_queue(
        differences, baseline, candidate, set(declared), set(controls)
    )


def test_changed_status_is_included():
    rows = build(
        [difference("changed.ltl", "gain")],
        [raw("changed.ltl", 100)],
        [raw("changed.ltl", 100)],
    )

    assert len(rows) == 1
    assert rows[0]["reason"] == "changed-status"
    assert rows[0]["epoch_1_kind"] == "gain"


def test_timing_only_is_included():
    rows = build(
        [difference("timing.ltl", "slower")],
        [raw("timing.ltl", 100)],
        [raw("timing.ltl", 100)],
    )

    assert len(rows) == 1
    assert rows[0]["reason"] == "timing"


@pytest.mark.parametrize(
    ("baseline_rss", "threshold"),
    [
        (64 * queue_builder.MIB, 16 * queue_builder.MIB),
        (200 * queue_builder.MIB, 30 * queue_builder.MIB),
    ],
)
def test_memory_increase_inclusive_threshold_boundary(baseline_rss, threshold):
    instances = {
        "below.ltl": baseline_rss + threshold - 1,
        "boundary.ltl": baseline_rss + threshold,
        "above.ltl": baseline_rss + threshold + 1,
    }
    rows = build(
        [],
        [raw(instance, baseline_rss) for instance in instances],
        [raw(instance, rss) for instance, rss in instances.items()],
    )

    rows_by_instance = {row["instance"]: row for row in rows}
    assert set(rows_by_instance) == {"boundary.ltl", "above.ltl"}
    assert "below.ltl" not in rows_by_instance
    assert rows_by_instance["boundary.ltl"]["reason"] == "memory-increase"
    assert rows_by_instance["boundary.ltl"]["rss_delta_bytes"] == str(threshold)
    assert rows_by_instance["above.ltl"]["reason"] == "memory-increase"
    assert rows_by_instance["above.ltl"]["rss_delta_bytes"] == str(threshold + 1)


def test_declared_benefit_is_included_without_anomaly():
    rows = build(
        [],
        [raw("benefit.ltl", 100)],
        [raw("benefit.ltl", 100)],
        declared={"benefit.ltl"},
    )

    assert len(rows) == 1
    assert rows[0]["reason"] == "declared-benefit"
    assert rows[0]["epoch_1_kind"] == rows[0]["epoch_2_kind"] == ""


def test_near_cap_control_is_included_without_anomaly():
    rows = build(
        [],
        [raw("control.ltl", 100)],
        [raw("control.ltl", 100)],
        controls={"control.ltl"},
    )

    assert len(rows) == 1
    assert rows[0]["reason"] == "near-cap-control"


def test_epoch_two_only_anomaly_is_not_dropped():
    rows = build(
        [difference("epoch-two.ltl", "loss", epoch=2)],
        [raw("epoch-two.ltl", 100)],
        [raw("epoch-two.ltl", 100)],
    )

    assert len(rows) == 1
    assert rows[0]["reason"] == "changed-status"
    assert rows[0]["epoch_1_kind"] == ""
    assert rows[0]["epoch_2_kind"] == "loss"


@pytest.mark.parametrize(("baseline_rss", "candidate_rss"), [(None, 100), (100, None), (0, 100)])
def test_missing_or_zero_rss_does_not_fabricate_a_delta(baseline_rss, candidate_rss):
    rows = build(
        [difference("missing-rss.ltl", "faster")],
        [raw("missing-rss.ltl", baseline_rss)],
        [raw("missing-rss.ltl", candidate_rss)],
    )

    assert len(rows) == 1
    assert rows[0]["reason"] == "timing"
    assert rows[0]["rss_delta_bytes"] == rows[0]["rss_delta_pct"] == ""


def test_instance_with_zero_reasons_is_excluded():
    rows = build(
        [],
        [raw("unaffected.ltl", 100)],
        [raw("unaffected.ltl", 100)],
    )

    assert rows == []
