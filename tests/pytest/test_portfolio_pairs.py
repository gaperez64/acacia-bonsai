"""Error-policy regressions with mocked scope invocations; no solver or build needed."""

import csv
import importlib.util
import json
import pathlib
import sys

import pytest


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "run-portfolio-pairs.py"
PAIRS = {
    "alpha": "real:small:backward,unreal:formula:forward",
    "beta": "real:small:forward,unreal:automaton:forward",
}
INSTANCES = ["error", "crash", "later"]


def load():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("run_portfolio_pairs", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


pairs = load()


@pytest.fixture
def campaign(tmp_path):
    binary = tmp_path / "solver"
    binary.write_text("#!/usr/bin/env python3\nraise AssertionError('must mock invocation')\n")
    binary.chmod(0o755)
    for instance in INSTANCES:
        (tmp_path / f"{instance}.tlsf").write_text("//STATUS: realizable\n")
    (tmp_path / "list").write_text("".join(f"{instance}.ltl\n" for instance in INSTANCES))
    (tmp_path / "map").write_text(
        "instance\ttlsf\n"
        + "".join(f"{instance}.ltl\t{instance}.tlsf\n" for instance in INSTANCES)
    )
    (tmp_path / "pairs").write_text(
        "".join(f"{label}|{arms}\n" for label, arms in PAIRS.items())
    )
    return [
        "--bin", str(binary), "--pairs", str(tmp_path / "pairs"),
        "--list", str(tmp_path / "list"), "--tlsf-map", str(tmp_path / "map"),
        "--tlsf-corpus", str(tmp_path), "--output", str(tmp_path / "out.tsv"),
        "--acacia-sha", "frozen-sha", "--preset", "test-preset",
    ]


def read_tsv(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def solver_result(result):
    from benchlib import RunResult

    stdout, stderr, exit_code = {
        "ERROR": ("starting solver\n", "Exception caught: std::bad_alloc\n", 3),
        "CRASH": ("starting solver\n", "segmentation fault\n", -11),
        "REALIZABLE": ("REALIZABLE\n", "", 0),
        "UNREALIZABLE": ("UNREALIZABLE\n", "", 1),
    }[result]
    return RunResult(
        stdout, stderr, exit_code, 0.04, False,
        stdout_bytes=len(stdout), stderr_bytes=len(stderr), scope_unit="acacia-test.scope",
    )


def mock_invocations(monkeypatch, results):
    calls = []
    outcomes = iter(results)

    def scoped(cmd, **kwargs):
        calls.append((cmd[2], pathlib.Path(cmd[-1]).name))
        outcome = next(outcomes)
        if isinstance(outcome, BaseException):
            raise outcome
        return solver_result(outcome)

    monkeypatch.setattr(pairs, "run_systemd_scope", scoped)
    return calls


def run_main(monkeypatch, campaign, *options):
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), *campaign, *options])
    return pairs.main()


def failure_message(result):
    run = solver_result(result)
    diagnostic = "\n".join((run.stdout, run.stderr)).strip()
    return (f"binary failed for pair alpha; --arms {PAIRS['alpha']!r}; "
            f"exit={run.returncode}\n{diagnostic}")


@pytest.mark.parametrize("result", ["ERROR", "CRASH"])
@pytest.mark.parametrize("options", [[], ["--error-policy", "stop"]], ids=["default", "stop"])
@pytest.mark.parametrize("entrypoint", ["run", "main"])
def test_stop_preserves_exception_diagnostic_and_exit(
    monkeypatch, campaign, tmp_path, capsys, result, options, entrypoint,
):
    calls = mock_invocations(monkeypatch, [result, "REALIZABLE"])
    if entrypoint == "run":
        args = pairs.build_parser().parse_args([*campaign, *options])
        with pytest.raises(pairs.PairError) as caught:
            pairs.run(args)
        assert str(caught.value) == failure_message(result)
    else:
        assert run_main(monkeypatch, campaign, *options) == 2
    captured = capsys.readouterr()
    assert captured.err == (f"error: {failure_message(result)}\n" if entrypoint == "main" else "")
    assert f"instance=error.ltl cap=17s result={result} seconds=0.040" in captured.out
    assert calls == [(PAIRS["alpha"], "error.tlsf")]
    row, = read_tsv(tmp_path / "out.tsv")
    assert row["result"] == result
    assert json.loads((tmp_path / "out-metadata.json").read_text())["error_policy"] == "stop"


def test_collect_records_failures_continues_and_summarizes(
    monkeypatch, campaign, tmp_path, capsys,
):
    calls = mock_invocations(monkeypatch, ["ERROR"] * 4 + ["CRASH"] * 4 + ["REALIZABLE"] * 4)
    assert run_main(monkeypatch, campaign, "--error-policy", "collect", "--repetitions", "2") == 2
    assert len(calls) == 12
    assert calls[-1][1] == "later.tlsf"
    rows = read_tsv(tmp_path / "out.tsv")
    assert [row["result"] for row in rows] == ["ERROR"] * 4 + ["CRASH"] * 4 + ["REALIZABLE"] * 4
    assert [row["run_index"] for row in rows] == [str(index) for index in range(12)]
    assert all(row["exit_code"] == "3" and row["resource_reason"] == "" for row in rows[:4])
    assert all(row["exit_code"] == "-11" and row["resource_reason"] == "signal:11"
               for row in rows[4:8])
    assert all(row["timed_out"] == "false" for row in rows)
    captured = capsys.readouterr()
    assert captured.err.count("error: binary failed for pair") == 8
    assert f"error: {failure_message('ERROR')}\n" in captured.err
    assert f"error: {failure_message('CRASH')}\n" in captured.err
    summary_line = captured.err.splitlines()[-1]
    assert summary_line.startswith("ERRORS COLLECTED: 8 ERROR/CRASH rows")
    for label in PAIRS:
        for instance in ["error", "crash"]:
            assert f"pair={label} instance={instance}.ltl" in summary_line
    assert "later.ltl" not in summary_line

    summary = read_tsv(tmp_path / "out-summary.tsv")
    assert len(summary) == 6
    for row in summary:
        if row["instance"] == "later.ltl":
            assert row["smallest_cap_solved"] == "17"
            assert row["decisive_result"] == "REALIZABLE"
            assert row["still_unsolved_at_max_cap"] == "false"
            assert row["repetition_id"] == "1"
        else:
            assert row["smallest_cap_solved"] == row["decisive_result"] == ""
            assert row["decisive_seconds"] == row["repetition_id"] == row["order_index"] == ""
            assert row["still_unsolved_at_max_cap"] == "true"
            assert row["failure_kind_at_max_cap"] == row["instance"].removesuffix(".ltl").upper()
    assert read_tsv(tmp_path / "out-conflicts.tsv") == []
    assert json.loads((tmp_path / "out-metadata.json").read_text())["error_policy"] == "collect"


def test_collect_without_errors_succeeds(monkeypatch, campaign, capsys):
    calls = mock_invocations(monkeypatch, ["REALIZABLE"] * 6)
    assert run_main(monkeypatch, campaign, "--error-policy", "collect") == 0
    assert len(calls) == 6
    assert capsys.readouterr().err == ""


@pytest.mark.parametrize("result", ["ERROR", "CRASH"])
@pytest.mark.parametrize("recorded_policy", ["collect", "stop", "legacy"])
def test_resume_collect_skips_failures_and_keeps_them_in_final_count(
    monkeypatch, campaign, tmp_path, capsys, result, recorded_policy,
):
    mock_invocations(monkeypatch, [result, KeyboardInterrupt()])
    policy = "stop" if recorded_policy == "legacy" else recorded_policy
    assert run_main(monkeypatch, campaign, "--error-policy", policy) == (
        130 if policy == "collect" else 2
    )
    output = tmp_path / "out.tsv"
    original = output.read_bytes()
    recorded_row, = read_tsv(output)
    metadata_path = tmp_path / "out-metadata.json"
    if recorded_policy == "legacy":
        metadata = json.loads(metadata_path.read_text())
        del metadata["error_policy"]
        metadata_path.write_text(json.dumps(metadata))
    capsys.readouterr()

    calls = mock_invocations(monkeypatch, ["REALIZABLE"] * 5)
    assert run_main(monkeypatch, campaign, "--resume", "--error-policy", "collect") == 2
    assert len(calls) == 5
    assert (PAIRS["alpha"], "error.tlsf") not in calls
    assert (PAIRS["beta"], "error.tlsf") in calls
    assert output.read_bytes().startswith(original)
    rows = read_tsv(output)
    assert len(rows) == 6 and rows[0] == recorded_row
    assert [row["run_index"] for row in rows] == [str(index) for index in range(6)]
    assert json.loads(metadata_path.read_text())["error_policy"] == "collect"
    captured = capsys.readouterr()
    assert "ERRORS COLLECTED: 1 ERROR/CRASH rows" in captured.err
    assert "pair=alpha instance=error.ltl" in captured.err

    completed = output.read_bytes()
    assert run_main(monkeypatch, campaign, "--resume", "--error-policy", "collect") == 2
    assert len(calls) == 5
    assert output.read_bytes() == completed
    assert capsys.readouterr().err == captured.err


@pytest.mark.parametrize("result", ["ERROR", "CRASH"])
def test_resume_stop_refuses_collected_failures(monkeypatch, campaign, tmp_path, capsys, result):
    mock_invocations(monkeypatch, [result, KeyboardInterrupt()])
    assert run_main(monkeypatch, campaign, "--error-policy", "collect") == 130
    capsys.readouterr()
    output = tmp_path / "out.tsv"
    original = output.read_bytes()
    calls = mock_invocations(monkeypatch, [])
    assert run_main(monkeypatch, campaign, "--resume") == 2
    assert capsys.readouterr().err == (
        "error: recorded binary error/crash; inspect the campaign before starting a new output\n"
    )
    assert calls == []
    assert output.read_bytes() == original


def test_changing_policy_does_not_bypass_resume_treatment_guard(
    monkeypatch, campaign, tmp_path, capsys,
):
    mock_invocations(monkeypatch, ["ERROR"])
    assert run_main(monkeypatch, campaign) == 2
    capsys.readouterr()
    original = (tmp_path / "out-metadata.json").read_bytes()
    calls = mock_invocations(monkeypatch, [])
    assert run_main(monkeypatch, campaign, "--resume", "--error-policy", "collect", "--cap", "30") == 2
    assert capsys.readouterr().err == (
        "error: resume configuration, schedule or binary differs from recorded campaign\n"
    )
    assert calls == []
    assert (tmp_path / "out-metadata.json").read_bytes() == original


def test_error_policy_collect_still_stops_on_verdict_conflicts(
    monkeypatch, campaign, tmp_path, capsys,
):
    calls = mock_invocations(monkeypatch, ["REALIZABLE", "UNREALIZABLE", "ERROR"])
    assert run_main(monkeypatch, campaign, "--error-policy", "collect") == 1
    assert len(calls) == 2
    assert "verdict conflict: error.ltl" in capsys.readouterr().err
    assert len(read_tsv(tmp_path / "out-conflicts.tsv")) == 2
