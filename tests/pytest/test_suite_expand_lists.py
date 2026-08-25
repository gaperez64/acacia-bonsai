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
        "ST\trealizable\t!skip\ttest.ltl\trealizable/test.ltl",
        "SB\trealizable\t!skip\tbench.ltl\trealizable/bench.ltl",
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
        "B\tsyntcomp26\tpanel\tAlarm.ltl\tsyntcomp/abc.ltl"
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
