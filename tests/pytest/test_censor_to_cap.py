"""Tests for offline censoring of archived uniform-cap observations."""

from __future__ import annotations

import csv
import importlib.util
import json
import pathlib

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking/gr1-par2-20260923/campaign/censor-to-cap.py"


def load_script():
    spec = importlib.util.spec_from_file_location("censor_to_cap", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = load_script()


def write_rows(path, fields, rows, delimiter=","):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter=delimiter)
        writer.writeheader()
        writer.writerows(rows)


def read_rows(path, delimiter=","):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter=delimiter))


def test_cactus_censor_preserves_only_decisive_results_within_cap(tmp_path):
    source = tmp_path / "ltlsynt-cap120.csv"
    output = tmp_path / "ltlsynt-cap60.csv"
    cases = [
        ("under", "REALIZABLE", "59.5", "0"),
        ("boundary", "UNREALIZABLE", "60", "1"),
        ("over", "REALIZABLE", "60.1", "0"),
        ("memout", "RESOURCE_LIMIT", "42", "-15"),
        ("timeout", "TIMEOUT", "120.2", "124"),
        ("error", "ERROR", "4", "2"),
    ]
    write_rows(source, ["instance", "result", "seconds", "exit"],
               [dict(zip(("instance", "result", "seconds", "exit"), case))
                for case in cases])
    tool.derive(source, output, 120, 60)
    rows = read_rows(output)
    assert [(row["result"], row["seconds"]) for row in rows] == [
        ("REALIZABLE", "59.5"), ("UNREALIZABLE", "60"),
        *[("TIMEOUT", "60")] * 4,
    ]
    assert all(float(row["seconds"]) <= 60 for row in rows)
    assert [row["original_result"] for row in read_rows(
        tmp_path / "ltlsynt-cap60-provenance.tsv", "\t"
    )] == [case[1] for case in cases]
    metadata = json.loads((tmp_path / "ltlsynt-cap60-provenance.json").read_text())
    assert metadata["statement"] == tool.PROVENANCE_NOTE
    assert metadata["C_high_s"] == 120 and metadata["C_low_s"] == 60


def test_cactus_refuses_mixed_caps_and_non_decreasing_cap(tmp_path):
    source = tmp_path / "ltlsynt-cap120.csv"
    output = tmp_path / "ltlsynt-cap60.csv"
    write_rows(source, ["instance", "result", "seconds", "exit"], [
        dict(instance="a", result="TIMEOUT", seconds="120.2", exit="124"),
        dict(instance="b", result="TIMEOUT", seconds="17.1", exit="124"),
    ])
    with pytest.raises(ValueError, match="inconsistent"):
        tool.derive(source, output, 120, 60)
    with pytest.raises(ValueError, match="C_low < C_high"):
        tool.derive(source, output, 120, 120)
    assert not output.exists()


def test_coverage_tsv_censor_is_loadable_and_refuses_mixed_caps(tmp_path):
    coverage = tool.coverage
    source = tmp_path / "B-cap120-epoch1.tsv"
    output = tmp_path / "B-cap60-epoch1.tsv"
    instances = tmp_path / "all.list"
    cases = [
        ("under", "REALIZABLE", "59", "0"),
        ("over", "UNREALIZABLE", "61", "1"),
        ("memout", "MEMOUT", "45", "-15"),
        ("timeout", "TIMEOUT", "120.2", "124"),
        ("error", "ERROR", "3", "2"),
    ]
    instances.write_text("".join(f"{case[0]}\n" for case in cases))
    rows = []
    for index, (instance, result, seconds, exit_code) in enumerate(cases):
        row = dict.fromkeys(coverage.OUTPUT_COLUMNS, "")
        row.update(solver_label="B-cap120-epoch1", instance=instance,
                   tlsf_file=f"{instance}.tlsf", cap_s="120", result=result,
                   seconds=seconds, exit_code=exit_code,
                   timed_out=str(result == "TIMEOUT").lower(),
                   expectation_source="status" if result in tool.DECISIVE else "none",
                   run_index=str(index), acacia_sha="source", binary_sha256="binary",
                   preset="otf_sparse_formula", memory_max="8G", memory_swap_max="0",
                   collect_rusage="true")
        rows.append(row)
    coverage.atomic_write_tsv(source, coverage.OUTPUT_COLUMNS, rows)
    coverage.write_summary(source, "B-cap120-epoch1", [case[0] for case in cases], rows, 120)
    tool.derive(source, output, 120, 60, instances)
    derived = coverage.load_uniform_observations(
        tmp_path / "B-cap60-epoch1-summary.tsv", output, instances, 60
    )
    assert [derived[name]["result"] for name, *_ in cases] == [
        "REALIZABLE", "TIMEOUT", "TIMEOUT", "TIMEOUT", "TIMEOUT"
    ]
    assert all(float(row["seconds"]) <= 60 for row in derived.values())
    rows[1]["cap_s"] = "17"
    coverage.atomic_write_tsv(source, coverage.OUTPUT_COLUMNS, rows)
    with pytest.raises(coverage.CoverageError, match="uniform cap"):
        tool.derive(source, output, 120, 60, instances)
