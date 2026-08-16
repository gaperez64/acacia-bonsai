import csv
import importlib.util
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "landing-bar.py"
SPEC = importlib.util.spec_from_file_location("landing_bar", SCRIPT)
landing_bar = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = landing_bar
SPEC.loader.exec_module(landing_bar)


def write_rows(path, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=["suite", "instance", "result", "seconds", "exit"]
        )
        writer.writeheader()
        writer.writerows(rows)


def row(instance, result, seconds=1.0, suite="panel"):
    return {
        "suite": suite,
        "instance": instance,
        "result": result,
        "seconds": seconds,
        "exit": 0,
    }


def test_gate_passes_without_lost_answers(tmp_path, capsys):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("a.ltl", "REALIZABLE"), row("b.ltl", "TIMEOUT", 17)])
    write_rows(candidate, [row("a.ltl", "REALIZABLE", 0.5), row("b.ltl", "UNREALIZABLE")])

    assert landing_bar.main([str(baseline), str(candidate)]) == 0
    assert capsys.readouterr().out.rstrip().endswith("GATE PASS")


def test_gate_rejects_lost_and_flipped_answers(tmp_path, capsys):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("a.ltl", "REALIZABLE"), row("b.ltl", "UNREALIZABLE")])
    write_rows(candidate, [row("a.ltl", "TIMEOUT", 17), row("b.ltl", "REALIZABLE")])

    assert landing_bar.main([str(baseline), str(candidate)]) == 1
    output = capsys.readouterr().out
    assert "lost panel/a.ltl" in output
    assert "verdict changed panel/b.ltl" in output
    assert output.rstrip().endswith("GATE FAIL: 2 instance comparison failure(s)")


def test_gate_rejects_incomplete_campaigns(tmp_path, capsys):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("a.ltl", "TIMEOUT", 17)])
    write_rows(candidate, [row("b.ltl", "UNKNOWN")])

    assert landing_bar.main([str(baseline), str(candidate)]) == 1
    output = capsys.readouterr().out
    assert "missing candidate row: panel/a.ltl" in output
    assert "unexpected candidate row: panel/b.ltl" in output


def test_near_cap_timeout_is_rescued_by_extended_remeasurement(
    tmp_path, capsys, monkeypatch
):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("fragile.ltl", "REALIZABLE", 15.9)])
    write_rows(candidate, [row("fragile.ltl", "TIMEOUT", 17.0)])

    calls = []

    def fake_run_solver(binary, instances_dir, key, timeout, memory_max, memory_swap_max):
        calls.append((binary.name, key, timeout, memory_max, memory_swap_max))
        return (
            landing_bar.Result("REALIZABLE", "solved", 16.2),
            "REALIZABLE\n",
            "",
            [str(binary), "-F", key],
        )

    monkeypatch.setattr(landing_bar, "run_solver", fake_run_solver)
    baseline_bin = tmp_path / "baseline-bin"
    candidate_bin = tmp_path / "candidate-bin"
    assert landing_bar.main(
        [
            str(baseline),
            str(candidate),
            "--baseline-bin",
            str(baseline_bin),
            "--candidate-bin",
            str(candidate_bin),
        ]
    ) == 0
    assert calls == [
        ("baseline-bin", "panel/fragile.ltl", 51.0, "8G", "0"),
        ("candidate-bin", "panel/fragile.ltl", 51.0, "8G", "0"),
    ]
    output = capsys.readouterr().out
    assert "REMEASURE panel/fragile.ltl" in output
    assert output.rstrip().endswith("GATE PASS")


def test_near_cap_timeout_still_fails_when_extended_run_does_not_answer(
    tmp_path, capsys, monkeypatch
):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("fragile.ltl", "REALIZABLE", 15.9)])
    write_rows(candidate, [row("fragile.ltl", "TIMEOUT", 17.0)])

    answers = iter(
        [
            landing_bar.Result("REALIZABLE", "solved", 16.1),
            landing_bar.Result(None, "timeout", 51.0),
        ]
    )

    def fake_run_solver(binary, instances_dir, key, timeout, memory_max, memory_swap_max):
        return next(answers), "", "", [str(binary), "-F", key]

    monkeypatch.setattr(landing_bar, "run_solver", fake_run_solver)
    assert landing_bar.main(
        [
            str(baseline),
            str(candidate),
            "--baseline-bin",
            str(tmp_path / "baseline-bin"),
            "--candidate-bin",
            str(tmp_path / "candidate-bin"),
        ]
    ) == 1
    output = capsys.readouterr().out
    assert "lost panel/fragile.ltl after 51s remeasurement" in output
    assert output.rstrip().endswith("GATE FAIL: 1 instance comparison failure(s)")
