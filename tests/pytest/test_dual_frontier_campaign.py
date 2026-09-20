"""Tests for the frozen dual-frontier delta campaign helpers."""

import importlib.util
import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_script(name):
    path = ROOT / "benchmarking" / name
    spec = importlib.util.spec_from_file_location(name.removesuffix(".py"), path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_event_inspection_preserves_truncation(tmp_path):
    freezer = load_script("freeze-dual-frontier-manifest.py")
    complete = tmp_path / "cpre-1.tsv"
    complete.write_text(
        "# schema_version=2 loop=1 k=2 actions=1 before=1 input=x\n"
        "[before]\n"
        "-1\n"
        "[actions]\n"
        "action\t0\n"
        "0\t0\t1\n"
        "[after]\t1\n"
        "-1\n"
    )
    header, transitions, valid, reason = freezer.inspect_event(complete)
    assert (header["loop"], transitions, valid, reason) == ("1", 1, True, "")

    truncated = tmp_path / "cpre-2.tsv"
    truncated.write_text(complete.read_text().split("[after]", 1)[0])
    _, _, valid, reason = freezer.inspect_event(truncated)
    assert not valid
    assert "truncated" in reason


def delta_row(family, loop, delta, *, complete=True):
    metrics = {
        "before_maxima": "1024",
        "after_maxima": "1000",
        "before_min_excluded": "16",
        "after_min_excluded": "20",
        "delta_min_excluded": str(delta),
        "positive_generator_survival_fraction": "0.500000",
        "delta_construction_status": "complete",
        "exact": "yes",
    }
    return {
        "family": family,
        "loop": str(loop),
        "before_count": "1024",
        "mode": "-",
        "process_outcome": "ok" if complete else "timeout",
        "replay_status": "complete" if complete else "",
        "replay_metrics_json": json.dumps(metrics) if complete else "{}",
    }


def test_gate_requires_and_accepts_the_predeclared_multi_family_signal(tmp_path):
    study = load_script("dual-frontier-study.py")
    rows = [delta_row(f"family-{index % 3}", index, 128) for index in range(20)]
    rows.append(delta_row("censored-family", 99, 0, complete=False))
    report = tmp_path / "report.md"
    study.write_markdown(report, "delta", rows)
    text = report.read_text()

    assert "**PASS**" in text
    assert "20 complete large events across 3 families" in text
    assert "censored-family" in text
    assert "100.0%" in text
