"""Strict SyFCo references preserve source timing before target adaptation."""

import importlib.util
import pathlib
import shutil
import subprocess
import sys

import pytest


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))
from tlsf_pairs import convert_pair  # noqa: E402


@pytest.mark.parametrize("model", ["Mealy", "Moore"])
@pytest.mark.parametrize("strict", [False, True])
@pytest.mark.parametrize("target", [None, "Mealy", "Moore"])
def test_pair_rewrites_strict_semantics_before_target_adaptation(
    monkeypatch, tmp_path, model, strict, target,
):
    source, part = tmp_path / "source.tlsf", tmp_path / "source.part"
    commands = []
    plain_formula = "G(i -> o)\n"
    strict_formula = "(o U !i) || G o\n"
    expected_formula = strict_formula if strict else plain_formula

    def run(cmd, **kwargs):
        commands.append(cmd)
        if "--print-semantics" in cmd:
            output = f"{model},Strict\n" if strict else f"{model}\n"
        elif cmd[0] == "canonicalizer":
            assert kwargs["input"] == expected_formula
            assert cmd[1:] == ["--delay-aps", "i" if model == "Moore" else "o"]
            output = "adapted formula\n"
        else:
            flags = ["--overwrite-semantics", model, "-pxo"] if strict else []
            assert cmd == ["syfco", "--format", "ltlxba", "--mode", "fully",
                           *flags, "--part-file", str(part), str(source)]
            part.write_text(".inputs i\n.outputs o\n")
            output = strict_formula if "--overwrite-semantics" in cmd else plain_formula
        return subprocess.CompletedProcess(cmd, 0, output, "")

    monkeypatch.setattr(subprocess, "run", run)
    formula, converter, error = convert_pair("syfco", source, part, 10, target, "canonicalizer")
    adapted = target is not None and model != target
    assert formula == ("adapted formula\n" if adapted else expected_formula)
    assert (converter, error) == ("syfco", "")
    assert len(commands) == (3 if adapted else 2)


@pytest.mark.parametrize("model", ["Mealy", "Moore"])
@pytest.mark.parametrize("strict", [False, True])
@pytest.mark.parametrize("route", ["ltlsynt", "g5"])
def test_real_syfco_strict_formula_and_unchanged_non_strict_output(tmp_path, model, strict, route):
    syfco = shutil.which("syfco")
    if syfco is None:
        pytest.skip("SyFCo is optional for the conversion integration test")
    source = tmp_path / "source.tlsf"
    semantics = f"Strict,{model}" if strict else model
    # F X o exercises the strong-next printer failure addressed by -pxo.
    guarantee = "F X o" if strict else "F o"
    source.write_text(
        f'INFO {{ TITLE: "strict" DESCRIPTION: "regression" SEMANTICS: {semantics} '
        'TARGET: Mealy }\nMAIN { INPUTS { i; } OUTPUTS { o; } '
        f'REQUIRE {{ i; }} ASSERT {{ o; }} GUARANTEES {{ {guarantee}; }} }}\n'
    )
    if route == "ltlsynt":
        spec = importlib.util.spec_from_file_location(
            "strict_coverage", BENCHMARKING / "run-syntcomp26-coverage.py")
        coverage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(coverage)
        ltl, declared_model = coverage.subset_convert_tlsf(syfco, source, tmp_path)
        assert declared_model == model
        formula = ltl.read_text()
    else:
        formula, converter, error = convert_pair(syfco, source, tmp_path / "source.part", 10)
        assert (converter, error) == ("syfco", "")
    if strict:
        assert formula == (
            "((((o) U (! (i))) || (G (o))) && ((G (i)) -> (X (F (o)))))\n"
        )
    else:
        original = subprocess.run(
            [syfco, "--format", "ltlxba", "--mode", "fully", str(source)],
            check=True, capture_output=True, text=True, timeout=10,
        ).stdout
        assert formula == original.rstrip() + "\n"
