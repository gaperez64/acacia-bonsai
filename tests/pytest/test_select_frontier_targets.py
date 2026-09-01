from __future__ import annotations

import csv
import importlib.util
import pathlib
import sys


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


def candidate(instance, family, score, expectation="none"):
    return {
        "instance": instance,
        "family_key": family,
        "score": str(score),
        "expectation": expectation,
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


def miniature_inputs(tmp_path):
    summary_fields = list(summary_row())
    summaries = [
        summary_row(),
        {
            **summary_row("easy.ltl", result="REALIZABLE"),
            "parameter_values_json": '{"n":1}',
            "tlsf_bytes": "1024",
        },
    ]
    summary = tmp_path / "summary.tsv"
    write_tsv(summary, summary_fields, summaries)

    frontiers = tmp_path / "frontiers.tsv"
    write_tsv(frontiers, list(frontier_row()), [frontier_row()])

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
