"""Focused unit tests for run-syntcomp26-coverage.py.

These tests cover input validation and result normalization without invoking a
solver or requiring any build products.
"""
from __future__ import annotations

import argparse
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


def test_resource_limit_normalizes_to_memout():
    assert coverage.normalize_result(run_result(resource_limited=True)) == (
        "MEMOUT",
        "memory",
    )


def test_negative_return_code_normalizes_to_signal_crash():
    assert coverage.normalize_result(run_result(returncode=-9)) == (
        "CRASH",
        "signal:9",
    )
