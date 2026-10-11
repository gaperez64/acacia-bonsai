"""Attribution must distinguish RSS samples, terminal events, and scope OOM."""
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
spec = importlib.util.spec_from_file_location(
    "diagnose_portfolio", ROOT / "benchmarking/diagnose-portfolio.py")
diag = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diag)
from benchlib import RunResult  # noqa: E402


def test_terminal_and_decline_are_different_times():
    events = [
        {"worker": 0, "event": "worker_start", "mono_ns": 1_000_000_000},
        {"worker": 0, "event": "decline", "mono_ns": 2_000_000_000,
         "reason": "unsupported", "route": "R"},
        {"worker": 0, "event": "parent_terminal", "mono_ns": 4_000_000_000,
         "worker_pid": 42, "exit_code": 2, "signal": 0, "reason": "exit",
         "route": "direct", "telemetry": "complete"},
    ]
    result = diag.arm_summary(events, [], ["both:gr1-real-lift:oxidd"])[0]
    assert result["exit_s"] == 3
    assert result["declines"][0]["seconds"] == 1
    assert result["sampled_peak_rss_bytes"] is None
    assert result["rss_sample_count"] == 0
    assert result["route"] == "direct"


def test_resource_end_uses_scope_events_and_parent_winner():
    oom = RunResult("", "", -9, 1, False, scope_memory_events='{"oom_kill": 1}')
    assert diag.ending_resource(oom, []) == "cgroup memory (OOM kill)"
    timeout = RunResult("", "", 124, 60, True)
    assert diag.ending_resource(timeout, []) == "wall deadline"
    answer = RunResult("REALIZABLE\n", "", 0, 1, False)
    assert diag.ending_resource(answer, [{"event": "parent_winner"}]) == "verified answer"
    assert diag.ending_resource(answer, [{"event": "route_selected"}]) != "verified answer"


def test_sampler_parses_comm_and_descendants(tmp_path):
    def process(pid, name, child):
        base = tmp_path / str(pid)
        task = base / "task" / str(pid)
        task.mkdir(parents=True)
        # Field 3 onward: state, ppid, pgrp, ..., utime(14), stime(15),
        # starttime(22), vsize(23), rss(24).
        fields = ["S"] + ["0"] * 21
        fields[11:13] = ["13", "7"]
        fields[19] = "999"
        fields[21] = "2"
        (base / "stat").write_text(f"{pid} ({name}) " + " ".join(fields))
        (task / "children").write_text(child)
    process(42, "name ) with spaces", "43")
    process(43, "tool", "")
    rows = diag.process_tree(42, tmp_path)
    assert {r["pid"] for r in rows} == {42, 43}
    assert all(r["cpu_ticks"] == 20 for r in rows)
    assert all(r["start_ticks"] == 999 for r in rows)
    assert diag.process_tree(44, tmp_path) == []


def test_phase_tail_keeps_partial_record(tmp_path):
    observer = diag.ArmObserver(tmp_path, 0.1)
    path = tmp_path / "42.jsonl"
    line = json.dumps({"event": "worker_start", "worker": 0, "worker_pid": 42})
    path.write_text(line[:15])
    observer.read_events()
    assert observer.events == []
    path.write_text(line + "\n")
    observer.read_events()
    observer.read_events()
    assert len(observer.events) == 1
    assert observer.workers == {0: 42}


def test_selection_prioritizes_losses_from_joined_rows(tmp_path):
    header = "instance\ttlsf_file\tresult\tseconds\tflags\tsolver_label\n"
    race = tmp_path / "race.tsv"
    race.write_text(header + "a\ta.tlsf\tTIMEOUT\t60\t\trace\n"
                    + "b\tb.tlsf\tREALIZABLE\t50\t\trace\n")
    solo = tmp_path / "solo.tsv"
    solo.write_text(header + "a\ta.tlsf\tREALIZABLE\t2\t--arms real:small:backward\tarm\n"
                    + "b\tb.tlsf\tREALIZABLE\t10\t--arms real:small:backward\tarm\n"
                    + "c\tc.tlsf\tREALIZABLE\t59\t--arms real:small:backward\tarm\n")
    rows = diag.select_candidates(race, [solo])
    assert [row["instance"] for row in rows] == ["a", "b"]
    assert rows[0]["lost_at_cap"]


def test_duplicate_race_rows_are_not_silently_overwritten(tmp_path):
    import pytest
    race = tmp_path / "race.tsv"
    race.write_text("instance\tresult\ninput\tUNKNOWN\ninput\tTIMEOUT\n")
    with pytest.raises(ValueError, match="one observation"):
        diag.select_candidates(race, [])


def test_observed_membership_rejects_wrong_binary_config():
    import pytest
    events = [
        {"event": "worker_spec", "worker": 0, "kind": "legacy",
         "requested_polarity": "REAL", "translation": "small", "transform": "real"},
        {"event": "worker_spawn", "worker": 0, "requested_backend": "backward"},
    ]
    diag.validate_membership(events, ["real:small:backward"])
    with pytest.raises(RuntimeError, match="differs"):
        diag.validate_membership(events, ["real:small:forward"])
    with pytest.raises(RuntimeError, match="membership"):
        diag.validate_membership([], ["real:small:backward"])


def test_phase_tail_preserves_partial_multibyte_text(tmp_path):
    observer = diag.ArmObserver(tmp_path, 0.1)
    path = tmp_path / "42.jsonl"
    line = json.dumps({"event": "decline", "worker": 0, "reason": "\u00e9"},
                      ensure_ascii=False).encode()
    path.write_bytes(line[:-4])
    observer.read_events()
    path.write_bytes(line + b"\n")
    observer.read_events()
    assert observer.events[0]["reason"] == "\u00e9"
    assert observer.errors == []
