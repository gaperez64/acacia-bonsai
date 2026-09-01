from __future__ import annotations

import importlib.util
import pathlib
import sys
from types import SimpleNamespace

import pytest


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "run_diag_targets.py"


def load_run_diag_targets():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("run_diag_targets_tlsf", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_headered_tlsf_map_does_not_treat_header_as_data(tmp_path):
    module = load_run_diag_targets()
    source_map = tmp_path / "tlsf-sources.tsv"
    source_map.write_text("instance\ttlsf\nexample.ltl\texample.tlsf\n")
    corpus = tmp_path / "corpus"

    assert module.read_tlsf_map(source_map, corpus) == {
        "example.ltl": corpus / "example.tlsf"
    }


def test_missing_logical_name_error_names_the_instance(tmp_path):
    module = load_run_diag_targets()

    with pytest.raises(ValueError, match="missing\\.ltl"):
        module.resolve_tlsf_target("missing.ltl", {})


def _successful_run():
    return SimpleNamespace(
        returncode=0,
        timed_out=False,
        stdout="ACACIA_DIAG diag_kind=final result=realizable\n",
        stderr="",
        stdout_bytes=57,
        stderr_bytes=0,
    )


def _run_main(module, monkeypatch, argv):
    commands = []

    def fake_run(command, _timeout, **_kwargs):
        commands.append(command)
        return _successful_run()

    monkeypatch.setattr(module, "run_process_group", fake_run)
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), *argv])
    assert module.main() == 0
    return commands


def test_native_tlsf_command_uses_flags_and_never_reads_a_partition(
    tmp_path, monkeypatch
):
    module = load_run_diag_targets()
    build = tmp_path / "build"
    binary = build / "src" / "acacia-bonsai"
    binary.parent.mkdir(parents=True)
    binary.touch()
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    tlsf = corpus / "example.tlsf"
    tlsf.write_text("INFO {}\nMAIN {}\n")
    source_map = tmp_path / "tlsf-sources.tsv"
    source_map.write_text("instance\ttlsf\nexample.ltl\texample.tlsf\n")
    monkeypatch.setattr(
        module,
        "read_part",
        lambda _path: pytest.fail("native TLSF must not read or convert a .part file"),
    )

    commands = _run_main(
        module,
        monkeypatch,
        [
            "--build",
            str(build),
            "--tlsf-map",
            str(source_map),
            "--tlsf-corpus",
            str(corpus),
            "--flags=-u automaton",
            "--csv",
            str(tmp_path / "diagnostics.csv"),
            "example.ltl",
        ],
    )

    assert commands == [[str(binary), "-u", "automaton", "-T", str(tlsf)]]


def test_missing_mapped_tlsf_file_is_fatal_and_names_path(tmp_path):
    module = load_run_diag_targets()
    missing = tmp_path / "corpus" / "missing.tlsf"

    with pytest.raises(FileNotFoundError, match=str(missing)):
        module.resolve_tlsf_target("example.ltl", {"example.ltl": missing})


def test_legacy_ltl_part_command_still_works_without_tlsf_map(
    tmp_path, monkeypatch
):
    module = load_run_diag_targets()
    build = tmp_path / "build"
    binary = build / "src" / "acacia-bonsai"
    binary.parent.mkdir(parents=True)
    binary.touch()
    suite = tmp_path / "suite"
    suite.mkdir()
    ltl = suite / "legacy.ltl"
    ltl.write_text("G(i -> o)\n")
    ltl.with_suffix(".part").write_text(".inputs i\n.outputs o\n")

    commands = _run_main(
        module,
        monkeypatch,
        [
            "--build",
            str(build),
            "--suite-dir",
            str(suite),
            "--flags=-u automaton",
            "--csv",
            str(tmp_path / "diagnostics.csv"),
            "legacy.ltl",
        ],
    )

    assert commands == [
        [
            str(binary),
            "-F",
            str(ltl),
            "-i",
            "i",
            "-o",
            "o",
            "-u",
            "automaton",
        ]
    ]
