"""Focused unit tests for run-syntcomp26-coverage.py.

These tests cover input validation and result normalization without invoking a
solver or requiring any build products.
"""
from __future__ import annotations

import argparse
import csv
import importlib.util
import pathlib
import sys

import pytest


BENCHMARKING = pathlib.Path(__file__).resolve().parents[2] / "benchmarking"
SCRIPT = BENCHMARKING / "run-syntcomp26-coverage.py"


def load():
    sys.path.insert(0, str(BENCHMARKING))
    try:
        spec = importlib.util.spec_from_file_location("run_syntcomp26_coverage", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


coverage = load()


def test_parse_caps_orders_caps_ascending():
    assert coverage.parse_caps("60,1,17,5,1") == [1, 5, 17, 60]


@pytest.mark.parametrize("value", ["", "1,,5", "one", "1.5", "0", "-1"])
def test_parse_caps_rejects_malformed_input(value):
    with pytest.raises(argparse.ArgumentTypeError):
        coverage.parse_caps(value)


@pytest.mark.parametrize(
    ("status", "expected"),
    [
        ("realizable", "REALIZABLE"),
        ("unrealizable", "UNREALIZABLE"),
        ("unknown", None),
        # These misspellings are real corpus data, not hypothetical variants.
        ("uknown", None),
        ("unknon", None),
    ],
)
def test_status_annotation_sets_expected_verdict(tmp_path, status, expected):
    tlsf = tmp_path / "case.tlsf"
    tlsf.write_text(f"//STATUS: {status}\n", encoding="utf-8")

    assert coverage.expected_verdict(tlsf) == expected


def write_status_exceptions(path, rows):
    path.write_text(
        "instance\tannotated_status\tcorrected_status\tevidence\n"
        + "".join("\t".join(row) + "\n" for row in rows),
        encoding="utf-8",
    )


def test_status_exception_rejects_annotation_that_disagrees_with_tlsf(tmp_path):
    tlsf = tmp_path / "case.tlsf"
    tlsf.write_text("//STATUS: realizable\n", encoding="utf-8")
    exceptions = tmp_path / "exceptions.tsv"
    write_status_exceptions(
        exceptions,
        [("case.tlsf", "unrealizable", "realizable", "reviewed upstream")],
    )

    with pytest.raises(coverage.CoverageError, match="corpus changed"):
        coverage.read_status_exceptions(exceptions, tmp_path)


def test_status_exception_rejects_empty_evidence(tmp_path):
    tlsf = tmp_path / "case.tlsf"
    tlsf.write_text("//STATUS: realizable\n", encoding="utf-8")
    exceptions = tmp_path / "exceptions.tsv"
    write_status_exceptions(
        exceptions,
        [("case.tlsf", "realizable", "unrealizable", "")],
    )

    with pytest.raises(coverage.CoverageError, match="empty evidence"):
        coverage.read_status_exceptions(exceptions, tmp_path)


def run_result(*, returncode=0, timed_out=False, resource_limited=False):
    return coverage.RunResult(
        stdout="",
        stderr="",
        returncode=returncode,
        seconds=0.0,
        timed_out=timed_out,
        resource_limited=resource_limited,
    )


@pytest.mark.parametrize("returncode", [0, 1, -9, -15])
def test_resource_limit_normalizes_to_memout(returncode):
    assert coverage.normalize_result(run_result(resource_limited=True, returncode=returncode)) == (
        "MEMOUT",
        "memory",
    )


def test_negative_return_code_normalizes_to_signal_crash():
    assert coverage.normalize_result(run_result(returncode=-9)) == (
        "CRASH",
        "signal:9",
    )


@pytest.fixture
def campaign(tmp_path):
    binary = tmp_path / "solver"
    binary.write_text("#!/bin/sh\necho REALIZABLE\n")
    binary.chmod(0o755)
    (tmp_path / "case.tlsf").write_text("//STATUS: realizable\n")
    (tmp_path / "list").write_text("case.ltl\n")
    (tmp_path / "map").write_text("instance\ttlsf\ncase.ltl\tcase.tlsf\n")
    # An empty table rather than a path to nothing: this fixture wants "no
    # corrections", and a named file that does not exist now says the caller
    # misconfigured the run.
    write_status_exceptions(tmp_path / "exceptions.tsv", [])
    return coverage.build_parser().parse_args([
        "--bin", str(binary), "--solver-label", "candidate",
        "--list", str(tmp_path / "list"), "--tlsf-map", str(tmp_path / "map"),
        "--tlsf-corpus", str(tmp_path),
        "--status-exceptions", str(tmp_path / "exceptions.tsv"),
        "--caps", "17", "--memory-max", "8G", "--memory-swap-max", "0",
        "--output", str(tmp_path / "out.tsv"), "--acacia-sha", "frozen-sha",
        "--preset", "test-preset", "--collect-rusage",
    ])


def test_scoped_rusage_and_worker_capture(monkeypatch, campaign, tmp_path):
    calls = []
    def scoped(cmd, **kwargs):
        calls.append((cmd, kwargs))
        return coverage.RunResult("REALIZABLE\n", "ACACIA_RUSAGE 1.23 0.07 2048\n",
                                  0, 1.5, False, memory_peak_bytes=4096,
                                  scope_unit="acacia-test.scope")
    monkeypatch.setattr(coverage, "run_systemd_scope", scoped)
    campaign.worker_records_dir = tmp_path / "records"
    assert coverage.run(campaign) == 0
    cmd, options = calls[0]
    assert cmd[:4] == ["/usr/bin/time", "-q", "-f", "ACACIA_RUSAGE %U %S %M"]
    assert options["timeout"] == 17 and options["memory_max"] == "8G"
    assert options["env"]["ACACIA_DIAG_INSTANCE"] == "case.ltl"
    assert pathlib.Path(options["env"]["ACACIA_SPOT_CAPTURE_DIR"]).is_dir()
    with open(campaign.output) as source:
        row, = csv.DictReader(source, delimiter="\t")
    assert row["result"] == "REALIZABLE"
    assert row["cpu_seconds"] == "1.300000"
    assert row["max_process_rss_bytes"] == str(2048 * 1024)
    assert row["scope_memory_peak_bytes"] == "4096"
    assert row["scope_unit"] == "acacia-test.scope"
    assert row["worker_records_dir"] == str(campaign.worker_records_dir)
    campaign.resume = True
    assert coverage.run(campaign) == 0
    assert len(calls) == 1


def test_legacy_resume_preserves_measurements_and_does_not_rerun(monkeypatch, campaign):
    calls = []
    def scoped(*args, **kwargs):
        calls.append(1)
        return coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False, memory_peak_bytes=0)
    monkeypatch.setattr(coverage, "run_systemd_scope", scoped)
    assert coverage.run(campaign) == 0
    with open(campaign.output) as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert rows[0]["scope_memory_peak_bytes"] == "0"
    fields = [field for field in coverage.OUTPUT_COLUMNS if field != "scope_unit"]
    with open(campaign.output, "w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows({key: value for key, value in row.items() if key != "scope_unit"} for row in rows)
    campaign.resume = True
    assert coverage.run(campaign) == 0
    assert calls == [1]
    with open(campaign.output) as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        assert reader.fieldnames == coverage.OUTPUT_COLUMNS
        result, = reader
    assert result["scope_unit"] == ""
    assert result["scope_memory_peak_bytes"] == "0"
    assert result["result"] == "REALIZABLE" and result["seconds"] == "0.1"


@pytest.mark.parametrize("field,value", [
    ("flags", "-r small"), ("acacia_sha", "other-sha"),
    ("preset", "other-preset"), ("memory_max", "4G"),
    ("memory_swap_max", "1G"), ("collect_rusage", False),
])
def test_resume_rejects_changed_treatment(monkeypatch, campaign, field, value):
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **kw:
                        coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False))
    assert coverage.run(campaign) == 0
    campaign.resume = True
    setattr(campaign, field, value)
    with pytest.raises(coverage.CoverageError, match="differs"):
        coverage.run(campaign)


def test_resume_rejects_rebuilt_binary_and_censored_usage_stays_absent(monkeypatch, campaign):
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **kw:
                        coverage.RunResult("", "", -15, 17.1, True))
    assert coverage.run(campaign) == 0
    with open(campaign.output) as source:
        row, = csv.DictReader(source, delimiter="\t")
    assert row["result"] == "TIMEOUT"
    assert row["cpu_seconds"] == row["max_process_rss_bytes"] == row["scope_memory_peak_bytes"] == ""
    pathlib.Path(campaign.bin).write_text("#!/bin/sh\necho UNKNOWN\n")
    campaign.resume = True
    with pytest.raises(coverage.CoverageError, match="differs"):
        coverage.run(campaign)


def test_named_status_exceptions_must_exist(tmp_path):
    # The failure this prevents is expensive and looks like something else: an
    # empty table makes every adjudicated wrong //STATUS read as a fresh verdict
    # conflict, which under the default policy aborts the run, which looks like
    # a soundness bug in the solver.
    with pytest.raises(coverage.CoverageError, match="does not exist"):
        coverage.read_status_exceptions(
            tmp_path / "absent.tsv", tmp_path, required=True)


def test_absent_default_status_exceptions_warns_but_proceeds(tmp_path, capsys):
    assert coverage.read_status_exceptions(tmp_path / "absent.tsv", tmp_path) == {}
    assert "WARNING" in capsys.readouterr().err


def test_empty_status_exceptions_table_is_not_an_error(tmp_path):
    write_status_exceptions(tmp_path / "exceptions.tsv", [])
    assert coverage.read_status_exceptions(
        tmp_path / "exceptions.tsv", tmp_path, required=True) == {}
