"""Synthetic tests for the W1 paired B-vs-S gain/loss categorization."""

import importlib.util
import sys
from pathlib import Path

import pytest


SCRIPT = (
    Path(__file__).resolve().parents[2]
    / "benchmarking"
    / "witness-lifting-20260918"
    / "opening"
    / "gains-losses.py"
)
SPEC = importlib.util.spec_from_file_location("w1_gains_losses", SCRIPT)
gains_losses = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gains_losses
SPEC.loader.exec_module(gains_losses)


def solved(instance, seconds, result="REALIZABLE"):
    return {
        "instance": instance,
        "decisive_result": result,
        "decisive_seconds": str(seconds),
        "still_unsolved_at_max_cap": "false",
        "failure_kind_at_max_cap": result,
    }


def unsolved(instance, failure_kind="TIMEOUT"):
    return {
        "instance": instance,
        "decisive_result": "",
        "decisive_seconds": "",
        "still_unsolved_at_max_cap": "true",
        "failure_kind_at_max_cap": failure_kind,
    }


def comparison(baseline, candidate):
    return gains_losses.compare_series([baseline], [candidate])


def test_clean_gain():
    rows = comparison(unsolved("gain.ltl"), solved("gain.ltl", 1.25))
    assert rows == [
        {
            "instance": "gain.ltl",
            "kind": "gain",
            "baseline_status": "TIMEOUT",
            "baseline_seconds": "",
            "candidate_status": "REALIZABLE",
            "candidate_seconds": "1.250",
            "delta_seconds": "",
        }
    ]


def test_clean_loss():
    rows = comparison(solved("loss.ltl", 1.25), unsolved("loss.ltl", "UNKNOWN"))
    assert rows[0]["kind"] == "loss"
    assert rows[0]["baseline_status"] == "REALIZABLE"
    assert rows[0]["candidate_status"] == "UNKNOWN"
    assert rows[0]["candidate_seconds"] == rows[0]["delta_seconds"] == ""


def test_verdict_conflict_takes_precedence_over_timing():
    rows = comparison(
        solved("conflict.ltl", 1.0, "REALIZABLE"),
        solved("conflict.ltl", 10.0, "UNREALIZABLE"),
    )
    assert len(rows) == 1
    assert rows[0]["kind"] == "verdict-conflict"
    assert rows[0]["delta_seconds"] == "+9.000"


def test_unsolved_kind_change():
    rows = comparison(
        unsolved("kind-change.ltl", "TIMEOUT"),
        unsolved("kind-change.ltl", "ERROR"),
    )
    assert len(rows) == 1
    assert rows[0]["kind"] == "unsolved-kind-change"


@pytest.mark.parametrize(
    ("candidate_seconds", "expected_kind"),
    [
        (21.0001, "slower"),
        (21.0, None),
        (20.9999, None),
        (18.9999, "faster"),
        (19.0, None),
        (19.0001, None),
    ],
)
def test_five_percent_threshold_is_strict_on_both_sides(candidate_seconds, expected_kind):
    rows = comparison(
        solved("threshold.ltl", 20.0), solved("threshold.ltl", candidate_seconds)
    )
    assert ([row["kind"] for row in rows] or [None]) == [expected_kind]


@pytest.mark.parametrize(
    ("candidate_seconds", "expected_kind"),
    [
        (0.3001, "slower"),
        (0.30, None),
        (0.2999, None),
        (0.1999, "faster"),
        (0.20, None),
        (0.2001, None),
    ],
)
def test_fifty_millisecond_floor_is_strict_on_both_sides(candidate_seconds, expected_kind):
    rows = comparison(
        solved("threshold.ltl", 0.25), solved("threshold.ltl", candidate_seconds)
    )
    assert ([row["kind"] for row in rows] or [None]) == [expected_kind]


def test_unaffected_row_is_not_returned():
    assert comparison(solved("unaffected.ltl", 2.0), solved("unaffected.ltl", 2.05)) == []
