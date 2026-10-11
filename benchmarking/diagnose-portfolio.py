#!/usr/bin/env python3
"""Attribute one input's race and isolated arms; never schedule a corpus campaign.

Reuse benchlib's scope owner so memory.peak/events survive workload OOM. The
observer stays outside the workload and samples process trees from worker PIDs.
RSS sums include shared pages repeatedly; only the cgroup peak is scope memory.
"""
from __future__ import annotations

import argparse
import csv
import dataclasses
import fcntl
import glob
import hashlib
import json
import os
import pathlib
import threading
import time

from benchlib import SOLVED, check_campaign_scopes, classify_run, run_systemd_scope

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def select_candidates(race_path, arm_paths):
    """Join recorded identity, then rank resource/CPU risk without solver dispatch."""
    race_rows = read_rows(race_path)
    race = {row["instance"]: row for row in race_rows}
    if len(race) != len(race_rows):
        raise ValueError("race rows must contain one observation per input")
    solos = {}
    for path in arm_paths:
        for row in read_rows(path):
            if row["result"] in SOLVED and row["instance"] in race:
                solos.setdefault(row["instance"], []).append(row)
    candidates = []
    for identity, rows in solos.items():
        old = race[identity]
        best = min(rows, key=lambda r: float(r["seconds"]))
        solo = float(best["seconds"])
        wall = float(old["seconds"])
        candidates.append({"instance": identity, "tlsf_file": old["tlsf_file"],
                           "race_result": old["result"], "race_seconds": wall,
                           "solo_seconds": solo, "solo_arm": best["flags"],
                           "solo_source": best["solver_label"],
                           "race_peak_bytes": old.get("scope_memory_peak_bytes", ""),
                           "lost_at_cap": old["result"] not in SOLVED,
                           "slowdown": wall / max(solo, 1e-9)})
    return sorted(candidates, key=lambda r: (r["lost_at_cap"],
                                           max(r["solo_seconds"], r["race_seconds"]),
                                           r["slowdown"]), reverse=True)


def process_tree(pid, proc=pathlib.Path("/proc")):
    """Read cumulative CPU, RSS and thread count, including tool descendants."""
    pending = [pid]
    seen = set()
    result = []
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        try:
            base = proc / str(current)
            # comm can contain spaces and ')'; fields follow its last ')'.
            fields = (base / "stat").read_text().rsplit(")", 1)[1].split()
            tasks = list((base / "task").iterdir())
            for task in tasks:
                try:
                    pending.extend(map(int, (task / "children").read_text().split()))
                except OSError:
                    pass
            result.append({"pid": current, "start_ticks": int(fields[19]),
                           "cpu_ticks": int(fields[11]) + int(fields[12]),
                           "rss_bytes": max(0, int(fields[21])) * os.sysconf("SC_PAGE_SIZE"),
                           "threads": len(tasks), "state": fields[0],
                           "processor": int(fields[36]) if len(fields) > 36 else None})
        except (OSError, ValueError, IndexError):
            continue
    return result


def host_snapshot():
    result = {"loadavg": pathlib.Path("/proc/loadavg").read_text().strip()}
    for pattern, label in (("/sys/class/thermal/thermal_zone*/temp", "temperature_mC"),
                           ("/sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq",
                            "frequency_kHz")):
        values = {}
        for name in glob.glob(pattern):
            try:
                values[name] = int(pathlib.Path(name).read_text())
            except (OSError, ValueError):
                pass
        result[label] = values
    return result


class ArmObserver:
    def __init__(self, directory, interval):
        self.directory = directory
        self.interval = interval
        self.stop = threading.Event()
        self.events = []
        self.workers = {}
        self.offsets = {}
        self.cpu = {}
        self.samples = []
        self.errors = []
        self.constraints = {}
        self.started_ns = time.monotonic_ns()
        self.thread = threading.Thread(target=self.observe, daemon=True)

    def read_events(self):
        for path in self.directory.glob("*.jsonl"):
            with path.open("rb") as stream:
                stream.seek(self.offsets.get(path, 0))
                while line := stream.readline():
                    if not line.endswith(b"\n"):
                        stream.seek(stream.tell() - len(line))
                        break
                    try:
                        record = json.loads(line)
                    except ValueError:
                        self.errors.append("invalid phase record")
                        continue
                    record["record_pid"] = int(path.stem)
                    self.events.append(record)
                    if record.get("event") in {"worker_spawn", "worker_start", "worker_spec"}:
                        self.workers[record["worker"]] = record["worker_pid"]
                self.offsets[path] = stream.tell()

    def sample(self):
        now = time.monotonic_ns()
        trees = {worker: process_tree(pid) for worker, pid in self.workers.items()}
        delta = {}
        for worker, tree in trees.items():
            ticks = 0
            for process in tree:
                key = (worker, process["pid"], process["start_ticks"])
                previous = self.cpu.get(key, 0)
                ticks += max(0, process["cpu_ticks"] - previous)
                self.cpu[key] = process["cpu_ticks"]
            delta[worker] = ticks
        total = sum(delta.values())
        if not self.constraints:
            for pid in self.workers.values():
                try:
                    membership = pathlib.Path(f"/proc/{pid}/cgroup").read_text()
                    group = next(line[3:] for line in membership.splitlines()
                                 if line.startswith("0::"))
                    base = pathlib.Path("/sys/fs/cgroup") / group.lstrip("/")
                    self.constraints = {key: (base / key).read_text().strip()
                                        for key in ("memory.max", "memory.swap.max", "cpu.max",
                                                    "cpuset.cpus.effective")
                                        if (base / key).exists()}
                    hierarchy = {}
                    for ancestor in (base, *base.parents):
                        if not ancestor.is_relative_to("/sys/fs/cgroup"):
                            break
                        limits = {key: (ancestor / key).read_text().strip()
                                  for key in ("cpu.max", "cpuset.cpus.effective")
                                  if (ancestor / key).exists()}
                        if limits:
                            hierarchy[str(ancestor)] = limits
                    self.constraints["cpu_hierarchy"] = hierarchy
                    break
                except (OSError, StopIteration):
                    continue
        for worker, tree in trees.items():
            if not tree:
                continue
            self.samples.append({"mono_ns": now, "elapsed_s": (now-self.started_ns)/1e9,
                                 "worker": worker, "rss_bytes": sum(p["rss_bytes"] for p in tree),
                                 "cpu_ticks_delta": delta[worker],
                                 "cpu_share": delta[worker]/total if total else None,
                                 "threads": sum(p["threads"] for p in tree), "processes": tree})

    def observe(self):
        try:
            while not self.stop.is_set():
                self.read_events()
                self.sample()
                self.stop.wait(self.interval)
            self.read_events()
        except Exception as error:
            self.errors.append(str(error))

    def finish(self):
        self.stop.set()
        self.thread.join()
        return self.events, self.samples


def arm_summary(events, samples, arms):
    result = []
    hz = os.sysconf("SC_CLK_TCK")
    for index, arm in enumerate(arms):
        records = [e for e in events if e.get("worker") == index]
        observations = [s for s in samples if s["worker"] == index]
        start = next((e["mono_ns"] for e in records if e.get("event") == "worker_start"), None)
        terminal = next((e for e in records if e.get("event") == "parent_terminal"), {})
        exits = [e for e in records
                 if e.get("event") in {"decline", "route_stopped", "terminal_result"}]
        sampled_cpu = (sum(s["cpu_ticks_delta"] for s in observations) / hz
                       if observations else None)
        pid = terminal.get("worker_pid")
        phases = [e for e in events if e.get("record_pid") == pid and "phase" in e]
        phase_peak = max((e["peak_rss_kb"] * 1024 for e in phases
                          if e.get("peak_rss_kb", -1) >= 0), default=None)
        seconds = (terminal["mono_ns"] - start)/1e9 if start and terminal else None
        result.append({"arm": arm, "worker": index, "pid": terminal.get("worker_pid"),
                       "exit_s": seconds, "exit_code": terminal.get("exit_code"),
                       "signal": terminal.get("signal"), "reason": terminal.get("reason"),
                       "route": terminal.get("route"), "stage": terminal.get("stage"),
                       "telemetry": terminal.get("telemetry", "missing"),
                       "rss_sample_count": len(observations),
                       "sampled_peak_rss_bytes": max(
                           (s["rss_bytes"] for s in observations), default=None),
                       "phase_peak_rss_bytes": phase_peak,
                       "sampled_cpu_s": sampled_cpu,
                       "cpu_missing_reason": "arm exited between samples"
                       if not observations else "",
                       "phase_cpu_lower_bound_s": max(
                           (e["cpu_ns"]/1e9 for e in phases if e.get("cpu_ns") is not None),
                           default=None),
                       "mean_cores": sampled_cpu/seconds
                       if seconds and sampled_cpu is not None else None,
                       "max_threads": max((s["threads"] for s in observations), default=None),
                       "declines": [{"event": e["event"], "reason": e.get("reason"),
                                     "stage": e.get("stage"), "route": e.get("route"),
                                     "seconds": (e["mono_ns"]-start)/1e9 if start else None}
                                    for e in exits]})
    return result


def ending_resource(run, events):
    counters = json.loads(run.scope_memory_events or "{}")
    if classify_run(run) in SOLVED and any(e.get("event") == "parent_winner" for e in events):
        return "verified answer"
    if counters.get("oom_kill", 0):
        return "cgroup memory (OOM kill)"
    if run.timed_out or any(e.get("reason") in {"deadline", "deadline_rejected"} for e in events):
        return "wall deadline"
    if any(e.get("reason") == "resource" for e in events):
        return "arm resource limit; all arms ended"
    return "all arms ended" if run.returncode in (0, 1, 2) else "error (inspect stderr)"


def validate_membership(events, arms):
    specs = {e["worker"]: e for e in events if e.get("event") == "worker_spec"}
    contexts = {e["worker"]: e for e in events if e.get("event") == "worker_spawn"}
    if set(specs) != set(range(len(arms))):
        raise RuntimeError("worker membership is missing or differs from the build configuration")
    for index, arm in enumerate(arms):
        spec = specs[index]
        if spec["kind"] != "legacy":
            matches = spec["kind"] == arm
        else:
            parts = arm.split(":")
            polarity = spec["requested_polarity"].lower()
            transform = spec["translation"] if polarity == "real" else spec["transform"]
            backend = contexts.get(index, {}).get("requested_backend")
            matches = parts[:3] == [polarity, transform, backend]
            if len(parts) == 4:
                matches = matches and parts[3] == spec["provider"]
        if not matches:
            raise RuntimeError("observed worker differs from the supplied build configuration")


def run_one(args, arms, label):
    if not check_campaign_scopes("single-input attribution"):
        raise RuntimeError("another workload is running")
    output = args.output / label
    output.mkdir(parents=True, exist_ok=False)
    phases = output / "phases"
    phases.mkdir()
    host_before = host_snapshot()
    observer = ArmObserver(phases, args.interval)
    observer.thread.start()
    command = [str(args.binary.resolve()), "-T", str(args.input.resolve())]
    if args.stage_concurrency is not None:
        command += ["--stage-concurrency", args.stage_concurrency]
    if label != "race":
        command += ["--arms", arms[0]]
    env = dict(os.environ)
    env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
    scope_env = {"ACACIA_PHASE_RECORDS": str(phases.resolve()),
                 "ACACIA_OUTER_DEADLINE_MONOTONIC": str(time.monotonic()+args.cap)}
    try:
        run = run_systemd_scope(command, args.cap, "8G", "0", env=env,
                               scope_env=scope_env, unit_prefix="acacia-diagnostic",
                               cpu_quota=args.cpu_quota)
    finally:
        events, samples = observer.finish()
    for name, records in (("events", events), ("samples", samples)):
        (output / f"{name}.jsonl").write_text("".join(json.dumps(r)+"\n" for r in records))
    (output / "stdout.log").write_text(run.stdout)
    (output / "stderr.log").write_text(run.stderr)
    metadata = dataclasses.asdict(run)
    metadata.pop("stdout")
    metadata.pop("stderr")
    summary = {"schema": 1, "mode": label, "input": str(args.input.resolve()),
               "input_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
               "binary": str(args.binary.resolve()),
               "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
               "command": command, "cap_s": args.cap, "memory_max": "8G", "swap_max": "0",
               "cpu_quota": args.cpu_quota, "observed_constraints": observer.constraints,
               "observer_affinity": sorted(os.sched_getaffinity(0)),
               "sample_interval_s": args.interval, "observer_errors": observer.errors,
               "cpu_sample_note": "lower bound: work after the last sample may be missing",
               "host_before": host_before, "host_after": host_snapshot(),
               "run": metadata, "result": classify_run(run),
               "ending_resource": ending_resource(run, events),
               "arms": arm_summary(events, samples, arms)}
    (output / "summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    print(json.dumps({"mode": label, "result": summary["result"], "seconds": run.seconds,
                      "ending_resource": summary["ending_resource"], "output": str(output)}),
          flush=True)
    validate_membership(events, arms)
    if observer.errors or not events or not run.memory_peak_bytes or not run.scope_memory_events:
        raise RuntimeError("incomplete instrumentation; inspect summary before another input")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    select = commands.add_parser(
        "select", help="read evidence only; choose plausible contention inputs")
    select.add_argument("--race-rows", type=pathlib.Path, required=True)
    select.add_argument("--arm-rows", type=pathlib.Path, nargs="+", required=True)
    select.add_argument("--limit", type=int, default=3)
    select.add_argument("--output", type=pathlib.Path, required=True)
    run = commands.add_parser("run", help="diagnose exactly one input, sequentially")
    run.add_argument("--binary", type=pathlib.Path, required=True)
    run.add_argument("--input", type=pathlib.Path, required=True)
    run.add_argument("--build-config", type=pathlib.Path, required=True,
                     help="built .acacia-config.json, supplies shipping arm membership")
    run.add_argument("--output", type=pathlib.Path, required=True)
    run.add_argument("--cap", type=float, default=60)
    run.add_argument("--interval", type=float, default=0.1)
    run.add_argument("--mode", choices=("all", "race", "standalone"), default="all")
    run.add_argument("--stage-concurrency", help="off or a positive solver stage count")
    run.add_argument("--cpu-quota", help="optional matched scope CPU quota, e.g. 400%%")
    args = parser.parse_args()
    if args.command == "select":
        if args.limit <= 0:
            parser.error("selection limit must be positive")
        rows = select_candidates(args.race_rows, args.arm_rows)[:args.limit]
        args.output.write_text(json.dumps({"race_source": str(args.race_rows),
                                          "arm_sources": list(map(str, args.arm_rows)),
                                          "candidates": rows}, indent=2)+"\n")
        print(json.dumps(rows, indent=2))
        return 0
    if not 0 < args.cap < float("inf") or not 0 < args.interval < float("inf"):
        parser.error("cap and sample interval must be positive")
    arms = json.loads(args.build_config.read_text())["default_arms"].split(",")
    args.output.mkdir(parents=True, exist_ok=True)
    lock = ROOT / "build_scratch/i202-diagnostic.lock"
    with lock.open("w") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if not check_campaign_scopes("single-input attribution"):
            return 1
        if args.mode in {"all", "race"}:
            run_one(args, arms, "race")
        if args.mode in {"all", "standalone"}:
            for i, arm in enumerate(arms):
                run_one(args, [arm], f"arm-{i}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
