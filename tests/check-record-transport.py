#!/usr/bin/env python3
"""A full pipe loses telemetry explicitly; disk failures never block the solver."""

import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile


def rows(directory):
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines()]


def check_snapshots(records):
    """Independent subset oracle: unpublished payloads never become evidence."""
    boundaries = ("decline", "route_stopped", "verification_complete", "terminal_result", "exit")
    reaps = [r for r in records if r.get("event") == "worker_reap"]
    assert sorted(r["start_mono_ns"] for r in reaps) == list(range(1, 33)), reaps
    recovered = [r for r in records if r.get("event") == "worker_resource"]
    assert len(recovered) == 80, "I11: expected 5 * 16 committed boundaries"
    for reap in reaps:
        mask = reap["start_mono_ns"] - 1
        assert reap["signal"] == signal.SIGKILL and reap["exit_code"] == -1, (
            "I11: killed worker has no verdict", reap)
        assert reap["dropped_records"] == 1, ("I11: retain shared loss counter", reap)
        samples = [r for r in recovered if r["worker_pid"] == reap["worker_pid"]]
        expected = {name for i, name in enumerate(boundaries) if mask & (1 << i)}
        assert len(samples) == len(expected) and {r["boundary"] for r in samples} == expected, (
            "I11: recover exactly committed boundaries", mask, samples)
        for sample in samples:
            i = boundaries.index(sample["boundary"])
            assert sample["recovered"] and sample["dropped_records"] == 1, sample
            assert (sample["mono_ns"], sample["process_cpu_ns"]) == (100 + i, 200 + i), sample


def check_snapshot_controls(records):
    check_snapshots(records)
    empty = next(r for r in records if r.get("event") == "worker_reap"
                 and r["start_mono_ns"] == 1)
    sample = next(r for r in records if r.get("event") == "worker_resource")
    # Keep mutations of the observation in-suite as negative controls for the
    # same oracle used on production output, including fabricated completion.
    controls = {
        "uncommitted boundary": [dict(r, worker_pid=empty["worker_pid"], start_mono_ns=1)
                                 if r is sample else r for r in records],
        "missing committed boundary": [r for r in records if r is not sample],
        "lost loss counter": [dict(r, dropped_records=0) if r is empty else r for r in records],
        "killed worker verdict": [dict(r, exit_code=0, signal=0) if r is empty else r
                                  for r in records],
    }
    for name, mutated in controls.items():
        try:
            check_snapshots(mutated)
        except AssertionError:
            continue
        raise AssertionError(f"I11: oracle accepted mutation: {name}")


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=sys.argv[2]) as temporary:
        root = Path(temporary)
        directory = root / "snapshots"
        directory.mkdir()
        process = subprocess.run([str(binary)], input="s", text=True,
                                 env=dict(os.environ, ACACIA_PHASE_RECORDS=str(directory)),
                                 capture_output=True, timeout=5)
        assert (process.returncode, process.stdout, process.stderr) == (
            0, "32 reaped snapshots\n", ""), process
        check_snapshot_controls(rows(directory))
        print("I11: 32 killed snapshots, 80 committed boundaries, 4 oracle mutations rejected")
        for pipe in ("native", "512"):
            directory = root / pipe
            directory.mkdir()
            env = os.environ.copy()
            env["ACACIA_PHASE_RECORDS"] = str(directory)
            env["ACACIA_TEST_RECORD_DRAIN"] = "1"
            if pipe == "512":
                env["ACACIA_TEST_RECORD_PIPE_512"] = "1"
            process = subprocess.run([str(binary)], input="x", text=True, env=env,
                                     capture_output=True, timeout=3)
            assert process.returncode == 0, process
            records = rows(directory)
            assert any(row.get("event") == "parent_terminal"
                       and row["telemetry"] == "complete" for row in records), records
            assert all(row.get("dropped_records", 0) == 0 for row in records), records
            entry = next(row for row in records if row.get("event") == "stage_entry")
            end = next(row for row in records if row.get("event") == "stage_completion")
            metric = next(row for row in records if row.get("event") == "stage_metric")
            assert entry["subjob"] == 1 and entry["subjobs"] == 2 and entry["k"] == 3
            assert entry["stage_id"] == end["stage_id"] == metric["stage_id"]
            accounting = next(row for row in records if row.get("event") == "accounting_entry")
            assert accounting["phase_id"] == entry["stage_id"] and accounting["parent_phase_id"] == 0
            assert accounting["subjob"] == entry["subjob"] and accounting["run_id"] == entry["run_id"]
            assert metric["key"] == "states" and metric["value"] == "7"
        directory = root / "full"
        directory.mkdir()
        env.pop("ACACIA_TEST_RECORD_DRAIN")
        env["ACACIA_PHASE_RECORDS"] = str(directory)
        writer_read, writer_write = os.pipe()
        env["ACACIA_TEST_RECORD_WRITER_GATE_FD"] = str(writer_read)
        process = subprocess.Popen([str(binary)], text=True, env=env,
                                   pass_fds=(writer_read,),
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        os.close(writer_read)
        process.stdin.write("f")
        process.stdin.flush()
        assert process.stdout.readline() == "flooded\n"
        process.stdin.write("l")
        process.stdin.flush()
        assert process.stdout.readline() == "terminal_dropped\n"
        assert not rows(directory), "writer read before its release"
        os.close(writer_write)
        stdout, stderr = process.communicate("x", timeout=3)
        assert process.returncode == 0 and not stderr, (stdout, stderr)
        records = rows(directory)
        terminal = next(row for row in records if row.get("event") == "parent_terminal")
        assert terminal["telemetry"] == "dropped" and terminal["dropped_records"] > 0
        assert any(row.get("phase") == "flood" for row in records), records
        summary, = [row for row in records if row.get("event") == "writer_summary"]
        assert summary["failed_records"] == 0 and not summary["incomplete_packet"], summary
        assert summary["delivered_records"] == len(records) - 1, summary
        for mode in ("fifo", "full_disk", "broken", "slow"):
            directory = root / mode
            directory.mkdir()
            env = os.environ.copy()
            env["ACACIA_PHASE_RECORDS"] = str(directory)
            def file_limit():
                resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
            if mode == "slow":
                env["ACACIA_TEST_RECORD_WRITER_STALL"] = "1"
            process = subprocess.Popen([str(binary)], text=True, env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       preexec_fn=file_limit if mode == "full_disk" else None)
            if mode in ("fifo", "broken"):
                if mode == "fifo":
                    os.mkfifo(directory / f"{process.pid}.jsonl")
                else:
                    (directory / f"{process.pid}.jsonl").symlink_to("/dev/full")
            stdout, stderr = process.communicate("x", timeout=3)
            assert process.returncode == 0 and not stderr, (mode, stdout, stderr)
            # Do not read FIFO destinations. An unconfirmed writer summary is
            # incomplete delivery; it says nothing about algorithmic declines.
            if mode in ("fifo", "broken"):
                (directory / f"{process.pid}.jsonl").unlink()
                summary = next(row for row in rows(directory)
                               if row.get("event") == "writer_summary")
                assert summary["failed_records"] > 0, summary
            if mode == "slow":
                summaries = rows(directory)
                assert len(summaries) == 1 and summaries[0]['event'] == 'parent_record_summary', summaries
                assert not any(r.get('event') == 'writer_summary' for r in summaries)
    print("record transport, 512-byte packets, and destination failures passed")


if __name__ == "__main__":
    main()
