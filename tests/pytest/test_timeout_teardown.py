"""Exercise runner, owner and checked solver together without real cgroups/systemd."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import signal
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
import benchlib  # noqa: E402


@pytest.mark.skipif(sys.platform != "linux", reason="Linux invocation owner uses wait4/cgroups")
def test_timeout_row_retains_every_terminal_and_both_summaries(monkeypatch, tmp_path):
    build = os.environ.get("ACACIA_ATTRIBUTION_TEST_BUILD")
    if not build:
        pytest.skip("set ACACIA_ATTRIBUTION_TEST_BUILD to a checked solver build")
    binary = Path(build) / "tests/game-backend-integration"
    assert binary.is_file(), binary
    spec = importlib.util.spec_from_file_location(
        "timeout_coverage", ROOT / "benchmarking/run-syntcomp26-coverage.py")
    coverage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(coverage)
    source = tmp_path / "source"
    source.mkdir()
    (source / "control.tlsf").write_text(
        'INFO { TITLE: "control" SEMANTICS: Mealy TARGET: Mealy }\n'
        'MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G o; } }\n')
    selection = tmp_path / "control.list"
    selection.write_text("control.ltl\n")
    mapping = tmp_path / "map.tsv"
    mapping.write_text("instance\ttlsf\ncontrol.ltl\tcontrol.tlsf\n")
    records = tmp_path / "records"
    exceptions = tmp_path / "exceptions.tsv"
    with exceptions.open("w") as stream:
        csv.DictWriter(stream, ["instance", "annotated_status", "corrected_status", "evidence"], delimiter="\t").writeheader()
    monkeypatch.setenv("ACACIA_TEST_CHILD_MODES", "stall,stall,stall,stall,stall")
    monkeypatch.setenv("ACACIA_OUTER_DEADLINE_MONOTONIC", str(benchlib.time.monotonic() + 10))
    args = coverage.build_parser().parse_args([
        "--bin", str(binary), "--solver-label", "fixture", "--list", str(selection),
        "--tlsf-map", str(mapping), "--tlsf-corpus", str(source), "--caps", "1",
        "--output", str(tmp_path / "rows.tsv"), "--acacia-sha", "fixture",
        "--phase-records-dir", str(records), "--collect-rusage",
        "--memory-max", "8G", "--memory-swap-max", "0",
        "--status-exceptions", str(exceptions),
    ])
    popen = benchlib.subprocess.Popen
    launches = []
    def launch(command, **kwargs):
        assert command[0] == "systemd-run"
        start = command.index(str(ROOT / "benchmarking/scope_memory.py"))
        fixture = ROOT / "tests/fixtures/scope_memory_owner.py"
        launches.append(command)
        return popen([sys.executable, str(fixture), *command[start + 1:]], **kwargs)
    monkeypatch.setattr(benchlib.subprocess, "Popen", launch)
    monkeypatch.setattr(benchlib, "_stop_user_scope", lambda unit: None)
    assert coverage.run(args) == 0
    assert len(launches) == 1 and "/usr/bin/time" not in launches[0]
    with (tmp_path / "rows.tsv").open() as stream:
        row, = csv.DictReader(stream, delimiter="\t")
    assert (row["result"], row["timed_out"], row["exit_code"]) == ("TIMEOUT", "true", "124")
    assert float(row["seconds"]) >= 1
    assert row["cpu_seconds"] and row["scope_memory_peak_bytes"] == "4096"
    events = [dict(json.loads(line), emitter_pid=int(path.stem))
              for path in records.rglob("*.jsonl") for line in path.read_text().splitlines()]
    spawned = [event for event in events if event.get("event") == "worker_spawn"]
    terminals = [event for event in events if event.get("event") == "parent_terminal"]
    assert len(spawned) == len(terminals) == 5
    assert {(event["worker"], event["worker_pid"]) for event in spawned} == {
        (event["worker"], event["worker_pid"]) for event in terminals}
    assert all(event["signal"] == signal.SIGKILL and event["reason"] == "interrupted"
               and event["telemetry"] == "incomplete" for event in terminals)
    parent_pid = terminals[0]["emitter_pid"]
    assert any(event.get("phase") == "record_summary" and event["emitter_pid"] == parent_pid
               for event in events)
    writer, = [event for event in events if event.get("event") == "writer_summary"]
    assert writer["failed_records"] == 0 and not writer["incomplete_packet"]
    assert writer["delivered_records"] == len(events) - 1
    assert all(event.get("dropped_records", 0) == 0 for event in events)
    assert not any(event.get("event") == "parent_winner" for event in events)
