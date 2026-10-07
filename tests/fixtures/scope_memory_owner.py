"""Test-only cgroup filesystem stand-in running the real invocation lifecycle owner."""
import argparse
import json
import os
from pathlib import Path
import signal
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "benchmarking"))
import scope_memory as memory  # noqa: E402


def descendants(pid):
    try:
        children = Path(f"/proc/{pid}/task/{pid}/children").read_text().split()
    except OSError:
        children = []
    return [child for value in children for child in descendants(int(value))] + [pid]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--memory-max", required=True)
    parser.add_argument("--swap-max", required=True)
    parser.add_argument("--oom-group", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    base = args.state / "fake-scope"
    base.mkdir()
    memory.current_cgroup = lambda: base
    mkdir, write, remove = Path.mkdir, Path.write_text, Path.rmdir
    def create(path, *a, **kw):
        mkdir(path, *a, **kw)
        if path.name == "invocation":
            for name, value in {"cgroup.events": "populated 0\n", "memory.peak": "4096",
                                "memory.events": "oom 0\noom_kill 0\n"}.items():
                write(path / name, value)
    def kill_scope(path, value, *a, **kw):
        if path.name == "cgroup.kill":
            started = json.loads((args.state / "started.json").read_text())
            for pid in descendants(started["pid"]):
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        return write(path, value, *a, **kw)
    def delete(path):
        if path.name == "invocation":
            assert (args.state / "ack").exists(), "scope deleted before memory collection"
            for file in path.iterdir():
                file.unlink()
        remove(path)
    Path.mkdir, Path.write_text, Path.rmdir = create, kill_scope, delete
    signal.signal(signal.SIGTERM, lambda *unused: memory.request_cancel(args.state))
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    return memory.own_invocation(args.state, command, args.memory_max, args.swap_max,
                                 oom_group=args.oom_group)


if __name__ == "__main__":
    raise SystemExit(main())
