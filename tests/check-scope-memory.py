#!/usr/bin/env python3
"""Real Linux lifecycle validation panel; run outside the Codex sandbox."""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "benchmarking"))
from benchlib import run_systemd_scope  # noqa: E402
from scope_memory import memory_fields  # noqa: E402

ALLOCATE = "blocks=[]\nwhile True: blocks.append(bytearray(4*1024*1024))"
CHILD = ("import os\npid=os.fork()\nif pid == 0:\n "
         + ALLOCATE.replace("\n", "\n ") + "\nos.waitpid(pid, 0)")


def main():
    rows = []
    for name, body, timeout, group in (
        ("normal", "data=bytearray(8*1024*1024)", 10, False),
        ("timeout", "import time; data=bytearray(8*1024*1024); time.sleep(30)", 1, False),
        ("child-oom", CHILD, 10, False),
        ("invocation-oom", ALLOCATE, 10, True),
    ):
        run = run_systemd_scope([sys.executable, "-c", body], timeout, "64M",
                               unit_prefix="acacia-memory-validation", oom_group=group)
        row = {"case": name, "exit": run.returncode, "timed_out": run.timed_out,
               "resource_limited": run.resource_limited, **memory_fields(run)}
        rows.append(row)
        print(json.dumps(row), flush=True)
        assert run.memory_peak_bytes and not run.scope_memory_peak_missing_reason, row
        assert run.max_process_rss_bytes or run.max_process_rss_missing_reason, row
        events = json.loads(run.scope_memory_events)
        assert run.timed_out == (name == "timeout"), row
        assert (events["oom_kill"] > 0) == ("oom" in name), row
        if name == "normal":
            assert run.returncode == 0, row
        if name == "invocation-oom":
            assert run.resource_limited and events.get("oom_group_kill", 0) > 0, row
    assert len({row["memory_cgroup"] for row in rows}) == 4
    print("PASS: four fresh invocation cgroups; peak/events collected before deletion")


if __name__ == "__main__":
    main()
