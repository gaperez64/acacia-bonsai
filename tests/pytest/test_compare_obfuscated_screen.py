"""Compare saved screen rows without running a solver."""
from __future__ import annotations

import csv
import pathlib
import sys
from types import SimpleNamespace

import pytest

BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
sys.path.insert(0, str(BENCHMARKING))
try:
    from benchlib import load_tool
    compare = load_tool("benchmarking/compare-obfuscated-screen.py")
finally:
    sys.path.pop(0)
coverage = compare.coverage


@pytest.fixture
def screens(tmp_path):
    plain = []
    obfuscated = []
    manifest = []
    for name, left, right in [
        ("steady", ("REALIZABLE", 2), ("REALIZABLE", 2.5)),
        ("loss", ("UNREALIZABLE", 3), ("TIMEOUT", 17.02)),
        ("gain", ("MEMOUT", 0.1), ("REALIZABLE", 4)),
        ("excluded", ("REALIZABLE", 1), None),
    ]:
        manifest.append({**dict.fromkeys(compare.obfuscate.MANIFEST_COLUMNS, ""),
                         "instance": name, "exit_status": "0" if right else "3",
                         "reason": "" if right else "self-check failure"})
        for rows, item, label in [(plain, left, "plain"), (obfuscated, right, "obfuscated")]:
            if item is None:
                continue
            result, seconds = item
            rows.append({**dict.fromkeys(coverage.OUTPUT_COLUMNS, ""),
                         "solver_label": label, "instance": name, "cap_s": "17",
                         "result": result, "seconds": str(seconds),
                         "exit_code": "1" if result == "UNREALIZABLE" else "0",
                         "expectation_source": "none"})
    return SimpleNamespace(plain=plain, obfuscated=obfuscated, manifest=manifest,
                           root=tmp_path, output=tmp_path / "comparison.tsv")


def run(screens, *extra):
    for name, columns in [("plain", coverage.OUTPUT_COLUMNS),
                          ("obfuscated", coverage.OUTPUT_COLUMNS),
                          ("manifest", compare.obfuscate.MANIFEST_COLUMNS)]:
        coverage.atomic_write_tsv(screens.root / f"{name}.tsv", columns, getattr(screens, name))
    return compare.main([
        "--plain", str(screens.root / "plain.tsv"),
        "--obfuscated", str(screens.root / "obfuscated.tsv"),
        "--manifest", str(screens.root / "manifest.tsv"),
        "--output", str(screens.output), "--noise-s", "0.5", *extra,
    ])


def test_summary_uses_same_successful_panel_and_par2(screens, capsys):
    assert run(screens) == 0
    summary = capsys.readouterr().out
    assert "paired inputs: 3; excluded by obfuscation: 1" in summary
    assert "solved: plain=2 obfuscated=2" in summary
    assert "plain=39.000000 obfuscated=40.500000" in summary
    assert "plain-only=1 obfuscated-only=1" in summary
    assert "verdict conflicts: 0" in summary
    with screens.output.open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert [row["coverage_change"] for row in rows] == ["none", "plain-only", "obfuscated-only", ""]
    assert [row["time_change"] for row in rows] == ["within-noise", "slower", "slower", ""]
    assert rows[0]["delta_s"] == "0.500000"
    assert rows[-1]["exclusion_reason"] == "self-check failure"
    assert rows[-1]["obfuscated_result"] == ""


@pytest.mark.parametrize("plain_result,obfuscated_result", [
    ("REALIZABLE", "UNREALIZABLE"), ("UNREALIZABLE", "REALIZABLE"),
])
def test_verdict_conflict_always_fails_and_writes_output(screens, capsys, plain_result, obfuscated_result):
    for row, result in [(screens.plain[0], plain_result), (screens.obfuscated[0], obfuscated_result)]:
        row.update(result=result, exit_code="0" if result == "REALIZABLE" else "1")
    assert run(screens, "--noise-s", "1000") == 3
    assert "verdict conflicts: 1" in capsys.readouterr().out
    assert "true" in screens.output.read_text()


def test_faster_and_zero_deltas(screens):
    screens.obfuscated[0]["seconds"] = "1.4"
    assert run(screens) == 0
    assert "-0.600000\tfaster" in screens.output.read_text()
    screens.obfuscated[0]["seconds"] = "2"
    assert run(screens, "--noise-s", "0") == 0
    assert "0.000000\twithin-noise" in screens.output.read_text()


@pytest.mark.parametrize("problem", [
    "duplicate", "missing", "extra", "cap", "mixed-caps", "mixed-labels", "negative-seconds",
    "nan-seconds", "infinite-seconds", "bad-exit", "unknown-status", "manifest-duplicate",
    "plain-missing",
])
def test_invalid_screens_do_not_replace_existing_report(screens, problem):
    screens.output.write_text("keep")
    if problem == "duplicate":
        screens.obfuscated.append(screens.obfuscated[0].copy())
    elif problem == "missing":
        screens.obfuscated.pop()
    elif problem == "extra":
        screens.obfuscated.append({**screens.obfuscated[0], "instance": "extra"})
    elif problem == "cap":
        for row in screens.obfuscated:
            row["cap_s"] = "5"
    elif problem == "mixed-caps":
        screens.obfuscated[0]["cap_s"] = "5"
    elif problem == "mixed-labels":
        screens.obfuscated[0]["solver_label"] = "other"
    elif problem.endswith("seconds"):
        screens.obfuscated[0]["seconds"] = {
            "negative-seconds": "-1", "nan-seconds": "nan", "infinite-seconds": "inf",
        }[problem]
    elif problem == "bad-exit":
        screens.obfuscated[0]["exit_code"] = "1"
    elif problem == "unknown-status":
        screens.obfuscated[0]["result"] = "ALIEN"
    elif problem == "manifest-duplicate":
        screens.manifest.append(screens.manifest[0].copy())
    else:
        screens.plain.pop()
    assert run(screens) == 2
    assert screens.output.read_text() == "keep"


def test_all_inputs_excluded_is_reported_without_inventing_observations(screens, capsys):
    screens.obfuscated.clear()
    for row in screens.manifest:
        row.update(exit_status="3", reason="self-check failure")
    assert run(screens) == 0
    summary = capsys.readouterr().out
    assert "paired inputs: 0; excluded by obfuscation: 4" in summary
    assert "plain=0.000000 obfuscated=0.000000" in summary


@pytest.mark.parametrize("value", ["-1", "nan", "inf"])
def test_invalid_noise_rejected(screens, value):
    with pytest.raises(SystemExit) as error:
        run(screens, "--noise-s", value)
    assert error.value.code == 2
