from __future__ import annotations

import csv
import hashlib
import importlib.util
import os
import pathlib
import subprocess
import sys
from dataclasses import replace

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "cactus-report.py"


def load_cactus_report():
    spec = importlib.util.spec_from_file_location("cactus_report", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_rows(path, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=["instance", "result", "seconds", "exit"]
        )
        writer.writeheader()
        writer.writerows(rows)


def row(instance, result, seconds):
    return {
        "instance": instance,
        "result": result,
        "seconds": seconds,
        "exit": 0,
    }


def test_par2_arithmetic_and_markdown(tmp_path):
    module = load_cactus_report()
    source = tmp_path / "solver.csv"
    write_rows(
        source,
        [
            row("a.ltl", "REALIZABLE", 1.25),
            row("b.ltl", "UNREALIZABLE", 2.75),
            row("c.ltl", "TIMEOUT", 10.0),
        ],
    )

    rows = module.load_csv(source)
    summary = module.summarize("solver", rows, timeout=10.0)
    assert summary.solved == 2
    assert summary.total == 3
    assert summary.solved_time == 4.0
    assert summary.par2 == 24.0
    assert summary.par2_mean == 8.0
    assert summary.real == summary.unreal == 1
    assert summary.cactus_endpoint == summary.solved
    assert summary.non_solved == {"TIMEOUT": 1}

    markdown = tmp_path / "plots" / "tiny.md"
    assert module.main(
        [
            "--csv",
            f"solver={source}",
            "--title",
            "Tiny example",
            "--timeout",
            "10",
            "--markdown",
            str(markdown),
        ]
    ) == 0
    assert markdown.is_file()
    assert markdown.stat().st_size > 0

    table = markdown.read_text()
    assert "| series | solved | of | PAR-2 (s) | total time on solved (s) |" in table
    assert "| solver | **2** | 3 | **24.000** | 4.000 |" in table
    assert "| REAL | UNREAL | PAR-2 mean (s) | Raw dataset hash |" in table
    assert f"| 1 | 1 | 8.000 | {hashlib.sha256(source.read_bytes()).hexdigest()} |" in table
    assert len({len(line.split("|")) for line in table.splitlines()}) == 1


def test_virtual_best_uses_per_instance_minimum_and_keeps_failure(tmp_path):
    module = load_cactus_report()
    first_path = tmp_path / "first.csv"
    second_path = tmp_path / "second.csv"
    write_rows(
        first_path,
        [
            row("a.ltl", "REALIZABLE", 5.0),
            row("b.ltl", "TIMEOUT", 17.0),
            row("c.ltl", "UNKNOWN", 0.2),
        ],
    )
    write_rows(
        second_path,
        [
            row("a.ltl", "UNREALIZABLE", 2.0),
            row("b.ltl", "REALIZABLE", 3.0),
            row("c.ltl", "ERROR", 0.1),
        ],
    )
    first = module.load_csv(first_path)
    second = module.load_csv(second_path)

    portfolio = module.make_virtual_best([first, second])

    assert portfolio["a.ltl"] == second["a.ltl"]
    assert portfolio["b.ltl"] == second["b.ltl"]
    assert portfolio["c.ltl"].result == "UNSOLVED"
    assert not portfolio["c.ltl"].solved
    summary = module.summarize("portfolio", portfolio, timeout=17.0)
    assert summary.solved == 2
    assert summary.solved_time == 5.0
    assert summary.par2 == 39.0

    markdown = tmp_path / "portfolio.md"
    assert module.main(
        [
            "--csv",
            f"first={first_path}",
            "--csv",
            f"second={second_path}",
            "--virtual-best",
            "portfolio=first,second",
            "--title",
            "Portfolio",
            "--timeout",
            "17",
            "--markdown",
            str(markdown),
        ]
    ) == 0

    table = markdown.read_text()
    assert "| portfolio | **2** | 3 | **39.000** | 5.000 |" in table


def test_mismatched_instance_sets_exit_nonzero_and_name_differences(tmp_path):
    first = tmp_path / "first.csv"
    second = tmp_path / "second.csv"
    write_rows(
        first,
        [
            row("common.ltl", "REALIZABLE", 1.0),
            row("only-first.ltl", "TIMEOUT", 17.0),
        ],
    )
    write_rows(
        second,
        [
            row("common.ltl", "REALIZABLE", 2.0),
            row("only-second.ltl", "UNKNOWN", 0.1),
        ],
    )

    completed = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--csv",
            f"first={first}",
            "--csv",
            f"second={second}",
            "--title",
            "Mismatch",
            "--out-prefix",
            str(tmp_path / "unused"),
            "--markdown",
            str(tmp_path / "unused.md"),
        ],
        capture_output=True,
        text=True,
    )

    assert completed.returncode != 0
    assert "instance sets differ" in completed.stderr
    assert "only-first.ltl" in completed.stderr
    assert "only-second.ltl" in completed.stderr
    assert not (tmp_path / "unused.png").exists()


def test_figure_files(tmp_path):
    pytest.importorskip("matplotlib", reason="matplotlib is optional; table tests run without it")
    module = load_cactus_report()
    source = tmp_path / "solver.csv"
    write_rows(
        source,
        [
            row("a.ltl", "REALIZABLE", 1.25),
            row("b.ltl", "UNREALIZABLE", 2.75),
            row("c.ltl", "TIMEOUT", 10.0),
        ],
    )

    prefix = tmp_path / "plots" / "tiny"
    assert module.main(
        [
            "--csv",
            f"solver={source}",
            "--title",
            "Tiny example",
            "--timeout",
            "10",
            "--out-prefix",
            str(prefix),
        ]
    ) == 0

    for output in (pathlib.Path(f"{prefix}.png"), pathlib.Path(f"{prefix}.pdf")):
        assert output.is_file()
        assert output.stat().st_size > 0


def test_out_prefix_without_matplotlib_exits_with_actionable_message(tmp_path):
    source = tmp_path / "solver.csv"
    write_rows(source, [row("a.ltl", "REALIZABLE", 1.0)])
    shim = tmp_path / "no-matplotlib"
    shim.mkdir()
    (shim / "matplotlib.py").write_text(
        'raise ImportError("simulated missing matplotlib")\n'
    )
    env = os.environ.copy()
    pythonpath = env.get("PYTHONPATH")
    env["PYTHONPATH"] = os.pathsep.join(
        [str(shim), *([pythonpath] if pythonpath else [])]
    )

    prefix = tmp_path / "missing" / "plot"
    completed = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--csv",
            f"solver={source}",
            "--title",
            "Missing dependency",
            "--out-prefix",
            str(prefix),
        ],
        capture_output=True,
        text=True,
        env=env,
    )

    assert completed.returncode != 0
    assert "matplotlib" in completed.stderr
    assert "omitting --out-prefix" in completed.stderr
    assert not pathlib.Path(f"{prefix}.png").exists()
    assert not pathlib.Path(f"{prefix}.pdf").exists()


def test_requires_at_least_one_output(tmp_path):
    source = tmp_path / "solver.csv"
    write_rows(source, [row("a.ltl", "REALIZABLE", 1.0)])

    completed = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--csv",
            f"solver={source}",
            "--title",
            "No output",
        ],
        capture_output=True,
        text=True,
    )

    assert completed.returncode != 0
    assert "at least one of --out-prefix or --markdown is required" in completed.stderr


@pytest.mark.parametrize("result,seconds,message", [
    ("MEMOUT", "1", "unknown result"),
    ("CRASH", "1", "unknown result"),
    ("OTHER", "1", "unknown result"),
    ("TIMEOUT", "nan", "finite"),
    ("TIMEOUT", "inf", "finite"),
    ("TIMEOUT", "-1", "non-negative"),
])
def test_load_csv_rejects_invalid_result_or_time(tmp_path, result, seconds, message):
    module = load_cactus_report()
    source = tmp_path / "invalid.csv"
    write_rows(source, [row("a", result, seconds)])
    with pytest.raises(ValueError, match=message):
        module.load_csv(source)


def test_load_csv_rejects_duplicate_instances(tmp_path):
    module = load_cactus_report()
    source = tmp_path / "duplicate.csv"
    write_rows(source, [row("a", "REALIZABLE", 1), row("a", "TIMEOUT", 17)])
    with pytest.raises(ValueError, match="duplicate instance"):
        module.load_csv(source)


def test_failure_categories_all_pay_par2_even_for_immediate_returns(tmp_path):
    module = load_cactus_report()
    source = tmp_path / "failures.csv"
    write_rows(source, [row(str(i), result, 0) for i, result in enumerate(module.NON_SOLVED_RESULTS)])
    summary = module.summarize("failures", module.load_csv(source), 17)
    assert summary.solved == summary.real == summary.unreal == summary.cactus_endpoint == 0
    assert summary.par2 == 170
    assert summary.par2_mean == 34
    assert summary.non_solved == dict.fromkeys(module.NON_SOLVED_RESULTS, 1)
    table = module.render_markdown([summary])
    assert "| TIMEOUT | RESOURCE_LIMIT | UNKNOWN | ERROR | SYFCO-FAIL |" in table
    assert "| 1 | 1 | 1 | 1 | 1 | 0 | 0 | 34.000 |" in table


@pytest.mark.parametrize("change,message", [
    ({"cactus_endpoint": 2}, "cactus curve endpoint"),
    ({"real": 2}, "REAL \\+ UNREAL"),
    ({"par2_mean": 99}, "PAR-2 mean"),
    ({"total": 2}, "dataset size"),
])
def test_inconsistent_summary_cannot_be_rendered(change, message):
    module = load_cactus_report()
    summary = module.summarize("solver", {"a": module.RunResult("REALIZABLE", 1)}, 17)
    with pytest.raises(ValueError, match=message):
        module.render_markdown([replace(summary, **change)])


def test_inconsistent_curve_endpoint_fails_report_before_writing(tmp_path, monkeypatch):
    module = load_cactus_report()
    source = tmp_path / "solver.csv"
    write_rows(source, [row("a", "REALIZABLE", 1)])
    monkeypatch.setattr(module, "cactus_seconds", lambda rows: [])
    output = tmp_path / "unused.md"
    with pytest.raises(SystemExit) as error:
        module.main(["--csv", f"solver={source}", "--title", "bad", "--markdown", str(output)])
    assert error.value.code != 0
    assert not output.exists()


def test_par2_mean_rounding_and_shared_scoring(monkeypatch):
    module = load_cactus_report()
    calls = []
    original = module.par2_score
    def score(solved_seconds, unsolved, timeout):
        calls.append((solved_seconds, unsolved, timeout))
        return original(solved_seconds, unsolved, timeout)
    monkeypatch.setattr(module, "par2_score", score)
    rows = {str(i): module.RunResult("REALIZABLE", time) for i, time in enumerate([1, 1, 2])}
    summary = module.summarize("solver", rows, 17)
    assert calls == [(4, 0, 17)]
    assert summary.par2_mean == pytest.approx(4 / 3)
    assert summary.par2_mean * summary.total == pytest.approx(summary.par2)
    assert "| 3 | 0 | 1.333 |" in module.render_markdown([summary])


def test_plot_keeps_sorted_wall_times_log_axis_and_table_endpoint(tmp_path, monkeypatch):
    pytest.importorskip("matplotlib", reason="matplotlib is optional; table tests run without it")
    module = load_cactus_report()
    import matplotlib.axes

    rows = {
        "slow": module.RunResult("UNREALIZABLE", 3),
        "timeout": module.RunResult("TIMEOUT", 17),
        "fast": module.RunResult("REALIZABLE", 1),
    }
    summary = module.summarize("solver", rows, 17)
    plotted = []
    scales = []
    original_plot, original_scale = matplotlib.axes.Axes.plot, matplotlib.axes.Axes.set_yscale
    def plot(axis, x, y, **kwargs):
        plotted.append((list(x), list(y)))
        return original_plot(axis, x, y, **kwargs)
    def set_yscale(axis, value, **kwargs):
        scales.append(value)
        return original_scale(axis, value, **kwargs)
    monkeypatch.setattr(matplotlib.axes.Axes, "plot", plot)
    monkeypatch.setattr(matplotlib.axes.Axes, "set_yscale", set_yscale)
    module.write_cactus_plot({"solver": rows}, "Curve", tmp_path / "curve", [summary])
    assert plotted == [([1, 2], [1, 3])]
    assert plotted[0][0][-1] == summary.solved
    assert "log" in scales
