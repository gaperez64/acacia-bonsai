"""Recover final cgroup accounting without misattributing a benchmark result."""
from __future__ import annotations

import importlib.util
import json
import pathlib

import pytest


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "benchmarking" / "annotate-scope-results.py"
spec = importlib.util.spec_from_file_location("scope_accounting", SCRIPT)
accounting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(accounting)


def unit(**changes):
    return dict(dict(unit="acacia-test.scope", argv=["/solver", "-r", "small", "-T", "/corpus/case.tlsf"],
                     end=100.0, cpu=None, memory=None, swap=None, oom=False), **changes)


def row(**changes):
    return dict(dict(solver_label="candidate", tlsf_file="case.tlsf", result="CRASH",
                     resource_reason="signal:15", timed_out="false", flags="-r small",
                     timestamp_utc="1970-01-01T00:01:40.500000Z"), **changes)


MANIFEST = {"candidate": dict(binary="/solver", flags="-r small")}


def test_final_journal_accounting_retains_zero_and_oom(tmp_path):
    path = tmp_path / "journal.jsonl"
    entries = [
        dict(USER_UNIT="acacia-test.scope", MESSAGE="Started acacia-test.scope - [systemd-run] /solver -r small -T /corpus/case.tlsf.", __REALTIME_TIMESTAMP="90000000"),
        dict(USER_UNIT="acacia-test.scope", UNIT_RESULT="oom-kill", __REALTIME_TIMESTAMP="99900000"),
        dict(USER_UNIT="acacia-test.scope", CPU_USAGE_NSEC="1250000000", MEMORY_PEAK="8589934592", MEMORY_SWAP_PEAK="0", __REALTIME_TIMESTAMP="100000000"),
    ]
    path.write_text("".join(json.dumps(entry) + "\n" for entry in entries))
    units = accounting.read_journal(path)
    matched, source = accounting.match_unit(row(), units, MANIFEST)
    out = accounting.annotate(row(), matched, source)
    assert out["result"] == "MEMOUT"
    assert out["original_result"] == "CRASH"
    assert out["original_resource_reason"] == "signal:15"
    assert out["scope_cpu_seconds"] == "1.250000000"
    assert out["scope_memory_peak_bytes"] == "8589934592"
    assert out["scope_memory_swap_peak_bytes"] == "0"
    assert out["scope_accounting_source"] == "argv+timestamp"


def test_recorded_unit_takes_precedence_over_legacy_timestamp():
    value = unit(end=1000.0)
    assert accounting.match_unit(row(scope_unit=value["unit"]), {value["unit"]: value}, {}) == (value, "unit")


@pytest.mark.parametrize("changes", [
    {"tlsf_file": "different.tlsf"}, {"flags": "-u formula"},
    {"timestamp_utc": "1970-01-01T00:01:50Z"}, {"solver_label": "unrecorded"},
])
def test_legacy_matching_requires_same_treatment_and_close_time(changes):
    assert accounting.match_unit(row(**changes), {"a": unit()}, MANIFEST) == (None, "missing")


def test_ambiguous_repetitions_are_not_silently_assigned():
    units = {"a": unit(end=100.0), "b": unit(unit="acacia-other.scope", end=101.0)}
    assert accounting.match_unit(row(), units, MANIFEST) == (None, "ambiguous")


@pytest.mark.parametrize("result,timed_out", [("REALIZABLE", "false"), ("UNREALIZABLE", "false"), ("TIMEOUT", "true"), ("TIMEOUT", "false")])
def test_oom_never_overwrites_an_answer_or_a_timeout(result, timed_out):
    out = accounting.annotate(row(result=result, timed_out=timed_out), unit(oom=True), "unit")
    assert out["result"] == result
    assert out["scope_oom_kill"] == "true"


def test_missing_accounting_is_not_measured_zero():
    missing = accounting.annotate(row(), unit(), "unit")
    measured = accounting.annotate(row(), unit(cpu=0, memory=0, swap=0), "unit")
    assert "scope_cpu_seconds" not in missing
    assert "scope_memory_peak_bytes" not in missing
    assert measured["scope_cpu_seconds"] == "0.000000000"
    assert measured["scope_memory_peak_bytes"] == measured["scope_memory_swap_peak_bytes"] == "0"
    assert accounting.annotate(row(), None, "ambiguous")["scope_accounting_source"] == "unavailable:ambiguous"
