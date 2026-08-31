"""Tests for benchmarking/semantic-action-census.py."""

import importlib.util
import pathlib

import pytest

SCRIPT = (
    pathlib.Path(__file__).resolve().parents[2] / "benchmarking/semantic-action-census.py"
)


def load_module():
    spec = importlib.util.spec_from_file_location("semantic_action_census", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def write_census(tmp_path, rows):
    path = tmp_path / "gap-census.tsv"
    header = "suite\tinstance\tset\tmechanism\n"
    body = "".join("\t".join(row) + "\n" for row in rows)
    path.write_text(header + body)
    return path


def test_instance_list_drops_comments_and_blanks(tmp_path):
    module = load_module()
    listing = tmp_path / "cohort.list"
    listing.write_text("# header\n\nfirst.ltl\nsecond.ltl  # trailing\n\n")

    assert module.read_instance_list(listing) == ["first.ltl", "second.ltl"]


def test_pairs_require_suite_equals_path():
    module = load_module()

    assert module.parse_pairs(["syntcomp24=a/b.tsv"], "--source-map") == {
        "syntcomp24": pathlib.Path("a/b.tsv")
    }
    with pytest.raises(SystemExit):
        module.parse_pairs(["syntcomp24"], "--source-map")


def test_cohort_filters_by_mechanism_prefix_and_groups_by_suite(tmp_path):
    module = load_module()
    census = write_census(
        tmp_path,
        [
            ("syntcomp24", "a.ltl", "acacia_slow", "M1 letter-loop"),
            ("syntcomp24", "b.ltl", "ltlsynt_only", "M2 downset"),
            ("syntcomp25", "c.ltl", "ltlsynt_only", "M1 letter-loop"),
            ("syntcomp25", "d.ltl", "ltlsynt_only", "M3 translation-stall"),
        ],
    )

    assert module.census_cohort(census, ["M1"]) == {
        "syntcomp24": ["a.ltl"],
        "syntcomp25": ["c.ltl"],
    }
    assert module.census_cohort(census, ["M1", "M2"]) == {
        "syntcomp24": ["a.ltl", "b.ltl"],
        "syntcomp25": ["c.ltl"],
    }
    # No mechanism filter keeps every row.
    assert sorted(module.census_cohort(census, [])) == ["syntcomp24", "syntcomp25"]


def test_cohort_deduplicates_instances_repeated_within_a_suite(tmp_path):
    """The gap census carries one row per (suite, instance, set), so the same
    instance can appear twice; running it twice would double the census cost
    and write contradictory duplicate rows."""
    module = load_module()
    census = write_census(
        tmp_path,
        [
            ("syntcomp24", "a.ltl", "acacia_slow", "M1 letter-loop"),
            ("syntcomp24", "a.ltl", "ltlsynt_only", "M1 letter-loop"),
        ],
    )

    assert module.census_cohort(census, ["M1"]) == {"syntcomp24": ["a.ltl"]}


def test_cohort_rejects_a_table_without_a_mechanism_column(tmp_path):
    module = load_module()
    path = tmp_path / "not-a-census.tsv"
    path.write_text("suite\tinstance\nsyntcomp24\ta.ltl\n")

    with pytest.raises(SystemExit):
        module.census_cohort(path, ["M1"])


def test_census_rows_take_one_row_per_worker_from_the_right_checkpoint():
    module = load_module()
    rows = [
        {"checkpoint": "after-rsimp", "path": "real"},
        {"checkpoint": "alphabet-census", "path": "real", "alphabet_output_paths": "491"},
        {"checkpoint": "alphabet-census", "path": "real", "alphabet_output_paths": "999"},
        {"checkpoint": "alphabet-census", "path": "unreal-formula"},
        {"checkpoint": "final", "path": "real", "decoded_transition_sets": "43"},
    ]

    census = module.census_rows(rows, decode=False)
    assert [row["path"] for row in census] == ["real", "unreal-formula"]
    # First occurrence wins, so a later progress dump cannot overwrite the count.
    assert census[0]["alphabet_output_paths"] == "491"

    decode = module.census_rows(rows, decode=True)
    assert [row["path"] for row in decode] == ["real"]
    assert decode[0]["decoded_transition_sets"] == "43"


def test_column_order_starts_with_the_gap_census_join_key():
    """The census must join gap-census.tsv on (suite, instance)."""
    module = load_module()

    assert module.COLUMNS[:2] == ["suite", "instance"]
    assert module.COLUMNS[-1] == "result"
    for column in ("raw_output_paths", "unique_residual_roots", "minimal_residual_roots"):
        assert column in module.COLUMNS
