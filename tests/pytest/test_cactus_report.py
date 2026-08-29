from __future__ import annotations

import csv
import importlib.util
import os
import pathlib
import subprocess
import sys

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
    pytest.importorskip("matplotlib")
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
