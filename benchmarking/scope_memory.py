"""Retain a delegated invocation cgroup until its external observer acknowledges it."""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import pathlib
import re
import signal
import sys
import threading
import time
from collections import Counter


MEMORY_COLUMNS = ["scope_memory_peak_source", "scope_memory_peak_missing_reason",
                  "scope_memory_events", "scope_memory_events_source", "scope_memory_events_missing_reason",
                  "memory_cgroup", "memory_oom_group", "max_process_rss_source", "max_process_rss_missing_reason"]
MEMORY_KEYS = ["solver_label", "instance", "cap_s", "run_index", "binary_sha256", "scope_unit",
               "seconds", "exit_code", "scope_memory_peak_bytes", "max_process_rss_bytes"]
RSS_SOURCE = "Linux wait4.ru_maxrss (maximum reaped process; not scope peak)"
TIME_RSS_SOURCE = "/usr/bin/time.ru_maxrss (not scope peak)"


def memory_sidecar(path):
    return pathlib.Path(str(path) + ".memory.tsv")


def write_memory_sidecar(path, rows):
    path = memory_sidecar(path)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, MEMORY_KEYS + MEMORY_COLUMNS, delimiter="\t",
                                    lineterminator="\n")
            writer.writeheader()
            writer.writerows({column: row.get(column, "") for column in writer.fieldnames}
                             for row in rows)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def append_memory_sidecar(path, row):
    with memory_sidecar(path).open("a", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, MEMORY_KEYS + MEMORY_COLUMNS, delimiter="\t",
                                lineterminator="\n")
        writer.writerow({column: row.get(column, "") for column in writer.fieldnames})
        stream.flush()
        os.fsync(stream.fileno())


def load_memory_sidecar(path, rows):
    path = memory_sidecar(path)
    if not path.exists():
        return
    observations = {}
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != MEMORY_KEYS + MEMORY_COLUMNS:
            raise ValueError(f"{path}: unexpected memory sidecar header")
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"{path}: malformed memory sidecar row")
            key = tuple(row[column] for column in MEMORY_KEYS)
            if key in observations:
                raise ValueError(f"{path}: duplicate memory sidecar observation")
            observations[key] = row
    for row in rows:
        key = tuple(row.get(column, "") for column in MEMORY_KEYS)
        observation = observations.get(key)
        if observation is not None:
            row.update({column: observation[column] for column in MEMORY_COLUMNS})


def memory_fields(run):
    peak = getattr(run, "memory_peak_bytes", None)
    rss = getattr(run, "max_process_rss_bytes", None)
    return {
        "scope_memory_peak_bytes": "" if peak is None else str(peak),
        "max_process_rss_bytes": "" if rss is None else str(rss),
        **{column: getattr(run, column, "") for column in MEMORY_COLUMNS},
        "scope_memory_peak_missing_reason": getattr(run, "scope_memory_peak_missing_reason", "")
        or ("collection provenance absent" if peak is None else ""),
        "max_process_rss_missing_reason": getattr(run, "max_process_rss_missing_reason", "")
        or ("collection provenance absent" if rss is None else ""),
    }


def cgroup_empty(cgroup):
    events = dict(line.split() for line in (cgroup / "cgroup.events").read_text().splitlines())
    if events.get("populated") not in {"0", "1"}:
        raise ValueError("missing or invalid populated state")
    return events["populated"] == "0"


def incomplete_memory(result, reason):
    result.update(memory_peak_bytes=None, scope_memory_events="",
                  scope_memory_peak_missing_reason=reason,
                  scope_memory_events_missing_reason=reason)
    return result


def read_memory(cgroup):
    result = {"memory_peak_bytes": None, "scope_memory_peak_source": "cgroup-v2/memory.peak",
              "scope_memory_peak_missing_reason": "", "scope_memory_events": "",
              "scope_memory_events_missing_reason": "",
              "scope_memory_events_source": "cgroup-v2/memory.events", "memory_cgroup": str(cgroup)}
    try:
        if not cgroup_empty(cgroup):
            raise RuntimeError("workload cgroup did not empty")
    except (OSError, ValueError, RuntimeError) as error:
        return incomplete_memory(result, f"cgroup.events: {error}")
    try:
        peak = int((cgroup / "memory.peak").read_text().strip())
        if peak <= 0 or peak >= (1 << 64) - 1:
            raise ValueError("nonpositive or sentinel peak")
        result["memory_peak_bytes"] = peak
    except (OSError, ValueError) as error:
        result["scope_memory_peak_missing_reason"] = f"memory.peak: {error}"
    try:
        events = dict(line.split() for line in (cgroup / "memory.events").read_text().splitlines())
        events = {key: int(value) for key, value in events.items()}
        if any(value < 0 for value in events.values()) or not {"oom", "oom_kill"} <= events.keys():
            raise ValueError("incomplete or negative memory.events")
        result["scope_memory_events"] = json.dumps(events, sort_keys=True)
    except (OSError, ValueError) as error:
        result["scope_memory_events_missing_reason"] = f"memory.events: {error}"
    # Both files must be read after the final drain, while the retained cgroup is empty.
    try:
        if not cgroup_empty(cgroup):
            raise RuntimeError("workload cgroup did not empty")
    except (OSError, ValueError, RuntimeError) as error:
        return incomplete_memory(result, f"cgroup.events: {error}")
    return result


def memory_observation(row, column):
    prefix = column.removesuffix("_bytes")
    reason = row.get(prefix + "_missing_reason", "")
    if reason:
        return None, reason
    source = row.get(prefix + "_source", "")
    if column == "scope_memory_peak_bytes":
        provenance = source == "cgroup-v2/memory.peak" and bool(row.get("memory_cgroup"))
    else:
        provenance = source in {RSS_SOURCE, TIME_RSS_SOURCE}
    if not provenance:
        return None, "missing or invalid collection provenance"
    value = row.get(column, "")
    valid = (str(value).isdigit() or isinstance(value, float)
             and math.isfinite(value) and value.is_integer())
    if not valid or not 0 < int(value) < (1 << 64) - 1:
        return None, "missing or invalid memory observation"
    return int(value), ""


def memory_statistic(rows, column):
    import statistics
    values, reasons = [], Counter()
    for row in rows:
        value, reason = memory_observation(row, column)
        if value is not None:
            values.append(value)
        else:
            reasons[reason] += 1
    complete = bool(rows) and len(values) == len(rows)
    return {"numerator": len(values), "denominator": len(rows),
            "missing_reasons": dict(reasons),
            "denominator_basis": "all campaign rows, including nonexecuted conversions",
            "max_bytes": max(values) if values else None,
            # Global coverage rule: all rows are required for a cohort median.
            "median_bytes": statistics.median(values) if complete else None,
            "selection_bias": ("observed subset may select by outcome, including MEMOUT; "
                               "median suppressed" if not complete else "none from missing samples")}


def atomic_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value))
    temporary.replace(path)


class MemoryObserver:
    """Runs in the benchmark driver, outside both owner and workload cgroups."""
    def __init__(self, state):
        self.state = state
        self.finished = threading.Event()
        self.thread = threading.Thread(target=self._observe, daemon=True)
        self.snapshot = {"memory_peak_bytes": None,
                         "scope_memory_peak_missing_reason": "owner did not publish final state",
                         "scope_memory_events_missing_reason": "owner did not publish final state",
                         "max_process_rss_missing_reason": "owner did not publish wait4 result"}
        self.thread.start()

    def _observe(self):
        while not self.finished.is_set():
            try:
                ready = json.loads((self.state / "ready.json").read_text())
            except (OSError, ValueError):
                self.finished.wait(0.01)
                continue
            self.collect(ready)
            return

    def collect(self, ready):
        # The owner cannot remove the cgroup until this read is acknowledged.
        if ready.get("cgroup"):
            self.snapshot = read_memory(pathlib.Path(ready["cgroup"]))
        if ready.get("error"):
            incomplete_memory(self.snapshot, ready["error"])
        rss = ready.get("max_process_rss_bytes")
        self.snapshot.update(
            memory_oom_group=ready.get("oom_group", ""),
            max_process_rss_bytes=rss if isinstance(rss, int) and rss > 0 else None,
            max_process_rss_source=RSS_SOURCE,
            max_process_rss_missing_reason="" if isinstance(rss, int) and rss > 0
            else ready.get("error", "wait4 RSS unavailable"))
        returncode = ready.get("workload_returncode")
        if isinstance(returncode, int):
            self.snapshot["workload_returncode"] = returncode
        (self.state / "ack").touch()
        self.finished.set()

    def cancel(self):
        (self.state / "cancel").touch()
        self.finished.wait(3)

    def close(self):
        self.finished.set()
        self.thread.join(timeout=1)


def memory_limit(value):
    if str(value) in {"infinity", "max"}:
        return "max"
    match = re.fullmatch(r"([0-9]+)([KMGT]?)", str(value), re.I)
    if not match:
        raise ValueError(f"unsupported memory limit {value!r}")
    return str(int(match[1]) * 1024 ** ("KMGT".index(match[2].upper()) + 1 if match[2] else 0))


def current_cgroup(root=pathlib.Path("/sys/fs/cgroup"), proc=pathlib.Path("/proc/self/cgroup")):
    for line in proc.read_text().splitlines():
        if line.startswith("0::/"):
            return root / line[4:]
    raise RuntimeError("cgroup v2 membership unavailable")


def own_invocation(state, cmd, memory_max, swap_max, *, oom_group=False):
    cgroup = None
    pid = None
    status = None
    outcome = {"oom_group": str(oom_group).lower()}
    try:
        base = current_cgroup()
        # A delegated scope initially has one member: this owner. Move it into
        # a sibling before enabling the memory controller (v2 no-internal-process rule).
        owner = base / "observer"
        owner.mkdir()
        (owner / "cgroup.procs").write_text(str(os.getpid()))
        (base / "cgroup.subtree_control").write_text("+memory")
        cgroup = base / "invocation"
        cgroup.mkdir()
        (cgroup / "memory.max").write_text(memory_limit(memory_max))
        (cgroup / "memory.swap.max").write_text(memory_limit(swap_max))
        (cgroup / "memory.oom.group").write_text("1" if oom_group else "0")
        pid = os.fork()
        if pid == 0:
            try:
                (cgroup / "cgroup.procs").write_text(str(os.getpid()))
                os.execvpe(cmd[0], cmd, os.environ)
            except BaseException as error:
                print(f"workload launch failed: {error}", file=sys.stderr)
                os._exit(127)
        atomic_json(state / "started.json", {"cgroup": str(cgroup), "pid": pid})
        while True:
            found, wait_status, usage = os.wait4(pid, os.WNOHANG)
            if found:
                status = wait_status
                outcome["max_process_rss_bytes"] = int(usage.ru_maxrss) * 1024
                break
            if (state / "cancel").exists():
                (cgroup / "cgroup.kill").write_text("1")
            time.sleep(0.01)
        # Reclaim descendants before the final peak/events read, but retain the
        # empty directory and the owner until the external reader acknowledges.
        (cgroup / "cgroup.kill").write_text("1")
        deadline = time.monotonic() + 3
        while not cgroup_empty(cgroup):
            if time.monotonic() >= deadline:
                raise RuntimeError("workload cgroup did not empty")
            time.sleep(0.01)
    except (OSError, ValueError, RuntimeError) as error:
        outcome["error"] = str(error)
        if cgroup is not None:
            try:
                (cgroup / "cgroup.kill").write_text("1")
            except OSError:
                pass
        if pid and status is None:
            _, status, usage = os.wait4(pid, 0)
            outcome["max_process_rss_bytes"] = int(usage.ru_maxrss) * 1024
    finally:
        outcome["workload_returncode"] = (os.waitstatus_to_exitcode(status)
                                          if status is not None else None)
        outcome["cgroup"] = str(cgroup) if cgroup is not None else ""
        atomic_json(state / "ready.json", outcome)
        deadline = time.monotonic() + 30
        while not (state / "ack").exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        if cgroup is not None:
            try:
                cgroup.rmdir()
            except OSError:
                pass
    if status is None:
        return 127
    code = os.waitstatus_to_exitcode(status)
    return code if code >= 0 else 128 - code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", required=True, type=pathlib.Path)
    parser.add_argument("--memory-max", required=True)
    parser.add_argument("--swap-max", required=True)
    parser.add_argument("--oom-group", action="store_true", help="kill the whole invocation on OOM")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, lambda *unused: (args.state / "cancel").touch())
    cmd = args.command[1:] if args.command[:1] == ["--"] else args.command
    return own_invocation(args.state, cmd, args.memory_max, args.swap_max,
                          oom_group=args.oom_group)


if __name__ == "__main__":
    raise SystemExit(main())
