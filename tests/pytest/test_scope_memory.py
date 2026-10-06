"""Fake v2 files and lifecycle events; never contact the host's systemd manager."""
import csv
import importlib.util
import json
import pathlib
import shutil
import sys
from types import SimpleNamespace

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
import benchlib  # noqa: E402
import scope_memory as memory  # noqa: E402


def files(cgroup, events, peak=4096):
    for name, value in {"memory.peak": str(peak), "memory.events": events,
                        "cgroup.events": "populated 0\n", "cgroup.kill": "",
                        "cgroup.procs": ""}.items():
        (cgroup / name).write_text(value)


@pytest.mark.parametrize("outcome,status,events,group", [
    ("normal", 0, "oom 0\noom_kill 0\noom_group_kill 0\n", True),
    ("timeout", 9, "oom 0\noom_kill 0\noom_group_kill 0\n", True),
    ("signal", 15, "oom 0\noom_kill 0\noom_group_kill 0\n", True),
    ("child-oom", 0, "oom 1\noom_kill 1\noom_group_kill 0\n", False),
    ("invocation-oom", 9, "oom 1\noom_kill 3\noom_group_kill 1\n", True),
])
def test_owner_retains_cgroup_for_external_read_after_every_exit(
        monkeypatch, tmp_path, outcome, status, events, group):
    base = tmp_path / "scope"
    base.mkdir()
    state = tmp_path / "state"
    state.mkdir()
    if outcome == "timeout":
        (state / "cancel").touch()
    monkeypatch.setattr(memory, "current_cgroup", lambda: base)
    monkeypatch.setattr(memory.os, "fork", lambda: 1234)
    waits = []
    def wait4(pid, flags):
        waits.append(pid)
        if outcome == "timeout" and len(waits) == 1:
            return 0, 0, SimpleNamespace(ru_maxrss=0)
        return pid, status, SimpleNamespace(ru_maxrss=2)
    monkeypatch.setattr(memory.os, "wait4", wait4)
    mkdir = pathlib.Path.mkdir
    def fake_mkdir(path, *args, **kwargs):
        mkdir(path, *args, **kwargs)
        if path.name == "invocation":
            files(path, events)
    monkeypatch.setattr(pathlib.Path, "mkdir", fake_mkdir)
    deleted = []
    def remove(path):
        assert (state / "ack").exists(), "cgroup deleted before external collection"
        assert (path / "memory.max").read_text() == "8589934592"
        assert (path / "memory.swap.max").read_text() == "0"
        assert (path / "memory.oom.group").read_text() == ("1" if group else "0")
        deleted.append(path)
        shutil.rmtree(path)
    monkeypatch.setattr(pathlib.Path, "rmdir", remove)
    observer = memory.MemoryObserver(state)
    code = memory.own_invocation(state, ["fake-solver"], "8G", "0", oom_group=group)
    observer.close()
    assert code == (128 + status if status in {9, 15} else 0)
    assert observer.snapshot["workload_returncode"] == memory.os.waitstatus_to_exitcode(status)
    assert deleted == [base / "invocation"]
    assert observer.snapshot["memory_peak_bytes"] == 4096
    assert observer.snapshot["max_process_rss_bytes"] == 2048
    assert json.loads(observer.snapshot["scope_memory_events"])["oom_kill"] == (
        1 if outcome == "child-oom" else 3 if outcome == "invocation-oom" else 0)
    assert (base / "cgroup.subtree_control").read_text() == "+memory"
    assert (base / "observer/cgroup.procs").read_text() == str(memory.os.getpid())


def assert_incomplete_scope(snapshot, reason):
    assert snapshot["memory_peak_bytes"] is None
    assert snapshot["scope_memory_events"] == ""
    assert snapshot["scope_memory_peak_missing_reason"] == reason
    assert snapshot["scope_memory_events_missing_reason"] == reason
    row = memory.memory_fields(SimpleNamespace(**snapshot))
    statistic = memory.memory_statistic([row], "scope_memory_peak_bytes")
    assert (statistic["numerator"], statistic["denominator"]) == (0, 1)
    assert statistic["missing_reasons"] == {reason: 1}
    assert statistic["median_bytes"] is None
    assert statistic["max_bytes"] is None


@pytest.mark.parametrize("failure", [
    "failed-drain", "invocation-mkdir", "memory.max", "memory.swap.max", "memory.oom.group",
    "started.json", "fork", "wait4", "cancel-kill", "cgroup.kill", "cgroup.events", "missing-populated",
    "invalid-populated",
])
def test_owner_errors_with_readable_cgroup_never_publish_complete_memory(
        monkeypatch, tmp_path, failure):
    base = tmp_path / "scope"
    base.mkdir()
    state = tmp_path / "state"
    state.mkdir()
    monkeypatch.setattr(memory, "current_cgroup", lambda: base)
    def fork():
        if failure == "fork":
            raise OSError("fork failed before final drain")
        return 1234
    monkeypatch.setattr(memory.os, "fork", fork)
    if failure == "cancel-kill":
        (state / "cancel").touch()
    waits = []
    def wait4(pid, flags):
        waits.append(flags)
        if failure == "cancel-kill" and len(waits) == 1:
            return 0, 0, SimpleNamespace(ru_maxrss=0)
        if failure == "wait4" and len(waits) == 1:
            raise OSError("wait4 failed before final drain")
        return pid, 0, SimpleNamespace(ru_maxrss=2)
    monkeypatch.setattr(memory.os, "wait4", wait4)
    mkdir = pathlib.Path.mkdir
    configured = []
    def fake_mkdir(path, *args, **kwargs):
        mkdir(path, *args, **kwargs)
        if path.name == "invocation":
            files(path, "oom 1\noom_kill 1\n")
            configured.append(path)
            if failure == "failed-drain":
                (path / "cgroup.events").write_text("populated 1\n")
            elif failure == "missing-populated":
                (path / "cgroup.events").write_text("frozen 0\n")
            elif failure == "invalid-populated":
                (path / "cgroup.events").write_text("populated invalid\n")
            elif failure == "invocation-mkdir":
                raise FileExistsError("invocation cgroup already exists")
    monkeypatch.setattr(pathlib.Path, "mkdir", fake_mkdir)
    write_text = pathlib.Path.write_text
    def failing_write(path, *args, **kwargs):
        if configured and (path.name == failure
                           or failure == "cancel-kill" and path.name == "cgroup.kill"
                           or failure == "started.json" and path.name == "started.tmp"):
            raise OSError(f"{failure} failed before final drain")
        return write_text(path, *args, **kwargs)
    monkeypatch.setattr(pathlib.Path, "write_text", failing_write)
    read_text = pathlib.Path.read_text
    def failing_read(path, *args, **kwargs):
        if failure == "cgroup.events" and path.name == failure:
            raise OSError("cgroup.events failed before final drain")
        return read_text(path, *args, **kwargs)
    monkeypatch.setattr(pathlib.Path, "read_text", failing_read)
    # A stopped observer makes publication/ack deterministic without host cgroups or sleeps.
    observer = memory.MemoryObserver(state)
    observer.close()
    publish = memory.atomic_json
    def collect_final(path, value):
        publish(path, value)
        if path.name == "ready.json":
            observer.collect(value)
    monkeypatch.setattr(memory, "atomic_json", collect_final)
    if failure == "failed-drain":
        import itertools
        ticks = itertools.count(0, 4)
        monkeypatch.setattr(memory.time, "monotonic", lambda: next(ticks))
    memory.own_invocation(state, ["solver"], "8G", "0")
    ready = json.loads((state / "ready.json").read_text())
    reason = ("workload cgroup did not empty" if failure == "failed-drain" else
              "invocation cgroup already exists" if failure == "invocation-mkdir" else
              "missing or invalid populated state" if failure.endswith("-populated") else
              f"{failure} failed before final drain")
    assert ready["error"] == reason
    assert ready["cgroup"] == str(base / "invocation")
    assert (base / "invocation/memory.peak").read_text() == "4096"
    if failure == "failed-drain":
        assert (base / "invocation/cgroup.events").read_text() == "populated 1\n"
    assert_incomplete_scope(observer.snapshot, reason)
    assert (state / "ack").exists()
    if failure in {"wait4", "cancel-kill"}:
        assert waits == [memory.os.WNOHANG, 0]
    elif failure in {"failed-drain", "cgroup.kill", "cgroup.events",
                     "missing-populated", "invalid-populated"}:
        assert waits == [memory.os.WNOHANG]
    if waits:
        assert observer.snapshot["max_process_rss_bytes"] == 2048
        assert not observer.snapshot["max_process_rss_missing_reason"]
        assert memory.memory_statistic([memory.memory_fields(SimpleNamespace(**observer.snapshot))],
                                       "max_process_rss_bytes")["numerator"] == 1
    else:
        assert observer.snapshot["max_process_rss_bytes"] is None
        assert observer.snapshot["max_process_rss_missing_reason"] == reason


@pytest.mark.parametrize("phase", ["before", "after", "removed-after"])
def test_peak_and_events_require_empty_cgroup_throughout_read(monkeypatch, tmp_path, phase):
    files(tmp_path, "oom 0\noom_kill 0\n")
    read_text = pathlib.Path.read_text
    checks = []
    def read_state(path, *args, **kwargs):
        if path.name == "cgroup.events":
            checks.append(path)
            if phase == "removed-after" and len(checks) == 2:
                raise OSError("drain state disappeared")
            if phase == "before" and len(checks) == 1 or phase == "after" and len(checks) == 2:
                return "populated 1\n"
        return read_text(path, *args, **kwargs)
    monkeypatch.setattr(pathlib.Path, "read_text", read_state)
    reason = ("cgroup.events: drain state disappeared" if phase == "removed-after" else
              "cgroup.events: workload cgroup did not empty")
    assert_incomplete_scope(memory.read_memory(tmp_path), reason)


@pytest.mark.parametrize("state", [None, "frozen 0\n", "populated invalid\n", "invalid\n"])
def test_unreadable_or_invalid_drain_state_excludes_peak_and_events(tmp_path, state):
    files(tmp_path, "oom 0\noom_kill 0\n")
    if state is None:
        (tmp_path / "cgroup.events").unlink()
    else:
        (tmp_path / "cgroup.events").write_text(state)
    snapshot = memory.read_memory(tmp_path)
    reason = snapshot["scope_memory_peak_missing_reason"]
    assert reason.startswith("cgroup.events:")
    assert_incomplete_scope(snapshot, reason)


@pytest.mark.parametrize("peak", [0, -1, (1 << 64) - 1, "max", "invalid"])
def test_missing_or_zero_peaks_are_never_fabricated(tmp_path, peak):
    files(tmp_path, "oom 1\noom_kill 1\n", peak)
    result = memory.read_memory(tmp_path)
    assert result["memory_peak_bytes"] is None
    assert result["scope_memory_peak_missing_reason"]
    assert json.loads(result["scope_memory_events"])["oom_kill"] == 1
    (tmp_path / "memory.peak").unlink()
    assert memory.read_memory(tmp_path)["memory_peak_bytes"] is None


def test_one_statistic_can_be_missing_without_erasing_another(tmp_path):
    files(tmp_path, "oom 0\noom_kill 0\n")
    (tmp_path / "memory.events").unlink()
    result = memory.read_memory(tmp_path)
    assert result["memory_peak_bytes"] == 4096
    assert result["scope_memory_events_missing_reason"]


@pytest.mark.parametrize("timeout,exit_code,workload_code,kills,limited", [
    (False, 0, 0, 0, False), (True, 124, -9, 0, False),
    (False, 0, 0, 1, False), (False, 137, -9, 3, True),
    (False, 143, -15, 0, False), (False, 143, 143, 0, False),
    (False, 1, 1, 1, True),
])
def test_existing_runner_collects_before_stop_and_uses_fresh_scope(
        monkeypatch, tmp_path, timeout, exit_code, workload_code, kills, limited):
    monkeypatch.setattr(benchlib, "ROOT", tmp_path)
    groups, stopped = [], []
    def run(cmd, cap, env, capture_filter, capture_consumer, **kwargs):
        unit = next(arg for arg in cmd if arg.startswith("--unit="))
        assert unit not in groups
        groups.append(unit)
        assert "--oom-group" not in cmd
        assert "--property=Delegate=yes" in cmd
        assert "--property=MemoryMax=infinity" in cmd
        assert cmd[cmd.index("--memory-max") + 1] == "8G"
        state = pathlib.Path(cmd[cmd.index("--state") + 1])
        cgroup = tmp_path / f"invocation-{len(groups)}"
        cgroup.mkdir()
        files(cgroup, f"oom {kills}\noom_kill {kills}\n")
        memory.atomic_json(state / "ready.json", {"cgroup": str(cgroup),
                                                   "max_process_rss_bytes": 2048,
                                                   "workload_returncode": workload_code})
        if timeout:
            kwargs["timeout_handler"]()
        else:
            import time
            for _ in range(100):
                if (state / "ack").exists():
                    break
                time.sleep(0.01)
        assert (state / "ack").exists()
        shutil.rmtree(cgroup)
        return benchlib.RunResult("UNREALIZABLE\n" if exit_code == 1 else
                                  "REALIZABLE\n" if exit_code == 0 else "", "", exit_code,
                                  0.1, timeout)
    monkeypatch.setattr(benchlib, "run_process_group", run)
    monkeypatch.setattr(benchlib, "_stop_user_scope", stopped.append)
    for _ in range(2):
        result = benchlib.run_systemd_scope(["solver"], 17, "8G")
        assert result.memory_peak_bytes == 4096
        assert result.max_process_rss_bytes == 2048
        assert result.resource_limited == limited
        assert result.returncode == (124 if timeout else workload_code)
        spec = importlib.util.spec_from_file_location(
            "memory_coverage", ROOT / "benchmarking/run-syntcomp26-coverage.py")
        coverage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(coverage)
        if exit_code == 143:
            assert coverage.normalize_result(result) == (
                ("CRASH", "signal:15") if workload_code < 0 else ("ERROR", ""))
        if exit_code == 1:
            assert coverage.normalize_result(result) == ("MEMOUT", "memory")
        assert result.scope_memory_peak_source == "cgroup-v2/memory.peak"
        assert not result.scope_memory_peak_missing_reason
    assert len(groups) == len(stopped) == 2


def test_non_linux_reports_unsupported_without_attempting_systemd(monkeypatch):
    monkeypatch.setattr(benchlib.sys, "platform", "darwin")
    monkeypatch.setattr(benchlib, "run_process_group",
                        lambda *args: benchlib.RunResult("", "", 0, 0.1, False))
    result = benchlib.run_systemd_scope(["solver"], 1, "8G")
    assert result.memory_peak_bytes is None and result.max_process_rss_bytes is None
    assert "unsupported" in result.scope_memory_peak_missing_reason
    assert "unsupported" in result.max_process_rss_missing_reason


def test_reports_suppress_memout_only_median_and_disclose_each_statistic():
    rows = [{"result": "REALIZABLE", "scope_memory_peak_bytes": "",
             "scope_memory_peak_missing_reason": "peak file missing", "max_process_rss_bytes": "1024",
             "max_process_rss_source": memory.RSS_SOURCE},
            {"result": "MEMOUT", "scope_memory_peak_bytes": "8589934592",
             "scope_memory_peak_source": "cgroup-v2/memory.peak", "memory_cgroup": "/invocation",
             "max_process_rss_bytes": "", "max_process_rss_missing_reason": "legacy reporter killed"}]
    for column, reason in (("scope_memory_peak_bytes", "peak file missing"),
                           ("max_process_rss_bytes", "legacy reporter killed")):
        statistic = memory.memory_statistic(rows, column)
        assert (statistic["numerator"], statistic["denominator"]) == (1, 2)
        assert statistic["median_bytes"] is None
        assert statistic["missing_reasons"] == {reason: 1}
        assert "MEMOUT" in statistic["selection_bias"]
    spec = importlib.util.spec_from_file_location("memory_threeway", ROOT / "benchmarking/tools/threeway-join.py")
    join = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(join)
    summary = join.peak_memory({"rows": {str(i): row for i, row in enumerate(rows)}})
    assert summary["median_mib"] is None
    assert summary["process_rss"]["numerator"] == 1
    assert summary["scope"]["denominator"] == 2


def test_complete_cohort_has_median_and_no_selection_bias():
    result = memory.memory_statistic([{"scope_memory_peak_bytes": value,
                                     "scope_memory_peak_source": "cgroup-v2/memory.peak",
                                     "memory_cgroup": "/invocation"} for value in ("1024", "2048")], "scope_memory_peak_bytes")
    assert result["median_bytes"] == 1536
    assert result["numerator"] == result["denominator"] == 2
    assert result["missing_reasons"] == {}


def test_journal_annotation_keeps_authoritative_workload_peak():
    spec = importlib.util.spec_from_file_location("memory_annotate",
                                                 ROOT / "benchmarking/annotate-scope-results.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    row = {"result": "REALIZABLE", "scope_memory_peak_bytes": "4096",
           "memory_cgroup": "/scope/invocation"}
    unit = {"unit": "acacia-fake.scope", "oom": False, "cpu": 1,
            "memory": 99999, "swap": None}
    assert module.annotate(row, unit, "journal")["scope_memory_peak_bytes"] == "4096"


def test_numeric_report_rows_keep_coverage_counts():
    result = memory.memory_statistic([{"scope_memory_peak_bytes": 4096.0,
                                     "scope_memory_peak_source": "cgroup-v2/memory.peak",
                                     "memory_cgroup": "/invocation"}],
                                   "scope_memory_peak_bytes")
    assert result["numerator"] == result["denominator"] == 1
    assert result["median_bytes"] == 4096


@pytest.mark.parametrize("retained_peak", ["", "99999"])
def test_failed_collector_cannot_become_complete_from_parent_journal(
        monkeypatch, tmp_path, retained_peak):
    state = tmp_path / "state"
    state.mkdir()
    def unavailable():
        raise OSError("memory controller delegation failed")
    monkeypatch.setattr(memory, "current_cgroup", unavailable)
    observer = memory.MemoryObserver(state)
    assert memory.own_invocation(state, ["solver"], "8G", "0") == 127
    observer.close()
    run = benchlib.RunResult("", "", 127, 0.1, False, **observer.snapshot)
    row = {**dict.fromkeys(memory.MEMORY_KEYS, ""), "result": "ERROR",
           "scope_unit": "acacia-fake.scope", **memory.memory_fields(run)}
    assert row["memory_cgroup"] == ""
    row["scope_memory_peak_bytes"] = retained_peak
    spec = importlib.util.spec_from_file_location(
        "failed_memory_annotate", ROOT / "benchmarking/annotate-scope-results.py")
    annotation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(annotation)
    unit = {"unit": "acacia-fake.scope", "oom": False, "cpu": 1,
            "memory": 99999, "swap": None}
    annotated = annotation.annotate(row, unit, "journal")
    assert annotated["scope_memory_peak_bytes"] == retained_peak
    assert annotated["parent_scope_memory_peak_bytes"] == "99999"
    statistic = memory.memory_statistic([annotated], "scope_memory_peak_bytes")
    assert (statistic["numerator"], statistic["denominator"]) == (0, 1)
    assert statistic["missing_reasons"] == {"memory controller delegation failed": 1}
    assert statistic["median_bytes"] is None
    assert statistic["max_bytes"] is None
    primary = tmp_path / "run.tsv"
    with primary.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, ["result", *memory.MEMORY_KEYS], delimiter="\t")
        writer.writeheader()
        writer.writerow({column: row[column] for column in writer.fieldnames})
    memory.write_memory_sidecar(primary, [row])
    journal = tmp_path / "journal.jsonl"
    journal.write_text(json.dumps({"USER_UNIT": "acacia-fake.scope", "MEMORY_PEAK": "99999",
                                   "CPU_USAGE_NSEC": "1", "__REALTIME_TIMESTAMP": "1000000"}) + "\n")
    output = tmp_path / "annotated.tsv"
    monkeypatch.setattr(sys, "argv", ["annotate", "--journal", str(journal),
                                      "--input", str(primary), "--output", str(output)])
    annotation.main()
    with output.open(newline="") as stream:
        cli_rows = list(csv.DictReader(stream, delimiter="\t"))
    memory.load_memory_sidecar(output, cli_rows)
    assert memory.memory_statistic(cli_rows, "scope_memory_peak_bytes") == statistic


@pytest.mark.parametrize("column,source,cgroup", [
    ("scope_memory_peak_bytes", "", "/invocation"),
    ("scope_memory_peak_bytes", "systemd journal", "/invocation"),
    ("scope_memory_peak_bytes", "cgroup-v2/memory.peak", ""),
    ("max_process_rss_bytes", "", ""),
    ("max_process_rss_bytes", "virtual reservation", ""),
])
def test_numeric_observations_without_valid_provenance_are_excluded(column, source, cgroup):
    row = {column: "4096", column.removesuffix("_bytes") + "_source": source,
           "memory_cgroup": cgroup}
    statistic = memory.memory_statistic([row], column)
    assert statistic["numerator"] == 0
    assert statistic["denominator"] == 1
    assert statistic["missing_reasons"] == {"missing or invalid collection provenance": 1}
    assert statistic["median_bytes"] is None


@pytest.mark.parametrize("column", ["scope_memory_peak_bytes", "max_process_rss_bytes"])
def test_explicit_missing_reason_overrides_numeric_observation_with_valid_provenance(column):
    row = {column: "4096", "scope_memory_peak_source": "cgroup-v2/memory.peak",
           "memory_cgroup": "/invocation", "max_process_rss_source": memory.RSS_SOURCE,
           column.removesuffix("_bytes") + "_missing_reason": "collection failed"}
    statistic = memory.memory_statistic([row], column)
    assert statistic["numerator"] == 0
    assert statistic["missing_reasons"] == {"collection failed": 1}
    assert statistic["median_bytes"] is None


@pytest.mark.parametrize("field", ["scope_unit", "binary_sha256", "scope_memory_peak_bytes"])
def test_sidecar_provenance_is_bound_to_the_primary_observation(tmp_path, field):
    path = tmp_path / "run.tsv"
    row = {**dict.fromkeys(memory.MEMORY_KEYS, ""),
           "scope_memory_peak_bytes": "4096", "scope_unit": "acacia-test.scope",
           "scope_memory_peak_source": "cgroup-v2/memory.peak", "memory_cgroup": "/invocation"}
    memory.write_memory_sidecar(path, [row])
    primary = {column: row[column] for column in memory.MEMORY_KEYS}
    memory.load_memory_sidecar(path, [primary])
    assert memory.memory_statistic([primary], "scope_memory_peak_bytes")["numerator"] == 1
    changed = {column: row[column] for column in memory.MEMORY_KEYS}
    changed[field] = "8192" if field.endswith("_bytes") else "different"
    memory.load_memory_sidecar(path, [changed])
    assert memory.memory_statistic([changed], "scope_memory_peak_bytes")["numerator"] == 0
