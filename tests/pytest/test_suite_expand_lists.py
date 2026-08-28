from __future__ import annotations

import importlib.util
import io
import pathlib

import pytest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "suites" / "expand-lists.py"


def load_expand_lists():
    spec = importlib.util.spec_from_file_location("expand_lists", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_parse_list_ignores_comments_and_blank_lines(tmp_path):
    module = load_expand_lists()
    path = tmp_path / "suite.list"
    path.write_text(
        """
        # comment
        first.ltl

        second.ltl  # trailing comment
        """,
    )

    assert module.parse_list(path) == ["first.ltl", "second.ltl"]


def test_parse_list_includes_relative_files(tmp_path):
    module = load_expand_lists()
    (tmp_path / "main.list").write_text("before.ltl\n@include other\nafter.ltl\n")
    (tmp_path / "other.list").write_text("included.ltl\n")

    assert module.parse_list(tmp_path / "main.list") == [
        "before.ltl",
        "included.ltl",
        "after.ltl",
    ]


def test_parse_list_include_cycles_stop(tmp_path):
    module = load_expand_lists()
    (tmp_path / "a.list").write_text("a.ltl\n@include b\n")
    (tmp_path / "b.list").write_text("b.ltl\n@include a\n")

    assert module.parse_list(tmp_path / "a.list") == ["a.ltl", "b.ltl"]


def test_skipped_suites_emit_skipped_records(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    tests_dir = root / "tests" / "realizable"
    bench_dir = root / "benchmarks" / "realizable"
    tests_dir.mkdir(parents=True)
    bench_dir.mkdir(parents=True)
    (tests_dir / "!skip.list").write_text("test.ltl\n")
    (bench_dir / "!skip.list").write_text("bench.ltl\n")
    module.ROOT = root

    out = io.StringIO()
    module.main(out)

    assert out.getvalue().splitlines() == [
        "ST\trealizable\t!skip\ttest.ltl\trealizable/test.ltl\t",
        "SB\trealizable\t!skip\tbench.ltl\trealizable/bench.ltl\t",
    ]


def test_source_map_redirects_storage_without_changing_logical_name(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "benchmarks" / "syntcomp26"
    folder.mkdir(parents=True)
    (folder / "panel.list").write_text("Alarm.ltl\n")
    (folder / "sources.tsv").write_text(
        "instance\tsource\nAlarm.ltl\tsyntcomp/abc.ltl\n"
    )
    module.ROOT = root

    out = io.StringIO()
    module.main(out)

    assert out.getvalue().splitlines() == [
        "B\tsyntcomp26\tpanel\tAlarm.ltl\tsyntcomp/abc.ltl\t"
    ]


def test_source_map_requires_every_listed_instance(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "benchmarks" / "syntcomp26"
    folder.mkdir(parents=True)
    (folder / "panel.list").write_text("missing.ltl\n")
    (folder / "sources.tsv").write_text("instance\tsource\n")
    module.ROOT = root

    with pytest.raises(KeyError, match="no source for 'missing.ltl'"):
        module.main(io.StringIO())


def test_records_have_six_tab_separated_fields(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "tests" / "realizable"
    folder.mkdir(parents=True)
    (folder / "smoke.list").write_text("test.ltl\n")
    module.ROOT = root

    out = io.StringIO()
    module.main(out)

    assert out.getvalue().splitlines()[0].split("\t") == [
        "T",
        "realizable",
        "smoke",
        "test.ltl",
        "realizable/test.ltl",
        "",
    ]


def test_tlsf_source_allows_missing_ltl_source(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "benchmarks" / "syntcomp26"
    folder.mkdir(parents=True)
    (folder / "panel.list").write_text("Alarm.ltl\n")
    (folder / "sources.tsv").write_text("instance\tsource\n")
    (folder / "tlsf-sources.tsv").write_text(
        "instance\ttlsf\nAlarm.ltl\tAlarm.tlsf\n"
    )
    module.ROOT = root

    out = io.StringIO()
    module.main(out)

    assert out.getvalue().splitlines() == [
        "B\tsyntcomp26\tpanel\tAlarm.ltl\t\tAlarm.tlsf"
    ]


def test_tlsf_source_without_ltl_source_map_emits_empty_ltl_field(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "benchmarks" / "syntcomp26"
    folder.mkdir(parents=True)
    (folder / "panel.list").write_text("Alarm.ltl\n")
    (folder / "tlsf-sources.tsv").write_text(
        "instance\ttlsf\nAlarm.ltl\tAlarm.tlsf\n"
    )
    module.ROOT = root

    out = io.StringIO()
    module.main(out)

    assert out.getvalue().splitlines() == [
        "B\tsyntcomp26\tpanel\tAlarm.ltl\t\tAlarm.tlsf"
    ]


def test_ltl_only_source_emits_empty_tlsf_field(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "benchmarks" / "syntcomp26"
    folder.mkdir(parents=True)
    (folder / "panel.list").write_text("Alarm.ltl\n")
    (folder / "sources.tsv").write_text(
        "instance\tsource\nAlarm.ltl\tsyntcomp/abc.ltl\n"
    )
    (folder / "tlsf-sources.tsv").write_text("instance\ttlsf\n")
    module.ROOT = root

    out = io.StringIO()
    module.main(out)

    assert out.getvalue().splitlines() == [
        "B\tsyntcomp26\tpanel\tAlarm.ltl\tsyntcomp/abc.ltl\t"
    ]


def test_tlsf_source_map_rejects_unsafe_paths(tmp_path):
    module = load_expand_lists()
    root = tmp_path / "suites"
    folder = root / "benchmarks" / "syntcomp26"
    folder.mkdir(parents=True)
    (folder / "panel.list").write_text("Alarm.ltl\n")
    (folder / "sources.tsv").write_text("instance\tsource\n")
    module.ROOT = root

    for invalid in ("/corpus/Alarm.tlsf", "nested/../Alarm.tlsf"):
        (folder / "tlsf-sources.tsv").write_text(
            f"instance\ttlsf\nAlarm.ltl\t{invalid}\n"
        )
        with pytest.raises(ValueError, match="invalid tlsf"):
            module.main(io.StringIO())
