"""Regression checks for the four #211 classifications; no timed campaign."""
import csv
import importlib.util
import pathlib
import subprocess
import sys
from types import SimpleNamespace

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"),
                                                 ROOT / "benchmarking" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


g4 = load("corpus-correctness-gate")
conversion = load("check-tlsf-conversion")
parity = load("tlsf-verdict-parity")
score = load("solver-profile-score")


def test_g4_allows_timeouts_but_not_hidden_failure_or_false_verdict():
    records = [{"name": "control", "result": "OK"},
               {"name": "bounded", "result": "TIMEOUT"}]
    assert g4.classify(records, 1)[:2] == (True, 1)
    assert not g4.classify(records, 125)[0]
    assert not g4.classify(records + [{"result": "FAIL"}], 2)[0]
    for marker in ("FALSE POSITIVE", "FALSE NEGATIVE", "FALSE VERDICT"):
        assert not g4.classify(records + [{"result": "TIMEOUT", "stderr": marker}], 2)[0]
    assert not g4.classify([], 0)[0]


def test_g2s_membership_rule_uses_same_regression_ceiling_without_speedup_requirement():
    values = {("baseline", "control", "target"): 100,
              ("candidate", "control", "target"): 100}
    assert not score.score(values, 5, 6, "optimization")[0]
    passed, messages = score.score(values, 5, 6, "membership-default")
    assert passed
    assert "rule=membership-default" in messages[0]
    values[("candidate", "control", "target")] = 107
    assert not score.score(values, 5, 6, "membership-default")[0]


def test_g5_runs_available_pair_after_preparation_and_spot_check_fail(monkeypatch, tmp_path):
    source = tmp_path / "source"
    pairs = tmp_path / "pairs"
    source.mkdir()
    pairs.mkdir()
    for name in ("good", "missing"):
        (source / f"{name}.tlsf").write_text("generated fixture")
    (pairs / "good.ltl").write_text("G(o <-> i)\n")
    (pairs / "good.part").write_text(".inputs i\n.outputs o\n")
    selection = tmp_path / "all.list"
    selection.write_text("good.ltl\nmissing.ltl\n")
    prep_calls = []
    def prepare(cmd, **kwargs):
        prep_calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 1)
    monkeypatch.setattr(parity.subprocess, "run", prepare)
    solver_calls = []
    def solver(cmd, *args, **kwargs):
        solver_calls.append(cmd)
        return SimpleNamespace(stdout="REALIZABLE\n", stderr="", returncode=0,
                               seconds=0.1, timed_out=False, resource_limited=False)
    monkeypatch.setattr(parity, "run_systemd_scope", solver)
    binary = tmp_path / "solver"
    binary.touch()
    summary = tmp_path / "summary"
    monkeypatch.setattr(sys, "argv", ["parity", "--prepare", "--bin", str(binary),
                        "--source", str(source), "--converted", str(pairs),
                        "--list", str(selection), "--expect-count", "2",
                        "--native-inspect", "inspect", "--canonicalizer", "canonical",
                        "--csv", str(tmp_path / "rows.csv"), "--summary", str(summary),
                        "--status", str(tmp_path / "status")])
    assert parity.main() == 1
    assert len(prep_calls) == 2 and len(solver_calls) == 2
    assert "missing conversions: 1" in summary.read_text()
    assert "spot-check exit: 1" in summary.read_text()
    assert "preparation exit: 1" in summary.read_text()


@pytest.mark.parametrize("semantics", ["Mealy", "Moore", "Mealy,Strict"])
def test_target_adapted_comparison_and_unknown_timeout(monkeypatch, tmp_path, semantics):
    source = tmp_path / "source"
    pairs = tmp_path / "pairs"
    source.mkdir()
    pairs.mkdir()
    spec = source / "control.tlsf"
    spec.write_text('INFO { TITLE: "control" DESCRIPTION: "test" SEMANTICS: '
                    + semantics + ' TARGET: Moore }\nMAIN { INPUTS { i; } OUTPUTS { o; } '
                    'GUARANTEES { G(o <-> i); } }\n')
    formula = "G(o <-> X i)" if semantics.startswith("Moore") else "G(o <-> i)"
    (pairs / "control.ltl").write_text(formula + "\n")
    (pairs / "control.part").write_text(".inputs i\n.outputs o\n")
    commands = []
    def syfco(syfco, source, part, timeout, target, canonicalizer):
        commands.append(target)
        assert target == "Mealy"
        part.write_text(".inputs i\n.outputs o\n")
        return formula + "\n"
    monkeypatch.setattr(conversion, "convert_pair", syfco)
    monkeypatch.setattr(conversion, "inspect_native", lambda *args: (formula, "i", "o"))
    monkeypatch.setattr(conversion, "formula_keys", lambda *args: (b"adapted", b"adapted"))
    report = tmp_path / "report.tsv"
    argv = [str(source), str(pairs), "--native-inspect", "inspect",
            "--canonicalizer", "canonical", "--report", str(report)]
    assert conversion.main(argv) == 0
    def timeout(*args):
        raise subprocess.TimeoutExpired("ltlfilt", 1)
    monkeypatch.setattr(conversion, "formula_keys", timeout)
    assert conversion.main(argv) == 1
    rows = list(csv.DictReader(report.open(), delimiter="\t"))
    assert rows[0]["outcome"] == "UNKNOWN"
    assert rows[0]["formula_ast_match"] == ""


@pytest.mark.parametrize("semantics,target", [("Mealy", "Mealy"), ("Moore", "Moore"),
                                              ("Strict,Mealy", "Moore")])
def test_real_target_adaptation_tools(tmp_path, semantics, target):
    import os
    build = pathlib.Path(os.environ.get("ACACIA_GATE_TEST_BUILD",
                                        ROOT / "build"))
    inspector = build / "tests/tlsf-frontend-inspect"
    canonicalizer = build / "tests/ltl-formula-canonicalize"
    if not inspector.is_file() or not canonicalizer.is_file():
        pytest.skip("set ACACIA_GATE_TEST_BUILD to a build with TLSF gate helpers")
    source = tmp_path / "source"
    pairs = tmp_path / "pairs"
    source.mkdir()
    pairs.mkdir()
    (source / "timing.tlsf").write_text(
        f'INFO {{ TITLE: "timing" DESCRIPTION: "generated" SEMANTICS: {semantics} '
        f'TARGET: {target} }}\nMAIN {{ INPUTS {{ i; }} OUTPUTS {{ o; }} '
        'ASSUMPTIONS { G F i; } GUARANTEES { G(o <-> i); } }\n')
    from tlsf_pairs import convert_pair
    formula = convert_pair("syfco", source / "timing.tlsf", pairs / "timing.part",
                           10, "Mealy", canonicalizer)
    (pairs / "timing.ltl").write_text(formula)
    assert conversion.main([str(source), str(pairs), "--native-inspect", str(inspector),
                            "--canonicalizer", str(canonicalizer)]) == 0
