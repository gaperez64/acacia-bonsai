from __future__ import annotations

import importlib.util
import pathlib
import sys

BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "freeze-frontier-targets.py"


def load():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("freeze_frontier_targets", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


freeze = load()


def candidate(**overrides):
    row = {
        "instance": "a.ltl",
        "family_key": "param:fam",
        "family_display": "fam",
        "tlsf_file": "a.tlsf",
        "tlsf_bytes": "1000",
        "parameter_dimension": "1",
        "parameter_values_json": '{"n": 3}',
        "unsolved_result": "TIMEOUT",
        "failure_kind": "time_limit",
        "neighbour_instance": "b.ltl",
        "neighbour_seconds": "1.0",
        "pair_kind": "cover",
    }
    row.update(overrides)
    return row


def diag_row(instance, **overrides):
    row = {
        "instance": instance, "aut_states": "50", "aut_edges": "100",
        "bool_threshold": "10", "max_f_size": "5", "loops": "3",
        "k_attempts": "1", "actions_seen": "100", "total_ms": "10",
        "result": "TIMEOUT", "final_reason": "cap",
    }
    row.update(overrides)
    return row


def test_aggregates_across_worker_rows():
    """Several workers per instance must collapse to maxima and a sum.

    Taking the first row would describe whichever worker was listed first
    rather than the one that made the instance hard.
    """
    rows = [
        diag_row("a.ltl", aut_states="10", actions_seen="100", total_ms="5"),
        diag_row("a.ltl", aut_states="900", actions_seen="250", total_ms="90",
                 result="MEMOUT"),
        diag_row("a.ltl", aut_states="40", actions_seen="7", total_ms="1"),
    ]
    agg = freeze.aggregate_diagnostics(rows)["a.ltl"]
    assert agg["aut_states_max"] == 900
    assert agg["actions_seen_sum"] == 357
    assert agg["worker_count"] == 3
    # The verdict comes from the longest-running worker, not the last row.
    assert agg["result"] == "MEMOUT"


def test_score_tiers_are_exclusive():
    """An instance earns the small tier or the medium one, never both."""
    diag = freeze.aggregate_diagnostics([diag_row("a.ltl", aut_states="50")])["a.ltl"]
    score, breakdown = freeze.score_candidate(candidate(), diag, set())
    assert "aut100+6" in breakdown
    assert "aut300" not in breakdown

    diag = freeze.aggregate_diagnostics([diag_row("a.ltl", aut_states="250")])["a.ltl"]
    _, breakdown = freeze.score_candidate(candidate(), diag, set())
    assert "aut300+4" in breakdown
    assert "aut100" not in breakdown


def test_memory_limit_is_not_penalised_but_error_is():
    """8 GiB exhaustion is a real solver limit, not an infrastructure failure."""
    diag = freeze.aggregate_diagnostics([diag_row("a.ltl")])["a.ltl"]
    mem_score, mem_breakdown = freeze.score_candidate(
        candidate(unsolved_result="MEMOUT", failure_kind="memory_limit"), diag, set())
    err_score, err_breakdown = freeze.score_candidate(
        candidate(unsolved_result="ERROR"), diag, set())
    assert "infra" not in mem_breakdown
    assert "infra-8" in err_breakdown
    assert mem_score == err_score + 8


def test_clean_cutoff_family_is_rewarded():
    diag = freeze.aggregate_diagnostics([diag_row("a.ltl")])["a.ltl"]
    _, breakdown = freeze.score_candidate(candidate(), diag, {"param:fam"})
    assert "cutoff+3" in breakdown


def test_missing_diagnostics_are_tolerated():
    score, breakdown = freeze.score_candidate(candidate(), None, set())
    assert score > 0
    assert "aut" not in breakdown and "rank" not in breakdown


def test_per_family_cap_of_two():
    scored = [
        dict(candidate(instance=f"i{i}.ltl"), score=40 - i, selection_reason="")
        for i in range(5)
    ]
    chosen = freeze.select(scored, target_count=5, memory_quota=0)
    assert len(chosen) == 2, "at most two targets may come from one family"


def test_memory_quota_prefers_distinct_families():
    """Taking the highest scorers alone puts every quota slot in one family."""
    scored = []
    for i in range(4):
        scored.append(dict(candidate(instance=f"t{i}.ltl",
                                     family_key=f"fam{i}"),
                           score=40, selection_reason=""))
    for i in range(3):
        scored.append(dict(candidate(instance=f"memA{i}.ltl", family_key="memfamA",
                                     unsolved_result="MEMOUT",
                                     failure_kind="memory_limit"),
                           score=14 - i, selection_reason=""))
    scored.append(dict(candidate(instance="memB0.ltl", family_key="memfamB",
                                 unsolved_result="MEMOUT",
                                 failure_kind="memory_limit"),
                       score=12, selection_reason=""))

    chosen = freeze.select(scored, target_count=4, memory_quota=2)
    quota = [r for r in chosen if r["selection_reason"] == "memory_quota"]
    assert len(quota) == 2
    assert len({r["family_key"] for r in quota}) == 2, "quota must span families"


def test_ranks_are_assigned_in_score_order():
    scored = [
        dict(candidate(instance="low.ltl", family_key="f1"), score=10,
             selection_reason=""),
        dict(candidate(instance="high.ltl", family_key="f2"), score=30,
             selection_reason=""),
    ]
    chosen = freeze.select(scored, target_count=2, memory_quota=0)
    assert [r["instance"] for r in chosen] == ["high.ltl", "low.ltl"]
    assert [r["rank"] for r in chosen] == [1, 2]
