from __future__ import annotations

import csv
import importlib.util
import pathlib
import subprocess
import sys


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


def test_par2_arithmetic_and_output_files(tmp_path):
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

    prefix = tmp_path / "plots" / "tiny"
    markdown = tmp_path / "plots" / "tiny.md"
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
            "--markdown",
            str(markdown),
        ]
    ) == 0
    outputs = (pathlib.Path(f"{prefix}.png"), pathlib.Path(f"{prefix}.pdf"), markdown)
    for output in outputs:
        assert output.is_file()
        assert output.stat().st_size > 0

    table = markdown.read_text()
    assert "| series | solved | of | PAR-2 (s) | total time on solved (s) |" in table
    assert "| solver | **2** | 3 | **24.000** | 4.000 |" in table


def test_virtual_best_uses_per_instance_minimum_and_keeps_failure(tmp_path):
    module = load_cactus_report()
    first = {
        "a.ltl": module.RunResult("REALIZABLE", 5.0),
        "b.ltl": module.RunResult("TIMEOUT", 17.0),
        "c.ltl": module.RunResult("UNKNOWN", 0.2),
    }
    second = {
        "a.ltl": module.RunResult("UNREALIZABLE", 2.0),
        "b.ltl": module.RunResult("REALIZABLE", 3.0),
        "c.ltl": module.RunResult("ERROR", 0.1),
    }

    portfolio = module.make_virtual_best([first, second])

    assert portfolio["a.ltl"] == second["a.ltl"]
    assert portfolio["b.ltl"] == second["b.ltl"]
    assert portfolio["c.ltl"].result == "UNSOLVED"
    assert not portfolio["c.ltl"].solved
    summary = module.summarize("portfolio", portfolio, timeout=17.0)
    assert summary.solved == 2
    assert summary.solved_time == 5.0
    assert summary.par2 == 39.0


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
