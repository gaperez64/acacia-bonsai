#!/usr/bin/env python3
"""A full pipe loses telemetry explicitly; disk failures never block the solver."""

import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time


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
            if pipe == "512":
                env["ACACIA_TEST_RECORD_PIPE_512"] = "1"
            process = subprocess.run([str(binary)], input="x", text=True, env=env,
                                     capture_output=True, timeout=3)
            assert process.returncode == 0, process
            records = rows(directory)
            assert any(row.get("event") == "parent_terminal"
                       and row["telemetry"] == "complete" for row in records), records
            assert all(row.get("dropped_records", 0) == 0 for row in records), records
        directory = root / "full"
        directory.mkdir()
        env["ACACIA_PHASE_RECORDS"] = str(directory)
        env["ACACIA_TEST_RECORD_WRITER_DELAY"] = "1"
        process = subprocess.Popen([str(binary)], text=True, env=env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        process.stdin.write("f")
        process.stdin.flush()
        assert process.stdout.readline() == "flooded\n"
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if any(row.get("phase") == "flood" for row in rows(directory)):
                break
            time.sleep(.005)
        else:
            raise AssertionError("writer did not recover")
        stdout, stderr = process.communicate("x", timeout=3)
        assert process.returncode == 0 and not stderr, (stdout, stderr)
        records = rows(directory)
        terminal = next(row for row in records if row.get("event") == "parent_terminal")
        assert terminal["telemetry"] == "dropped" and terminal["dropped_records"] > 0
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
                assert not list(directory.iterdir())
    print("record transport, 512-byte packets, and destination failures passed")


if __name__ == "__main__":
    main()
