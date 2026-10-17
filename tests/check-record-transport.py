#!/usr/bin/env python3
"""A full pipe loses telemetry explicitly; disk failures never block the solver."""

import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile


def rows(directory):
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines()]


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=sys.argv[2]) as temporary:
        root = Path(temporary)
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
