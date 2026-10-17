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


@pytest.mark.parametrize("platform", ["linux", "darwin"])
def test_scoped_rusage_and_worker_capture(monkeypatch, campaign, tmp_path, platform):
    monkeypatch.setattr(coverage.sys, "platform", platform)
    calls = []
    def scoped(cmd, **kwargs):
        calls.append((cmd, kwargs))
        return coverage.RunResult("REALIZABLE\n",
                                  "ACACIA_RUSAGE 1.23 0.07 2048\n" if platform != "linux" else "",
                                  0, 1.5, False, memory_peak_bytes=4096, scope_unit="acacia-test.scope",
                                  cpu_seconds=1.3, max_process_rss_bytes=2048 * 1024)
    monkeypatch.setattr(coverage, "run_systemd_scope", scoped)
    campaign.worker_records_dir = tmp_path / "records"
    assert coverage.run(campaign) == 0
    cmd, options = calls[0]
    if platform == "linux":
        assert cmd[0] == str(pathlib.Path(campaign.bin).resolve())
        assert "/usr/bin/time" not in cmd
    else:
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


def test_resume_row_index_and_interrupted_phase_records(monkeypatch, campaign, tmp_path):
    records = tmp_path / "phase-records"
    campaign.phase_records_dir = records
    seen = []

    def scoped(_cmd, **kwargs):
        directory = pathlib.Path(kwargs["env"]["ACACIA_PHASE_RECORDS"])
        seen.append(directory)
        (directory / "fresh.jsonl").write_text('{"phase":"record_summary"}\n')
        return coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False)

    monkeypatch.setattr(coverage, "run_systemd_scope", scoped)
    assert coverage.run(campaign) == 0
    assert seen[0].name == "case.ltl-0"
    (tmp_path / "another.tlsf").write_text("//STATUS: realizable\n")
    (tmp_path / "list").write_text("case.ltl\nanother.ltl\n")
    (tmp_path / "map").write_text(
        "instance\ttlsf\ncase.ltl\tcase.tlsf\nanother.ltl\tanother.tlsf\n")
    interrupted = records / "candidate/17/another.ltl-1"
    interrupted.mkdir(parents=True)
    (interrupted / "stale.jsonl").write_text("{torn")
    campaign.resume = True
    assert coverage.run(campaign) == 0
    assert len(seen) == 2 and seen[1] == interrupted
    assert not (interrupted / "stale.jsonl").exists()
    quarantined, = interrupted.parent.glob("another.ltl-1.interrupted-*")
    assert (quarantined / "stale.jsonl").read_text() == "{torn"
    with pathlib.Path(campaign.output).open(newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    assert [row["run_index"] for row in rows] == ["0", "1"]


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


def test_primary_columns_and_rows_remain_compatible_with_master(
        monkeypatch, campaign, master_benchmark_reader):
    master = master_benchmark_reader("run-syntcomp26-coverage.py")
    result = coverage.RunResult(
        "REALIZABLE\n", "", 0, 0.1, False, scope_unit="acacia-test.scope",
        memory_peak_bytes=4096, scope_memory_peak_source="cgroup-v2/memory.peak",
        scope_memory_peak_missing_reason="", memory_cgroup="/invocation")
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *args, **kwargs: result)
    assert coverage.run(campaign) == 0
    output = pathlib.Path(campaign.output)
    with output.open(newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        assert reader.fieldnames == master.OUTPUT_COLUMNS
        primary = list(reader)
    assert master.load_output(output) == primary
    restored = coverage.load_output(output)
    assert restored[0]["scope_memory_peak_source"] == "cgroup-v2/memory.peak"
    assert restored[0]["memory_cgroup"] == "/invocation"
    campaign.resume = True
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *args, **kwargs: pytest.fail("rerun"))
    assert coverage.run(campaign) == 0
    assert coverage.load_output(output) == restored


def test_expanded_primary_is_preserved_when_moved_to_sidecar(monkeypatch, campaign):
    result = coverage.RunResult(
        "REALIZABLE\n", "", 0, 0.1, False, scope_unit="acacia-test.scope",
        memory_peak_bytes=4096, scope_memory_peak_source="cgroup-v2/memory.peak",
        scope_memory_peak_missing_reason="", memory_cgroup="/invocation")
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *args, **kwargs: result)
    assert coverage.run(campaign) == 0
    output = pathlib.Path(campaign.output)
    rows = coverage.load_output(output)
    coverage.atomic_write_tsv(output, coverage.OUTPUT_COLUMNS + coverage.MEMORY_COLUMNS, rows)
    output.with_name(output.name + ".memory.tsv").unlink()
    original = output.read_bytes()
    campaign.resume = True
    assert coverage.run(campaign) == 0
    assert output.with_name(f"{output.stem}-legacy.tsv").read_bytes() == original
    assert coverage.load_output(output) == rows


@pytest.mark.parametrize("capture", ["all", "filter", "consumer"])
def test_late_answer_during_timeout_cleanup_is_never_solved(tmp_path, capture):
    import os
    import signal
    import sys
    import benchlib
    script = tmp_path / "late.py"
    script.write_text("""import signal, time

def finish(signum, frame):
    time.sleep(.05)
    print('REALIZABLE', flush=True)
    raise SystemExit(0)

signal.signal(signal.SIGTERM, finish)
print('ready', flush=True)
while True:
    signal.pause()
""")
    kwargs = {}
    consumed = []
    if capture == "filter":
        kwargs["capture_filter"] = lambda line: True
    if capture == "consumer":
        kwargs["capture_consumer"] = consumed.append
    # The scoped timeout handler runs before the launcher is signalled.
    def stop_parent():
        os.kill(process[0].pid, signal.SIGTERM)
        process[0].wait(timeout=.5)
    process = []
    popen = benchlib.subprocess.Popen
    def start(*args, **kwargs):
        child = popen(*args, **kwargs)
        process.append(child)
        return child
    with pytest.MonkeyPatch.context() as patch:
        patch.setattr(benchlib.subprocess, "Popen", start)
        run = benchlib.run_process_group([sys.executable, str(script)], .2,
                                        timeout_handler=stop_parent, **kwargs)
    output = "".join(consumed) if capture == "consumer" else run.stdout
    assert "REALIZABLE" in output
    assert run.timed_out and run.returncode == 124
    assert run.seconds >= .25
    assert coverage.normalize_result(run) == ("TIMEOUT", "timeout")
    assert benchlib.par2(0, 1, .2) == .4


def read_summary(output):
    with pathlib.Path(output).with_name(pathlib.Path(output).stem + "-summary.tsv").open() as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def add_second_target(campaign, tmp_path):
    (tmp_path / "other.tlsf").write_text("//STATUS: realizable\n")
    pathlib.Path(campaign.list).write_text("other.ltl\n")
    pathlib.Path(campaign.tlsf_map).write_text(
        "instance\ttlsf\ncase.ltl\tcase.tlsf\nother.ltl\tother.tlsf\n")
    campaign.resume = True


def test_resume_disjoint_lists_summary_rebuild_and_export(monkeypatch, campaign, tmp_path):
    calls = []
    def invoke(*args, **kwargs):
        calls.append(args)
        return coverage.RunResult("REALIZABLE\n", "", 0, 0.25, False)
    monkeypatch.setattr(coverage, "run_systemd_scope", invoke)
    assert coverage.run(campaign) == 0
    add_second_target(campaign, tmp_path)
    assert coverage.run(campaign) == 0
    assert coverage.run(campaign) == 0  # unchanged resume does not run either input again
    assert len(calls) == 2
    raw = pathlib.Path(campaign.output)
    original = raw.read_bytes()
    assert [r["instance"] for r in coverage.load_output(raw)] == ["case.ltl", "other.ltl"]
    expected = read_summary(raw)
    assert [r["instance"] for r in expected] == ["case.ltl", "other.ltl"]
    monkeypatch.setattr(coverage, "campaign_main", lambda *a: pytest.fail("report entered guard"))
    assert coverage.main(["summarize", "--runs", str(raw)]) == 0
    assert read_summary(raw) == expected
    assert raw.read_bytes() == original
    panel = tmp_path / "panel.list"
    panel.write_text("case.ltl\nother.ltl\n")
    summary = raw.with_name(raw.stem + "-summary.tsv")
    exported = coverage.export_cactus_csv(summary, raw, panel, 17, tmp_path / "result.csv")
    with exported.open() as stream:
        assert [r["instance"] for r in csv.DictReader(stream, delimiter="\t")] == [
            "case.ltl", "other.ltl"]


def test_shorter_resume_preserves_old_cap_and_failures(monkeypatch, campaign, tmp_path):
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("", "", -15, 17.01, True))
    assert coverage.run(campaign) == 0
    add_second_target(campaign, tmp_path)
    campaign.caps = [1]
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False))
    assert coverage.run(campaign) == 0
    a, b = read_summary(campaign.output)
    assert a["max_cap_s"] == b["max_cap_s"] == "17"
    assert a["failure_kind_at_max_cap"] == "TIMEOUT"
    assert a["still_unsolved_at_max_cap"] == "true"
    assert b["smallest_cap_solved"] == "1"
    assert b["still_unsolved_at_max_cap"] == "false"
    assert b["failure_kind_at_max_cap"] == ""  # no fabricated 17 s observation



def test_resume_preserves_requested_caps_that_were_never_executed(monkeypatch, campaign, tmp_path):
    campaign.caps = [1, 5, 17, 60]
    replies = iter([coverage.RunResult("", "", -15, 1.01, True),
                    coverage.RunResult("REALIZABLE\n", "", 0, 2.0, False)])
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k: next(replies))
    assert coverage.run(campaign) == 0
    assert read_summary(campaign.output)[0]["max_cap_s"] == "60"
    add_second_target(campaign, tmp_path)
    campaign.caps = [1]
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False))
    assert coverage.run(campaign) == 0
    first, second = read_summary(campaign.output)
    assert first["max_cap_s"] == second["max_cap_s"] == "60"
    assert first["smallest_cap_solved"] == "5"
    assert first["failure_kind_at_max_cap"] == second["failure_kind_at_max_cap"] == ""
    output = pathlib.Path(campaign.output)
    assert coverage.load_requested_caps(output) == {campaign.solver_label: {1, 5, 17, 60}}
    output.with_name(f"{output.stem}-summary.tsv").unlink()
    assert coverage.main(["summarize", "--runs", str(output), "--cap", "1"]) == 0
    assert read_summary(output) == [first, second]

def test_staged_summary_earliest_decision_and_explicit_missing_panel(monkeypatch, campaign, tmp_path):
    campaign.caps = [1, 5, 17, 60]
    replies = iter([coverage.RunResult("", "", -15, 1.01, True),
                    coverage.RunResult("REALIZABLE\n", "", 0, 2.0, False)])
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k: next(replies))
    assert coverage.run(campaign) == 0
    row, = read_summary(campaign.output)
    assert row["max_cap_s"] == "60" and row["smallest_cap_solved"] == "5"
    assert row["decisive_seconds"] == "2.0" and row["failure_kind_at_max_cap"] == ""
    panel = tmp_path / "panel.list"
    panel.write_text("unobserved.ltl\ncase.ltl\n")
    assert coverage.main(["summarize", "--runs", campaign.output,
                          "--list", str(panel), "--cap", "60"]) == 0
    missing, solved = read_summary(campaign.output)
    assert missing["instance"] == "unobserved.ltl"
    assert missing["still_unsolved_at_max_cap"] == "true"
    assert missing["failure_kind_at_max_cap"] == missing["decisive_result"] == ""
    assert solved == row
    with pytest.raises(coverage.CoverageError, match="expected uniform cap"):
        coverage.load_uniform_observations(
            pathlib.Path(campaign.output).with_name("out-summary.tsv"),
            pathlib.Path(campaign.output), panel, 60)


def test_summary_label_isolation_duplicate_and_provenance_checks(saved_export):
    a = saved_export.raw[0]
    b = {**a, "solver_label": "other", "cap_s": "60", "binary_sha256": "b" * 64}
    result = coverage.summary_rows("saved", [a, b])
    assert len(result) == 1 and result[0]["max_cap_s"] == "17"
    with pytest.raises(coverage.CoverageError, match="duplicate"):
        coverage.summary_rows("saved", [a, a])
    with pytest.raises(coverage.CoverageError, match="duplicate"):
        coverage.summary_rows("saved", [a], instances=[a["instance"], a["instance"]])
    with pytest.raises(coverage.CoverageError, match="mixed provenance"):
        coverage.summary_rows("saved", [a, {**a, "instance": "other", "flags": "changed"}])


def test_conflicts_survive_disjoint_resume_and_block_export(monkeypatch, campaign, tmp_path):
    campaign.conflict_policy = "collect"
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("UNREALIZABLE\n", "", 1, 0.1, False))
    assert coverage.run(campaign) == 3
    add_second_target(campaign, tmp_path)
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False))
    assert coverage.run(campaign) == 3
    raw = pathlib.Path(campaign.output)
    conflicts = raw.with_name("out-conflicts.tsv")
    before = conflicts.read_bytes()
    assert coverage.main(["summarize", "--runs", str(raw)]) == 3
    assert conflicts.read_bytes() == before
    assert len(read_summary(raw)) == 2
    panel = tmp_path / "panel.list"
    panel.write_text("case.ltl\nother.ltl\n")
    with pytest.raises(coverage.CoverageError, match="unresolved verdict conflicts"):
        coverage.load_uniform_observations(raw.with_name("out-summary.tsv"), raw, panel, 17)
    campaign.conflict_policy = "stop"
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k: pytest.fail("unresolved run"))
    assert coverage.run(campaign) == 1


def test_summary_includes_conflict_only_ids_and_all_labels(saved_export):
    f = saved_export
    coverage.atomic_write_tsv(f.runs, coverage.OUTPUT_COLUMNS, f.raw[:1])
    conflict = dict(zip(coverage.CONFLICT_COLUMNS,
                        ["other", "sidecar-only", "x.tlsf", "60", "REALIZABLE", "status",
                         "UNREALIZABLE", "0.1"]))
    coverage.atomic_write_tsv(f.runs.with_name("saved-conflicts.tsv"),
                              coverage.CONFLICT_COLUMNS, [conflict])
    assert coverage.main(["summarize", "--runs", str(f.runs)]) == 3
    rows = read_summary(f.runs)
    assert [(r["solver_label"], r["instance"], r["max_cap_s"]) for r in rows] == [
        ("other", "sidecar-only", "60"), ("saved", "real.ltl", "17")]


def test_opposing_unannotated_resumes_are_durable_conflicts(monkeypatch, campaign, tmp_path):
    (tmp_path / "case.tlsf").write_text("//STATUS: unknown\n")
    campaign.conflict_policy = "collect"
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False))
    assert coverage.run(campaign) == 0
    campaign.resume, campaign.caps = True, [1]
    monkeypatch.setattr(coverage, "run_systemd_scope", lambda *a, **k:
                        coverage.RunResult("UNREALIZABLE\n", "", 1, 0.1, False))
    assert coverage.run(campaign) == 3
    conflicts = coverage.load_conflicts(tmp_path / "out-conflicts.tsv")
    assert len(conflicts) == 1
    assert conflicts[0]["expected"] == "REALIZABLE"
    assert conflicts[0]["actual"] == "UNREALIZABLE"
    assert coverage.main(["summarize", "--runs", campaign.output]) == 3
    (tmp_path / "out-conflicts.tsv").unlink()
    with pytest.raises(coverage.CoverageError, match="opposing verdicts"):
        coverage.summary_rows("candidate", coverage.load_output(pathlib.Path(campaign.output)))
