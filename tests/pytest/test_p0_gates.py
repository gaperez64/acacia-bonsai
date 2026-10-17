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
converter = load("convert-tlsf-corpus")
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
    def syfco(syfco, source, part, timeout, target, canonicalizer, fallback_inspect):
        commands.append(target)
        assert target == "Mealy"
        part.write_text(".inputs i\n.outputs o\n")
        return formula + "\n", "syfco", ""
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
    formula, _, _ = convert_pair("syfco", source / "timing.tlsf", pairs / "timing.part",
                                 10, "Mealy", canonicalizer)
    (pairs / "timing.ltl").write_text(formula)
    assert conversion.main([str(source), str(pairs), "--native-inspect", str(inspector),
                            "--canonicalizer", str(canonicalizer)]) == 0


@pytest.fixture
def fallback_tools(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    for name in ("good", "fallback"):
        (source / f"{name}.tlsf").write_text("synthetic TLSF\n")
    syfco_log, inspector_log = tmp_path / "syfco.log", tmp_path / "inspector.log"
    reason = "SyFCo: strong next X[!] is unsupported"
    syfco = tmp_path / "syfco"
    syfco.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        "args = sys.argv[1:]\n"
        "name = pathlib.Path(args[-1]).stem\n"
        f"with open({str(syfco_log)!r}, 'a') as log: log.write(args[0] + ' ' + name + '\\n')\n"
        "if name == 'fallback':\n"
        "    if '--part-file' in args:\n"
        "        pathlib.Path(args[args.index('--part-file') + 1]).write_text('stale partition')\n"
        f"    print({reason!r}, file=sys.stderr)\n"
        "    raise SystemExit(1)\n"
        "if args[0] == '--print-semantics':\n"
        "    print('Moore')\n"
        "else:\n"
        "    assert args[:4] == ['--format', 'ltlxba', '--mode', 'fully']\n"
        "    pathlib.Path(args[args.index('--part-file') + 1]).write_text('.inputs i\\n.outputs o\\n')\n"
        "    print('G(o <-> i)')\n"
    )
    inspector = tmp_path / "inspect"
    inspector.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        "name = pathlib.Path(sys.argv[1]).stem\n"
        f"with open({str(inspector_log)!r}, 'a') as log: log.write(name + '\\n')\n"
        "formula, inputs, outputs = ('G X i', 'i,aux', '') if name == 'fallback' "
        "else ('G(o <-> X i)', 'i', 'o')\n"
        "sys.stdout.write('\\0'.join((formula, inputs, outputs, 'Moore', 'Moore', 'Mealy', '')))\n"
    )
    canonicalizer = tmp_path / "canonicalize"
    canonicalizer.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "formula = sys.stdin.read().strip()\n"
        "if sys.argv[1:]:\n"
        "    assert sys.argv[1:] == ['--delay-aps', 'i']\n"
        "    assert formula == 'G(o <-> i)', 'formula adapted twice'\n"
        "    formula = 'G(o <-> X i)'\n"
        "print(formula)\n"
    )
    for executable in (syfco, inspector, canonicalizer):
        executable.chmod(0o755)
    return SimpleNamespace(source=source, pairs=tmp_path / "pairs", syfco=syfco,
                           inspector=inspector, canonicalizer=canonicalizer, reason=reason,
                           syfco_log=syfco_log, inspector_log=inspector_log)


def prepare_with_fallback(tools):
    return converter.main([str(tools.source), str(tools.pairs), "--syfco", str(tools.syfco),
                           "--target", "Mealy", "--canonicalizer", str(tools.canonicalizer),
                           "--fallback-inspect", str(tools.inspector)])


def test_g5_prepares_fallback_and_reports_it_without_a_waiver(monkeypatch, tmp_path, fallback_tools):
    tools = fallback_tools
    selection = tmp_path / "all.list"
    selection.write_text("good.ltl\nfallback.ltl\n")
    binary = tmp_path / "solver"
    binary.touch()
    solver_calls = []
    def solver(cmd, *args, **kwargs):
        solver_calls.append(cmd)
        return SimpleNamespace(stdout="REALIZABLE\n", stderr="", returncode=0,
                               seconds=0.1, timed_out=False, resource_limited=False)
    monkeypatch.setattr(parity, "run_systemd_scope", solver)
    summary, rows_csv, status = tmp_path / "summary", tmp_path / "rows.csv", tmp_path / "status"
    argv = ["parity", "--prepare", "--bin", str(binary), "--source", str(tools.source),
            "--converted", str(tools.pairs), "--list", str(selection), "--expect-count", "2",
            "--syfco", str(tools.syfco), "--native-inspect", str(tools.inspector),
            "--canonicalizer", str(tools.canonicalizer), "--csv", str(rows_csv),
            "--summary", str(summary), "--status", str(status)]
    monkeypatch.setattr(sys, "argv", argv)
    assert parity.main() == 0
    assert len(solver_calls) == 4
    assert (tools.pairs / "fallback.ltl").read_text() == "G X i\n"
    assert (tools.pairs / "fallback.part").read_text() == ".inputs i aux\n.outputs\n"
    with (tools.pairs / "conversion.tsv").open() as stream:
        converted = {row["instance"]: row for row in csv.DictReader(stream, delimiter="\t")}
    assert converted["good.tlsf"]["converter"] == "syfco"
    assert converted["good.tlsf"]["syfco_error"] == ""
    assert converted["fallback.tlsf"]["converter"] == "tlsf-tools"
    assert converted["fallback.tlsf"]["syfco_error"] == tools.reason
    with (tools.pairs / "spot-check.tsv").open() as stream:
        checked = {row["instance"]: row for row in csv.DictReader(stream, delimiter="\t")}
    assert checked["good.tlsf"]["outcome"] == "PASS"
    assert checked["good.tlsf"]["independently_cross_checked"] == "1"
    assert checked["fallback.tlsf"]["outcome"] == "NOT_INDEPENDENTLY_CHECKED"
    assert checked["fallback.tlsf"]["independently_cross_checked"] == "0"
    assert checked["fallback.tlsf"]["formula_ast_match"] == ""
    assert checked["fallback.tlsf"]["syfco_error"] == tools.reason
    with rows_csv.open() as stream:
        rows = list(csv.DictReader(stream))
    assert [row["converter"] for row in rows] == ["syfco", "tlsf-tools"]
    assert rows[1]["syfco_error"] == tools.reason
    assert tools.syfco_log.read_text().splitlines().count("--format good") == 2
    assert tools.syfco_log.read_text().splitlines().count("--format fallback") == 2
    assert tools.inspector_log.read_text().splitlines() == ["fallback", "fallback", "good"]
    assert f"fallback.tlsf: {tools.reason}" in summary.read_text()
    assert "tlsf-tools pairs (not independently cross-checked): 1" in summary.read_text()
    assert "GATE PASS" in summary.read_text()
    assert status.read_text() == "COMPLETE PASS\n"
    # Resuming keeps converter provenance without running the solvers again.
    monkeypatch.setattr(sys, "argv", [arg for arg in argv if arg != "--prepare"] + ["--resume"])
    assert parity.main() == 0
    assert len(solver_calls) == 4
    assert f"fallback.tlsf: {tools.reason}" in summary.read_text()


@pytest.mark.parametrize("suffix", ["ltl", "part"])
def test_fallback_spot_check_still_rejects_a_wrong_pair(fallback_tools, tmp_path, suffix):
    tools = fallback_tools
    assert prepare_with_fallback(tools) == 0
    (tools.pairs / f"fallback.{suffix}").write_text("wrong\n")
    report = tmp_path / "report.tsv"
    assert conversion.main([str(tools.source), str(tools.pairs), "--syfco", str(tools.syfco),
                            "--native-inspect", str(tools.inspector), "--only", "fallback",
                            "--canonicalizer", str(tools.canonicalizer),
                            "--report", str(report)]) == 1
    with report.open() as stream:
        row, = csv.DictReader(stream, delimiter="\t")
    assert row["outcome"] == "FAIL"
    assert row["fallback_pair_match"] == "0"
    assert row["syfco_pair_match"] == ""
    assert row["independently_cross_checked"] == "0"


@pytest.mark.parametrize("failure", ["raise SystemExit(1)", "print('malformed')"])
def test_fallback_failure_removes_stale_pairs(fallback_tools, failure):
    tools = fallback_tools
    tools.pairs.mkdir()
    for suffix in ("ltl", "part"):
        (tools.pairs / f"fallback.{suffix}").write_text("stale\n")
    tools.inspector.write_text("#!/usr/bin/env python3\n" + failure + "\n")
    assert prepare_with_fallback(tools) == 1
    assert not (tools.pairs / "fallback.ltl").exists()
    assert not (tools.pairs / "fallback.part").exists()
    assert (tools.pairs / "good.ltl").is_file()


@pytest.mark.parametrize("stage", ["semantics", "adaptation"])
def test_post_conversion_failure_does_not_trigger_fallback(fallback_tools, stage):
    tools = fallback_tools
    if stage == "semantics":
        tools.syfco.write_text(tools.syfco.read_text().replace("print('Moore')", "raise SystemExit(1)"))
    else:
        tools.canonicalizer.write_text("#!/usr/bin/env python3\nraise SystemExit(1)\n")
    assert prepare_with_fallback(tools) == 1
    assert not (tools.pairs / "good.ltl").exists()
    assert not (tools.pairs / "good.part").exists()
    assert tools.inspector_log.read_text().splitlines() == ["fallback"]
