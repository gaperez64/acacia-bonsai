"""Coverage comparisons must reward answers and preserve missing accounting."""
from __future__ import annotations

import csv
import importlib.util
import pathlib

import pytest


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "benchmarking" / "compare-backend-race.py"
spec = importlib.util.spec_from_file_location("backend_comparison", SCRIPT)
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


def write_rows(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def test_fast_unknown_and_memory_limit_pay_full_par2_penalty():
    rows = {
        "answered": dict(result="REALIZABLE", seconds=1.0, scope_cpu_seconds="0"),
        "declined": dict(result="UNKNOWN", seconds=0.01),
        "censored": dict(result="MEMOUT", seconds=2.0, scope_memory_peak_bytes="8589934592"),
    }
    score = comparison.score(rows, 17)
    assert (score["answered"], score["real"], score["unreal"]) == (1, 1, 0)
    assert score["par2_seconds"] == 69
    assert score["answered_seconds"] == 1
    assert score["scope_cpu_seconds_observations"] == 1
    assert score["scope_cpu_seconds_sum"] == 0
    assert score["cpu_seconds_observations"] == 0
    assert score["cpu_seconds_sum"] is None
    assert score["scope_memory_peak_bytes_max"] == 8589934592


def test_answer_from_larger_cap_is_not_a_primary_cap_gain(tmp_path):
    path = tmp_path / "summary.tsv"
    write_rows(path, [dict(instance="case", decisive_result="UNREALIZABLE",
                          decisive_seconds="25", smallest_cap_solved="60")])
    rows = comparison.load_campaign(path, 17)
    assert comparison.score(rows, 17)["answered"] == 0
    assert comparison.score(rows, 17)["par2_seconds"] == 34


def test_raw_staged_caps_filter_before_repetition_check(tmp_path):
    path = tmp_path / "runs.tsv"
    write_rows(path, [dict(instance="case", result="TIMEOUT", seconds="1.05", cap_s="1"),
                      dict(instance="case", result="REALIZABLE", seconds="2.0", cap_s="17")])
    rows = comparison.load_campaign(path, 17)
    assert comparison.score(rows, 17)["answered"] == 1
    assert rows["case"]["seconds"] == 2.0


def test_repetitions_are_not_silently_replaced(tmp_path):
    path = tmp_path / "runs.tsv"
    value = dict(instance="case", result="REALIZABLE", seconds="1", cap_s="17")
    write_rows(path, [value, value])
    with pytest.raises(ValueError, match="repeated instance"):
        comparison.load_campaign(path, 17)


@pytest.mark.parametrize("seconds", ["nan", "inf", "-1"])
def test_invalid_times_are_rejected(tmp_path, seconds):
    path = tmp_path / "runs.tsv"
    write_rows(path, [dict(instance="case", result="REALIZABLE", seconds=seconds)])
    with pytest.raises(ValueError, match="invalid duration"):
        comparison.load_campaign(path, 17)


def test_comparison_refuses_different_cohorts(tmp_path, monkeypatch):
    base, candidate = tmp_path / "base.tsv", tmp_path / "candidate.tsv"
    write_rows(base, [dict(instance="a", result="REALIZABLE", seconds="1")])
    write_rows(candidate, [dict(instance="b", result="REALIZABLE", seconds="1")])
    monkeypatch.setattr("sys.argv", [str(SCRIPT), "--baseline", str(base), "--candidate", str(candidate)])
    with pytest.raises(SystemExit) as error:
        comparison.main()
    assert error.value.code == 2
