"""Tests for benchmarking/semantic-action-report.py."""

import importlib.util
import pathlib

import pytest

SCRIPT = (
    pathlib.Path(__file__).resolve().parents[2] / "benchmarking/semantic-action-report.py"
)

CENSUS_HEADER = (
    "suite\tinstance\tworker\tmode\troute\taut_states\tinput_classes\traw_output_paths\t"
    "unique_residual_roots\tminimal_residual_roots\tmax_output_paths\tmax_residual_roots\t"
    "decoded_transition_sets\tunique_action_vecs\trelation_bdd_nodes\tdominance_tests\t"
    "dominance_declines\tcensus_ms\tdominance_ms\tdecode_ms\tmax_f\tactions_seen\tresult\n"
)


def load_module():
    spec = importlib.util.spec_from_file_location("semantic_action_report", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def census_row(suite, instance, worker, paths, roots, minimal, declines=0):
    values = [suite, instance, worker, "census", "ltl", "10", "4", str(paths), str(roots),
              str(minimal), "0", "0", "0", "0", "0", "0", str(declines), "0", "0", "0",
              "0", "0", "solved"]
    return "\t".join(values) + "\n"


def write_census(tmp_path, rows):
    path = tmp_path / "semantic-action-census.tsv"
    path.write_text(CENSUS_HEADER + "".join(rows))
    return path


@pytest.mark.parametrize(
    "instance,expected",
    [
        # A trailing index, a _pb_N_pe_ block and a content hash are three
        # different ways syntcomp spells the same family; all must collapse, or
        # every "at least two families" clause passes trivially.
        ("amba_decomposed_lock16.ltl", "amba_decomposed_lock"),
        ("amba_decomposed_lock_pb_16_pe_.ltl", "amba_decomposed_lock"),
        ("workstation_resupply_pb_3_pe_.ltl", "workstation_resupply"),
        ("Morning_f2774e0b.ltl", "Morning"),
        ("prioritized_arbiter7.ltl", "prioritized_arbiter"),
        ("f-real-real.ltl", "f-real-real"),
        ("OneCounter.ltl", "OneCounter"),
    ],
)
def test_family_collapses_every_parameter_spelling(instance, expected):
    assert load_module().family(instance) == expected


def test_workers_without_a_census_are_not_counted(tmp_path):
    """A worker that never reached the precomputer has no ratio; counting it as
    1.0 would dilute the gate, and dividing by its zero roots would crash."""
    module = load_module()
    census = write_census(
        tmp_path,
        [
            census_row("syntcomp24", "a.ltl", "none", 0, 0, 0),
            census_row("syntcomp24", "b.ltl", "real", 100, 0, 0),
            census_row("syntcomp24", "c.ltl", "real", 100, 50, 50),
        ],
    )

    workers = module.load(census, None)
    assert [w["instance"] for w in workers] == ["c.ltl"]
    assert workers[0]["equality"] == 2.0


def test_dominance_is_zero_when_the_pass_did_not_run(tmp_path):
    module = load_module()
    census = write_census(tmp_path, [census_row("syntcomp24", "a.ltl", "real", 100, 50, 0)])

    worker = module.load(census, None)[0]
    assert worker["dominance"] == 0.0
    assert worker["equality"] == 2.0


def test_declines_are_carried_through_so_a_bailout_is_visible(tmp_path):
    module = load_module()
    census = write_census(
        tmp_path,
        [
            census_row("syntcomp24", "a.ltl", "real", 100, 50, 50, declines=1),
            census_row("syntcomp24", "b.ltl", "real", 100, 50, 50, declines=0),
        ],
    )

    workers = module.load(census, None)
    assert [w["declined"] for w in workers] == [True, False]


def test_mechanism_is_joined_from_the_gap_census(tmp_path):
    module = load_module()
    census = write_census(tmp_path, [census_row("syntcomp24", "a.ltl", "real", 100, 50, 50)])
    gap = tmp_path / "gap-census.tsv"
    gap.write_text(
        "suite\tinstance\tset\tmechanism\n"
        "syntcomp24\ta.ltl\tacacia_slow\tM1 letter-loop\n"
    )

    assert module.load(census, gap)[0]["mechanism"] == "M1 letter-loop"
    # Absent from the gap census is reported, not guessed.
    assert module.load(census, None)[0]["mechanism"] == "?"


def test_histogram_counts_at_or_above_each_threshold():
    module = load_module()

    assert module.histogram([1.0, 2.0, 8.0, 9.0], (2, 8)) == [(2, 3), (8, 2)]
