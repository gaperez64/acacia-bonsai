"""Synthetic checks for consolidated command ownership and converter provenance."""
import csv
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_tool  # noqa: E402


@pytest.mark.parametrize("tool,mode", [
    ("benchmarking/run-syntcomp26-coverage.py", "subset"),
    ("benchmarking/cactus-report.py", "threeway"),
    ("benchmarking/cactus-report.py", "classify"),
    ("benchmarking/legacy-phase-table.py", "completeness"),
    ("benchmarking/convert-tlsf-corpus.py", "native"),
])
def test_modes_parse_without_execution(tool, mode):
    completed = subprocess.run([sys.executable, str(ROOT / tool), mode, "--help"],
                               capture_output=True, text=True, timeout=8,
                               env={**os.environ, "ACACIA_CAMPAIGN_SCOPE_GUARD": "pytest"})
    assert completed.returncode == 0, completed.stderr
    assert "usage:" in completed.stdout


@pytest.mark.parametrize("native", [False, True])
def test_conversion_routes_share_selection_cleanup_and_skip_contract(tmp_path, monkeypatch, native):
    converter = load_tool("benchmarking/convert-tlsf-corpus.py")
    source, output = tmp_path / "source", tmp_path / "pairs"
    source.mkdir()
    for name in ("good", "bad", "unselected"):
        (source / f"{name}.tlsf").write_text("synthetic TLSF\n")
    selection = tmp_path / "panel.list"
    selection.write_text("good\nbad\n")
    output.mkdir()
    for suffix in ("ltl", "part"):
        (output / f"bad.{suffix}").write_text("stale conversion")
    def inspect(executable, path, timeout):
        if path.stem == "bad":
            raise RuntimeError("synthetic parse failure")
        return "G out", "inp", "out", "Moore", "Moore", "Mealy"
    def syfco(executable, path, part, timeout, target, canonicalizer):
        if path.stem == "bad":
            raise ValueError("synthetic parse failure")
        part.write_text(".inputs inp\n.outputs out\n")
        return "G out"
    monkeypatch.setattr(converter, "native_inspect", inspect)
    monkeypatch.setattr(converter, "convert_pair", syfco)
    args = [str(source), str(output), "--selection", str(selection)]
    if native:
        inspector = tmp_path / "inspector"
        inspector.touch()
        args += ["--native-inspect", str(inspector)]
    assert converter.main(args) == 1
    assert (output / "good.ltl").read_text() == "G out\n"
    assert (output / "good.part").read_text() == ".inputs inp\n.outputs out\n"
    assert not (output / "bad.ltl").exists() and not (output / "bad.part").exists()
    assert not (output / "unselected.ltl").exists()
    with (output / "skipped.tsv").open() as stream:
        skipped, = csv.DictReader(stream, delimiter="\t")
    assert skipped == {"instance": "bad.tlsf", "reason": "synthetic parse failure"}
    if native:
        with (output / "conversion.tsv").open() as stream:
            row, = csv.DictReader(stream, delimiter="\t")
        assert row["source_target"] == "Moore" and row["effective_target"] == "Mealy"
        assert row["formula_sha256"] == converter.sha256((output / "good.ltl").read_bytes())


def test_retained_native_converter_is_a_compatibility_entry():
    path = ROOT / "benchmarking/convert-tlsf-corpus-native.py"
    spec = importlib.util.spec_from_file_location("native_conversion_compatibility", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    owner = load_tool("benchmarking/convert-tlsf-corpus.py")
    assert module.selected_sources is owner.selected_sources
    assert module.inspect is owner.native_inspect


@pytest.mark.parametrize("status", [1, 3])
def test_callable_dispatch_propagates_failure_and_restores_arguments(monkeypatch, status):
    from benchlib import dispatch_mode
    original = ["tool", "subset", "--input", "example"]
    monkeypatch.setattr(sys, "argv", original)
    def failed():
        assert sys.argv == ["tool subset", "--input", "example"]
        return status
    assert dispatch_mode({"subset": failed}) == status
    assert sys.argv is original


def test_evidence_dispatch_propagates_handler_returncode(monkeypatch, tmp_path):
    evidence = load_tool("scripts/acacia-evidence.py")
    monkeypatch.setattr(evidence, "pack", lambda args: 7)
    monkeypatch.setattr(sys, "argv", ["acacia-evidence.py", "pack", "--campaign", "test",
                                    "--status", "partial", "--members", "README.md",
                                    "--out", str(tmp_path / "unused.tar.gz")])
    assert evidence.main() == 7
