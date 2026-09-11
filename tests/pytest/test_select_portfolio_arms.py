"""A portfolio's union must be read at one cap, not at whichever cap answered."""
from __future__ import annotations

import csv
import importlib.util
import pathlib

import pytest


SCRIPT = (
    pathlib.Path(__file__).resolve().parents[2]
    / "benchmarking"
    / "select-portfolio-arms.py"
)
spec = importlib.util.spec_from_file_location("select_portfolio_arms", SCRIPT)
selector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selector)

COLUMNS = [
    "solver_label",
    "instance",
    "smallest_cap_solved",
    "decisive_result",
    "decisive_seconds",
    "still_unsolved_at_max_cap",
    "failure_kind_at_max_cap",
    "max_cap_s",
]


def answer(instance, result, seconds, cap):
    return {
        "solver_label": "arm",
        "instance": instance,
        "smallest_cap_solved": str(cap),
        "decisive_result": result,
        "decisive_seconds": str(seconds),
        "still_unsolved_at_max_cap": "false",
        "failure_kind_at_max_cap": result,
        "max_cap_s": "60",
    }


def write_summary(path, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def test_answer_first_decided_past_the_cap_is_not_in_the_union(tmp_path):
    path = tmp_path / "arm-summary.tsv"
    write_summary(
        path,
        [
            answer("fast.ltl", "UNREALIZABLE", 3.0, 5),
            answer("boundary.ltl", "UNREALIZABLE", 16.5, 17),
            answer("slow.ltl", "UNREALIZABLE", 41.0, 60),
        ],
    )

    at_17 = selector.load_arm(path, 17)
    assert set(at_17) == {"fast.ltl", "boundary.ltl"}

    at_60 = selector.load_arm(path, 60)
    assert set(at_60) == {"fast.ltl", "boundary.ltl", "slow.ltl"}


def test_cap_filter_does_not_depend_on_how_long_the_answer_took(tmp_path):
    # decisive_seconds is measured under smallest_cap_solved, so an answer that
    # took 2.5s to find at the 60s cap is still not a 17s answer: the arm only
    # reached it because the earlier caps had already given up on the instance.
    path = tmp_path / "arm-summary.tsv"
    write_summary(path, [answer("late-but-quick.ltl", "REALIZABLE", 2.5, 60)])
    assert selector.load_arm(path, 17) == {}


def test_decisive_result_without_a_solving_cap_is_fatal(tmp_path):
    path = tmp_path / "arm-summary.tsv"
    row = answer("broken.ltl", "REALIZABLE", 1.0, 5)
    row["smallest_cap_solved"] = ""
    write_summary(path, [row])
    with pytest.raises(selector.CensusError, match="smallest_cap_solved"):
        selector.load_arm(path, 17)


def test_undecided_rows_are_skipped_without_reading_the_cap(tmp_path):
    path = tmp_path / "arm-summary.tsv"
    row = answer("timeout.ltl", "TIMEOUT", 0.0, 17)
    row["smallest_cap_solved"] = ""
    row["decisive_result"] = ""
    row["decisive_seconds"] = ""
    write_summary(path, [row])
    assert selector.load_arm(path, 17) == {}
