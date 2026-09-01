from __future__ import annotations

import csv
import importlib.util
import pathlib
import sys
from collections import Counter


SCRIPT = (
    pathlib.Path(__file__).resolve().parents[2]
    / "benchmarking"
    / "select-frontier-targets.py"
)


def load_module():
    spec = importlib.util.spec_from_file_location("select_frontier_targets", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def summary_row(instance="hard.ltl", result="TIMEOUT", family="family:a"):
    return {
        "instance": instance,
        "tlsf_file": instance.removesuffix(".ltl") + ".tlsf",
        "family_key": family,
        "family_display": family,
        "origin_kind": "param",
        "parameter_confidence": "exact",
        "parameter_values_json": '{"n":2}',
        "parameter_dimension": "1",
        "tlsf_bytes": "20480",
        "P_result": result,
    }


def frontier_row(family="family:a"):
    return {
        "family_key": family,
        "count": "3",
        "first_unsolved_instance": "hard.ltl",
        "minimal_unsolved_points_json": "[]",
    }


def pair_row(instance="hard.ltl", family="family:a", seconds="5"):
    return {
        "family_key": family,
        "solved_instance": "easy.ltl",
        "solved_params_json": '{"n":1}',
        "solved_seconds": seconds,
        "unsolved_instance": instance,
        "unsolved_params_json": '{"n":2}',
        "unsolved_failure": "TIMEOUT",
        "pair_kind": "cover",
    }


def test_score_table_is_applied_to_a_synthetic_row():
    module = load_module()

    score, breakdown = module.score_candidate(
        summary_row(), frontier_row(), pair_row(), {"hard.ltl"}
    )

    assert score == 12 + 10 + 8 + 6 + 4
    assert breakdown == "minimal+12,unsolved60+10,nbr5s+8,family3+6,tlsf20k+4"
    assert module.SCORE_WEIGHTS == {
        "minimal": 12,
        "unsolved60": 10,
        "nbr5s": 8,
        "nbr17s": 5,
        "family3": 6,
        "tlsf20k": 4,
        "tlsf50k": 2,
        "infra": -10,
    }


def test_memout_is_not_penalised_but_error_is():
    module = load_module()
    memout = summary_row(result="MEMOUT")
    error = summary_row(result="ERROR")

    memout_score, memout_breakdown = module.score_candidate(
        memout, frontier_row(), pair_row(), {"hard.ltl"}
    )
    error_score, error_breakdown = module.score_candidate(
        error, frontier_row(), pair_row(), {"hard.ltl"}
    )

    assert "infra" not in memout_breakdown
    assert "infra-10" in error_breakdown
    assert memout_score == error_score + 10


def candidate(
    instance, family, score, expectation="none", failure_kind="time_limit"
):
    return {
        "instance": instance,
        "family_key": family,
        "score": str(score),
        "expectation": expectation,
        "failure_kind": failure_kind,
    }


def test_selection_caps_each_family_at_three():
    module = load_module()
    candidates = [candidate(f"a{i}.ltl", "a", 100 - i) for i in range(7)]
    candidates += [candidate(f"b{i}.ltl", "b", 50 - i) for i in range(4)]

    selected = module.select_candidates(candidates, target_count=6)

    counts = {}
    for row in selected:
        counts[row["family_key"]] = counts.get(row["family_key"], 0) + 1
    assert len(selected) == 6
    assert counts == {"a": 3, "b": 3}


def write_tsv(path, fieldnames, rows):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=fieldnames, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)


def miniature_inputs(tmp_path, include_memory=False):
    summary_fields = list(summary_row())
    summaries = [
        summary_row(),
        {
            **summary_row("easy.ltl", result="REALIZABLE"),
            "parameter_values_json": '{"n":1}',
            "tlsf_bytes": "1024",
        },
    ]
    if include_memory:
        summaries.append(
            {
                **summary_row("memory.ltl", result="MEMOUT", family="family:m"),
                "origin_kind": "direct",
                "parameter_confidence": "none",
                "parameter_values_json": "{}",
                "parameter_dimension": "0",
            }
        )
    summary = tmp_path / "summary.tsv"
    write_tsv(summary, summary_fields, summaries)

    frontiers = tmp_path / "frontiers.tsv"
    frontier_rows = [frontier_row()]
    if include_memory:
        frontier_rows.append(
            {
                **frontier_row("family:m"),
                "count": "1",
                "first_unsolved_instance": "",
            }
        )
    write_tsv(frontiers, list(frontier_row()), frontier_rows)

    no_neighbour = {
        **pair_row(),
        "solved_instance": "",
        "solved_params_json": "",
        "solved_seconds": "",
        "pair_kind": "none",
    }
    pairs = tmp_path / "pairs.tsv"
    write_tsv(pairs, list(pair_row()), [no_neighbour, pair_row()])

    metadata = tmp_path / "metadata.tsv"
    metadata_rows = [
        {"logical_instance": "hard.ltl", "tlsf_file": "hard.tlsf", "status": "realizable"},
        {"logical_instance": "easy.ltl", "tlsf_file": "easy.tlsf", "status": "realizable"},
    ]
    if include_memory:
        metadata_rows.append(
            {
                "logical_instance": "memory.ltl",
                "tlsf_file": "memory.tlsf",
                "status": "unrealizable",
            }
        )
    write_tsv(metadata, list(metadata_rows[0]), metadata_rows)
    return summary, frontiers, pairs, metadata


def test_known_pair_is_never_emitted_without_its_neighbour_fields(tmp_path):
    module = load_module()
    paths = miniature_inputs(tmp_path)

    rows = module.build_candidates(*paths)

    assert len(rows) == 1
    assert rows[0]["neighbour_instance"] == "easy.ltl"
    assert rows[0]["neighbour_params_json"] == '{"n":1}'
    assert rows[0]["neighbour_seconds"] == "5"
    assert rows[0]["neighbour_result"] == "REALIZABLE"


def test_memory_candidate_without_ordered_neighbour_is_eligible(tmp_path):
    module = load_module()
    paths = miniature_inputs(tmp_path, include_memory=True)

    rows = module.build_candidates(*paths)
    memory = next(row for row in rows if row["instance"] == "memory.ltl")

    assert memory["failure_kind"] == "memory_limit"
    assert memory["neighbour_instance"] == ""
    assert memory["neighbour_params_json"] == ""
    assert memory["neighbour_seconds"] == ""
    assert memory["neighbour_result"] == ""
    assert memory["pair_kind"] == "none"


def quota_candidates():
    rows = [candidate("a-top.ltl", "a", 100), candidate("a-second.ltl", "a", 90)]
    rows.extend(
        candidate(f"{family}.ltl", family, 30 - index)
        for index, family in enumerate("bcdefghij")
    )
    rows.append(candidate("memory.ltl", "memory", 1, failure_kind="memory_limit"))
    return rows


def quota_selection(module):
    return module.select_candidates(
        quota_candidates(), target_count=11, memory_quota=1
    )


def test_memory_quota_includes_candidate_below_the_scored_cut():
    module = load_module()

    selected = quota_selection(module)

    assert "memory.ltl" in {row["instance"] for row in selected}


def test_memory_quota_does_not_displace_a_sole_family_representative():
    module = load_module()

    selected = quota_selection(module)

    assert "a-second.ltl" not in {row["instance"] for row in selected}
    assert {row["family_key"] for row in selected} >= set("bcdefghij")


def test_selection_reason_distinguishes_score_and_memory_quota_rows():
    module = load_module()

    selected = quota_selection(module)
    reasons = {row["instance"]: row["selection_reason"] for row in selected}

    assert reasons["memory.ltl"] == "memory_quota"
    assert reasons["a-top.ltl"] == "score"


def test_memory_quota_keeps_the_requested_target_count():
    module = load_module()

    selected = quota_selection(module)

    assert len(selected) == 11


def test_memory_quota_uses_distinct_families_first_and_keeps_family_cap():
    module = load_module()
    candidates = [
        candidate("a-top.ltl", "a", 100),
        candidate("a-second.ltl", "a", 99),
        candidate("a-third.ltl", "a", 98),
        candidate("b-top.ltl", "b", 97),
        candidate("b-second.ltl", "b", 96),
        candidate("b-third.ltl", "b", 95),
    ]
    candidates.extend(
        candidate(f"{family}.ltl", family, score)
        for family, score in zip("cdefghij", range(94, 86, -1))
    )
    candidates.extend(
        [
            candidate("memory-a-top.ltl", "a", 20, failure_kind="memory_limit"),
            candidate("memory-a-second.ltl", "a", 19, failure_kind="memory_limit"),
            candidate("memory-x.ltl", "memory:x", 18, failure_kind="memory_limit"),
            candidate("memory-y.ltl", "memory:y", 17, failure_kind="memory_limit"),
            candidate("memory-z.ltl", "memory:z", 16, failure_kind="memory_limit"),
        ]
    )

    selected = module.select_candidates(candidates, target_count=14, memory_quota=4)
    quota_rows = [
        row for row in selected if row["selection_reason"] == "memory_quota"
    ]
    family_counts = Counter(row["family_key"] for row in selected)

    assert [row["family_key"] for row in quota_rows] == [
        "a",
        "memory:x",
        "memory:y",
        "memory:z",
    ]
    assert len(selected) == 14
    assert max(family_counts.values()) <= module.PER_FAMILY_CAP


def test_output_order_is_deterministic(tmp_path):
    module = load_module()
    candidates = [
        candidate("z.ltl", "z", 20, "unrealizable"),
        candidate("b.ltl", "b", 30, "realizable"),
        candidate("a.ltl", "a", 30),
    ]

    first = module.select_candidates(candidates, target_count=3)
    second = module.select_candidates(list(reversed(candidates)), target_count=3)

    assert [row["instance"] for row in first] == ["a.ltl", "b.ltl", "z.ltl"]
    assert first == second
