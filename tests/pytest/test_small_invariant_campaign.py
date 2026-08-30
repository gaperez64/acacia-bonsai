"""Tests for benchmarking/small-invariant-campaign.py."""

import importlib.util
import pathlib

import pytest

SCRIPT = (
    pathlib.Path(__file__).resolve().parents[2]
    / "benchmarking/small-invariant-campaign.py"
)


def load_module():
    spec = importlib.util.spec_from_file_location("small_invariant_campaign", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_meta_is_read_as_a_two_line_table(tmp_path):
    module = load_module()
    (tmp_path / "meta.tsv").write_text(
        "schema_version\tstates\tbool_threshold\tinit_state\tworker\tinstance\n"
        "3\t437\t129\t129\treal\tlift4.ltl\n"
    )

    meta = module.read_meta(tmp_path)
    assert meta["states"] == "437"
    assert meta["worker"] == "real"
    assert meta["init_state"] == "129"


def test_meta_missing_its_row_is_empty_rather_than_a_crash(tmp_path):
    module = load_module()
    (tmp_path / "meta.tsv").write_text("schema_version\tstates\n")

    assert module.read_meta(tmp_path) == {}


def test_table_shape_comes_from_the_header_comment(tmp_path):
    module = load_module()
    (tmp_path / "all-input-actions.tsv").write_text(
        "# schema_version=1 inputs=25 actions=400 transitions=19000\n[input\t0]\n"
    )

    assert module.table_shape(tmp_path) == ("25", "400")


def test_table_shape_reports_zero_when_the_dump_was_declined(tmp_path):
    """A declined dump leaves only a .skipped file; the row must still be
    written, with the absence visible rather than the instance dropped."""
    module = load_module()
    (tmp_path / "all-input-actions.tsv.skipped").write_text("actions=999999 cap=65536\n")

    assert module.table_shape(tmp_path) == ("0", "0")


def test_solver_final_reports_none_when_the_run_did_not_converge(tmp_path):
    module = load_module()
    assert module.solver_final(tmp_path) == "none"

    (tmp_path / "antichain-final.tsv").write_text(
        "# k=2 loop=44 maxima=6343 after_bound_raise=0 final=1\n"
    )
    assert module.solver_final(tmp_path) == "6343"


def test_cohort_filters_by_mechanism_and_deduplicates(tmp_path):
    module = load_module()
    census = tmp_path / "gap-census.tsv"
    census.write_text(
        "suite\tinstance\tset\tmechanism\n"
        "syntcomp24\ta.ltl\tacacia_slow\tM2 downset\n"
        "syntcomp24\ta.ltl\tltlsynt_only\tM2 downset\n"
        "syntcomp24\tb.ltl\tltlsynt_only\tM1 letter-loop\n"
        "syntcomp25\tc.ltl\tltlsynt_only\tM2 downset\n"
    )

    assert module.census_cohort(census, ["M2"]) == {
        "syntcomp24": ["a.ltl"],
        "syntcomp25": ["c.ltl"],
    }


def test_pairs_require_suite_equals_path():
    module = load_module()

    assert module.parse_pairs(["syntcomp24=a.tsv"], "--source-map") == {
        "syntcomp24": pathlib.Path("a.tsv")
    }
    with pytest.raises(SystemExit):
        module.parse_pairs(["syntcomp24"], "--source-map")


def test_columns_start_with_the_join_key_and_carry_the_probe_fields():
    module = load_module()

    assert module.COLUMNS[:3] == ["suite", "instance", "worker"]
    for column in ("checkpoint_maxima", "core_maxima", "core_contains_init", "verified"):
        assert column in module.COLUMNS
    # The solver's own outcome has to be on the row: a core is only interesting
    # relative to how big the region became without one.
    assert "solver_final_maxima" in module.COLUMNS
