from __future__ import annotations

import csv
import json
import pathlib
import sys

import pytest

BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))

import family_metadata  # noqa: E402


def write_tsv(path: pathlib.Path, header: list[str], rows: list[list[str]]) -> None:
    with open(path, "w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def make_corpus(tmp_path: pathlib.Path, instances: list[tuple[str, str, str]]):
    """Build a miniature corpus.  Each entry is (logical, tlsf_file, origin)."""
    root = tmp_path / "corpus"
    root.mkdir(exist_ok=True)
    list_path = tmp_path / "all.list"
    list_path.write_text(
        "# comment\n\n" + "".join(f"{logical}\n" for logical, _, _ in instances)
    )
    write_tsv(tmp_path / "tlsf-sources.tsv", ["instance", "tlsf"],
              [[logical, tlsf] for logical, tlsf, _ in instances])
    write_tsv(tmp_path / "manifest.tsv", ["instance", "origin", "sha256"],
              [[tlsf, origin, "abc"] for _, tlsf, origin in instances])
    write_tsv(
        tmp_path / "conversion.tsv",
        ["instance", "inputs", "outputs", "semantics", "source_target",
         "effective_target"],
        [[tlsf, "2", "1", "Mealy", "Mealy", "Mealy"] for _, tlsf, _ in instances],
    )
    for _, tlsf, _ in instances:
        (root / tlsf).write_text("INFO {}\nMAIN {}\n")
    return dict(
        list_path=list_path,
        tlsf_sources=tmp_path / "tlsf-sources.tsv",
        manifest=tmp_path / "manifest.tsv",
        conversion=tmp_path / "conversion.tsv",
        corpus=root,
    )


def build(tmp_path, instances, **kwargs):
    paths = make_corpus(tmp_path, instances)
    return family_metadata.build(
        paths["list_path"], paths["tlsf_sources"], paths["manifest"],
        paths["conversion"], paths["corpus"],
        expected=kwargs.pop("expected", len(instances)), **kwargs,
    )


def test_exact_single_parameter_origin(tmp_path):
    rows = build(tmp_path, [("a.ltl", "a.tlsf", "param:tlsf/zoo/arb.tlsf:n=7")])
    row = rows[0]
    assert row["origin_kind"] == "param"
    assert row["parameter_confidence"] == "exact"
    assert row["family_key"] == "param:tlsf/zoo/arb.tlsf"
    assert row["family_display"] == "arb"
    assert row["parameter_names"] == "n"
    assert json.loads(row["parameter_values_json"]) == {"n": 7}
    assert row["parameter_dimension"] == 1
    assert row["parameters_numeric"] == "true"


def test_two_parameters_preserve_origin_order(tmp_path):
    rows = build(tmp_path, [("c.ltl", "c.tlsf", "param:tlsf/g/chomp.tlsf:N=2,M=5")])
    row = rows[0]
    assert row["parameter_names"] == "N,M"
    assert row["parameter_dimension"] == 2
    # Order matters: a componentwise comparison reads coordinates positionally.
    assert list(json.loads(row["parameter_values_json"])) == ["N", "M"]
    assert json.loads(row["parameter_values_json"]) == {"N": 2, "M": 5}


def test_nonnumeric_parameter_is_kept_but_not_orderable(tmp_path):
    rows = build(tmp_path, [("d.ltl", "d.tlsf", "param:tlsf/g/t.tlsf:mode=fast,n=3")])
    row = rows[0]
    assert json.loads(row["parameter_values_json"]) == {"mode": "fast", "n": 3}
    assert row["parameters_numeric"] == "false"
    assert family_metadata.orderable_families(rows) == set()


def test_decimal_parameter_parsed_as_float(tmp_path):
    rows = build(tmp_path, [("e.ltl", "e.tlsf", "param:tlsf/g/t.tlsf:p=0.5")])
    assert json.loads(rows[0]["parameter_values_json"]) == {"p": 0.5}
    assert rows[0]["parameters_numeric"] == "true"


def test_direct_origin_falls_back_to_filename_family(tmp_path):
    rows = build(tmp_path, [("robot_grid_4_4.ltl", "r.tlsf", "direct:tlsf/mine/r.tlsf")])
    row = rows[0]
    assert row["origin_kind"] == "direct"
    # A direct file has no parameters at all; claiming "heuristic" parameters
    # here would let a guess reach the frontier analysis as if it were data.
    assert row["parameter_confidence"] == "none"
    assert row["parameter_names"] == ""
    assert row["parameter_dimension"] == 0
    assert row["family_display"] == "robot_grid"
    assert row["family_key"] == "direct:tlsf/mine:robot_grid"


def test_orderable_requires_consistent_names_and_numeric_values(tmp_path):
    rows = build(tmp_path, [
        ("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n=1"),
        ("b.ltl", "b.tlsf", "param:tlsf/g/t.tlsf:n=2"),
    ])
    assert family_metadata.orderable_families(rows) == {"param:tlsf/g/t.tlsf"}

    mixed = build(tmp_path, [
        ("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n=1"),
        ("b.ltl", "b.tlsf", "param:tlsf/g/t.tlsf:m=2"),
    ])
    assert family_metadata.orderable_families(mixed) == set()


def test_missing_manifest_row_is_fatal(tmp_path):
    paths = make_corpus(tmp_path, [("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n=1")])
    write_tsv(paths["manifest"], ["instance", "origin", "sha256"], [])
    with pytest.raises(ValueError, match="no manifest entry"):
        family_metadata.build(paths["list_path"], paths["tlsf_sources"],
                              paths["manifest"], paths["conversion"],
                              paths["corpus"], expected=1)


def test_missing_tlsf_mapping_is_fatal(tmp_path):
    paths = make_corpus(tmp_path, [("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n=1")])
    write_tsv(paths["tlsf_sources"], ["instance", "tlsf"], [])
    with pytest.raises(ValueError, match="no TLSF mapping"):
        family_metadata.build(paths["list_path"], paths["tlsf_sources"],
                              paths["manifest"], paths["conversion"],
                              paths["corpus"], expected=1)


def test_conflicting_duplicate_mapping_is_fatal(tmp_path):
    paths = make_corpus(tmp_path, [("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n=1")])
    write_tsv(paths["tlsf_sources"], ["instance", "tlsf"],
              [["a.ltl", "a.tlsf"], ["a.ltl", "other.tlsf"]])
    with pytest.raises(ValueError, match="duplicate conflicting row"):
        family_metadata.build(paths["list_path"], paths["tlsf_sources"],
                              paths["manifest"], paths["conversion"],
                              paths["corpus"], expected=1)


def test_wrong_instance_count_is_fatal(tmp_path):
    paths = make_corpus(tmp_path, [("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n=1")])
    with pytest.raises(ValueError, match="expected 1524"):
        family_metadata.build(paths["list_path"], paths["tlsf_sources"],
                              paths["manifest"], paths["conversion"],
                              paths["corpus"], expected=1524)


def test_unknown_origin_kind_is_fatal(tmp_path):
    with pytest.raises(ValueError, match="unknown origin kind"):
        build(tmp_path, [("a.ltl", "a.tlsf", "mystery:tlsf/g/t.tlsf")])


def test_malformed_parameter_is_fatal(tmp_path):
    with pytest.raises(ValueError, match="malformed parameter"):
        build(tmp_path, [("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:n")])


def test_output_is_byte_stable(tmp_path):
    instances = [("a.ltl", "a.tlsf", "param:tlsf/g/t.tlsf:N=1,M=2")]
    first = build(tmp_path, instances)
    second = build(tmp_path, instances)
    assert first == second


# ---------------------------------------------------------------------------
# Integration against the committed corpus, skipped when it is not materialized.

ROOT = pathlib.Path(__file__).resolve().parents[2]
SUITE = ROOT / "tests" / "suites" / "benchmarks"
GENERATED = ROOT / "benchmarking" / "syntcomp26-family-instances.tsv"


@pytest.mark.skipif(not GENERATED.is_file(), reason="metadata not generated")
def test_generated_table_covers_the_official_set():
    with open(GENERATED, newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    assert len(rows) == family_metadata.EXPECTED_INSTANCES
    assert len({r["logical_instance"] for r in rows}) == len(rows)
    assert all(r["origin_kind"] in ("param", "direct") for r in rows)
    # Every exact row must carry a parseable parameter object.
    for row in rows:
        values = json.loads(row["parameter_values_json"])
        assert len(values) == int(row["parameter_dimension"])
        if row["parameter_confidence"] == "exact":
            assert row["origin"].startswith("param:")
            assert values
