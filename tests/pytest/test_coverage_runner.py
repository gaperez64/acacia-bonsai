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
from types import SimpleNamespace

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


@pytest.fixture
def saved_export(tmp_path):
    cases = [
        ("real.ltl", "REALIZABLE", "0", "1.25"),
        ("unreal.ltl", "UNREALIZABLE", "1", "2.75"),
        ("memory.ltl", "MEMOUT", "-9", "0.125"),
        ("crash.ltl", "CRASH", "-11", "0.25"),
        ("timeout.ltl", "TIMEOUT", "-15", "17.04"),
        ("unknown.ltl", "UNKNOWN", "2", "0.5"),
    ]
    fixture = SimpleNamespace(
        summary=tmp_path / "saved-summary.tsv", runs=tmp_path / "saved.tsv",
        instance_list=tmp_path / "all.list", output=tmp_path / "out.csv",
        sidecar=tmp_path / "out.raw.tsv", raw=[], summaries=[],
    )
    fixture.instance_list.write_text("# expected IDs\n\n" + "\n".join(c[0] for c in cases))
    for instance, result, exit_code, seconds in cases:
        fixture.raw.append({
            **dict.fromkeys(coverage.OUTPUT_COLUMNS, ""),
            "solver_label": "saved", "instance": instance, "cap_s": "17",
            "result": result, "seconds": seconds, "exit_code": exit_code,
            "expectation_source": "none", "binary_sha256": "a" * 64,
            "acacia_sha": "frozen", "preset": "test", "memory_max": "8G",
            "memory_swap_max": "0",
        })
        solved = result in {"REALIZABLE", "UNREALIZABLE"}
        fixture.summaries.append({
            "solver_label": "saved", "instance": instance,
            "smallest_cap_solved": "17" if solved else "",
            "decisive_result": result if solved else "",
            "decisive_seconds": seconds if solved else "",
            "still_unsolved_at_max_cap": "false" if solved else "true",
            "failure_kind_at_max_cap": result, "max_cap_s": "17",
        })
    return fixture


def write_export_inputs(fixture):
    coverage.atomic_write_tsv(fixture.runs, coverage.OUTPUT_COLUMNS, fixture.raw)
    coverage.atomic_write_tsv(fixture.summary, coverage.SUMMARY_COLUMNS, fixture.summaries)


def export_saved(fixture, cap=17):
    write_export_inputs(fixture)
    return coverage.export_cactus_csv(
        fixture.summary, fixture.runs, fixture.instance_list, cap, fixture.output,
    )


def read_export(path, delimiter=","):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream, delimiter=delimiter))


def test_cactus_export_preserves_verdicts_exits_and_raw_failures(saved_export):
    fixture = saved_export
    assert export_saved(fixture) == fixture.sidecar
    rows = read_export(fixture.output)
    raw = read_export(fixture.sidecar, "\t")
    assert [row["result"] for row in rows] == [
        "REALIZABLE", "UNREALIZABLE", "RESOURCE_LIMIT", "ERROR", "TIMEOUT", "UNKNOWN",
    ]
    assert [row["exit"] for row in rows] == ["0", "1", "-9", "-11", "-15", "2"]
    assert [float(row["seconds"]) for row in rows] == [1.25, 2.75, 17, 17, 17, 17]
    assert [row["result"] for row in raw] == [row["result"] for row in fixture.raw]
    assert [row["exit_code"] for row in raw] == [row["exit_code"] for row in fixture.raw]
    assert [row["seconds"] for row in raw] == [row["seconds"] for row in fixture.raw]
    assert {row["cap_s"] for row in raw} == {"17"}


@pytest.mark.parametrize("source", ["summaries", "raw"])
@pytest.mark.parametrize("problem", ["missing", "extra", "duplicate"])
def test_cactus_export_requires_exact_unique_observations(saved_export, source, problem):
    rows = getattr(saved_export, source)
    if problem == "missing":
        rows.pop()
    else:
        rows.append({**rows[-1], "instance": "extra.ltl" if problem == "extra" else rows[-1]["instance"]})
    # A failed validation also must not truncate previous successful exports.
    saved_export.output.write_text("keep CSV")
    saved_export.sidecar.write_text("keep raw")
    with pytest.raises(coverage.CoverageError, match="instance"):
        export_saved(saved_export)
    assert saved_export.output.read_text() == "keep CSV"
    assert saved_export.sidecar.read_text() == "keep raw"


@pytest.mark.parametrize("source,field,value", [
    ("summaries", "max_cap_s", "5"),
    ("summaries", "max_cap_s", "1,5,17"),
    ("summaries", "smallest_cap_solved", "1"),
    ("raw", "cap_s", "5"),
])
def test_cactus_export_rejects_wrong_or_mixed_caps(saved_export, source, field, value):
    getattr(saved_export, source)[0][field] = value
    with pytest.raises(coverage.CoverageError, match="cap"):
        export_saved(saved_export)
    assert not saved_export.output.exists()
    assert not saved_export.sidecar.exists()


def test_cactus_export_rejects_hidden_staged_observation(saved_export):
    saved_export.raw.append({**saved_export.raw[-1], "cap_s": "1"})
    with pytest.raises(coverage.CoverageError, match="uniform cap"):
        export_saved(saved_export)


@pytest.mark.parametrize("cap", [5, 0, -1, float("inf"), float("nan")])
def test_cactus_export_checks_expected_cap(saved_export, cap):
    with pytest.raises(coverage.CoverageError, match="cap"):
        export_saved(saved_export, cap)


@pytest.mark.parametrize("status", ["ALIEN", "UNSOLVED", "", "REALIZABLE"])
def test_cactus_export_rejects_unsupported_failure_instead_of_fabricating_timeout(
    saved_export, status,
):
    saved_export.summaries[-1]["failure_kind_at_max_cap"] = status
    with pytest.raises(coverage.CoverageError, match="unsupported status mapping"):
        export_saved(saved_export)
    assert not saved_export.output.exists()


@pytest.mark.parametrize("source,field", [("raw", "seconds"), ("summaries", "decisive_seconds")])
@pytest.mark.parametrize("seconds", ["-0.01", "inf", "nan", "oops"])
def test_cactus_export_rejects_invalid_seconds(saved_export, source, field, seconds):
    getattr(saved_export, source)[0][field] = seconds
    with pytest.raises(coverage.CoverageError, match="seconds"):
        export_saved(saved_export)


@pytest.mark.parametrize("index,exit_code", [(0, "1"), (1, "0"), (0, "bad")])
def test_cactus_export_requires_solved_exit_agreement(saved_export, index, exit_code):
    saved_export.raw[index]["exit_code"] = exit_code
    with pytest.raises(coverage.CoverageError, match="exit_code"):
        export_saved(saved_export)


@pytest.mark.parametrize("field,value", [
    ("decisive_result", "UNKNOWN"), ("decisive_seconds", "3"),
    ("failure_kind_at_max_cap", "UNREALIZABLE"),
    ("still_unsolved_at_max_cap", "invalid"), ("solver_label", "other"),
])
def test_cactus_export_rejects_summary_run_disagreement(saved_export, field, value):
    saved_export.summaries[0][field] = value
    with pytest.raises(coverage.CoverageError):
        export_saved(saved_export)


def test_cactus_export_rejects_conflicts_collected_by_runner(saved_export):
    path = saved_export.runs.with_name("saved-conflicts.tsv")
    coverage.atomic_write_tsv(path, coverage.CONFLICT_COLUMNS, [{
        "solver_label": "saved", "instance": "real.ltl", "tlsf_file": "real.tlsf",
        "cap_s": "17", "expected": "UNREALIZABLE", "expectation_source": "status",
        "actual": "REALIZABLE", "seconds": "1.25",
    }])
    with pytest.raises(coverage.CoverageError, match="unresolved verdict conflicts"):
        export_saved(saved_export)
    assert not saved_export.output.exists()
    assert not saved_export.sidecar.exists()


def test_cactus_export_accepts_empty_conflict_sidecar(saved_export):
    coverage.atomic_write_tsv(saved_export.runs.with_name("saved-conflicts.tsv"),
                              coverage.CONFLICT_COLUMNS, [])
    export_saved(saved_export)
    assert saved_export.output.exists()


def test_cactus_export_rejects_duplicate_expected_ids(saved_export):
    saved_export.instance_list.write_text("real.ltl\nreal.ltl\n")
    with pytest.raises(coverage.CoverageError, match="duplicate instances"):
        export_saved(saved_export)


def test_cactus_export_rejects_mixed_provenance(saved_export):
    saved_export.raw[-1]["binary_sha256"] = "b" * 64
    with pytest.raises(coverage.CoverageError, match="mixed provenance"):
        export_saved(saved_export)


def test_cactus_export_subcommand_never_enters_campaign_guard(saved_export, monkeypatch):
    write_export_inputs(saved_export)
    def forbidden_campaign():
        pytest.fail("reporting must not inspect systemd scopes or run a campaign")
    monkeypatch.setattr(coverage, "campaign_main", forbidden_campaign)
    monkeypatch.setattr(sys, "argv", [
        str(SCRIPT), "export-cactus", "--summary", str(saved_export.summary),
        "--list", str(saved_export.instance_list), "--cap", "17",
        "--output", str(saved_export.output),
    ])
    assert coverage.main() == 0
    assert saved_export.output.exists() and saved_export.sidecar.exists()


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
