#!/usr/bin/env python3
"""Read #210 weakening events from fresh per-invocation diagnostic directories."""

from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import sys


def read_records(directory):
    records = []
    problems = []
    for path in sorted(Path(directory).glob("*.jsonl")):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            try:
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("expected object")
            except ValueError:
                problems.append(f"{path.name}:{number}: invalid/truncated record")
                continue
            row = dict(row, emitter=path.stem)
            records.append(row)
    return records, problems


def census(records, problems=()):
    """Counts are exact only with confirmed delivery and a closed pre-pass."""
    writers = [r for r in records if r.get("event") == "writer_summary"]
    summaries = [r for r in records if r.get("phase") == "record_summary"]
    delivery = bool(writers and summaries) and not problems
    delivery &= all(r.get("failed_records") == 0 and r.get("incomplete_packet") is False
                    for r in writers)
    delivery &= all(r.get("dropped_records", 0) == 0 for r in records)
    producer_sequences = defaultdict(list)
    for row in records:
        if "seq" in row and row.get("observer") != "parent" and row.get("event") not in {
            "worker_spawn", "parent_terminal", "parent_winner"
        }:
            producer_sequences[(row.get("emitter"), row.get("worker_pid"))].append(row["seq"])
    for sequences in producer_sequences.values():
        delivery &= sequences == list(range(1, sequences[-1] + 1))
    workers = {r["worker_pid"] for r in records if r.get("event") in {
        "worker_start", "parent_terminal", "weakening_entry"
    }}
    result = []
    for pid in sorted(workers):
        worker_rows = [r for r in records if r.get("worker_pid") == pid]
        passes = sorted({r["prepass"] for r in worker_rows if r.get("prepass")}) or [None]
        for prepass in passes:
            rows = [r for r in worker_rows if r.get("prepass") == prepass]
            eligibility = next((r for r in rows if r.get("event") == "weakening_eligibility"), {})
            starts = [r for r in rows if r.get("event") == "weakening_attempt_start"]
            ends = [r for r in rows if r.get("event") == "weakening_attempt_end"]
            generated = [r for r in rows if r.get("event") == "weakening_candidate_generated"]
            finish = next((r for r in rows if r.get("event") == "weakening_end"), {})
            fallback = next((r for r in rows
                             if r.get("event") == "weakening_fallback_start"), None)
            terminal = next((r for r in worker_rows if r.get("event") == "parent_terminal"), None)
            exact = bool(delivery and eligibility and terminal and
                         (finish or any(r.get("observer") == "parent" for r in ends)))
            generated_ids = {r.get("run_id") for r in generated}
            started_ids = {r.get("run_id") for r in starts}
            ended_ids = {r.get("run_id") for r in ends}
            exact &= len(generated) == eligibility.get("generated", -1)
            exact &= len(generated_ids) == len(generated)
            exact &= all(type(run_id) is int and run_id > 0 for run_id in generated_ids)
            exact &= len(started_ids) == len(starts) and len(ended_ids) == len(ends)
            exact &= started_ids <= generated_ids and started_ids == ended_ids
            mode = next((r.get("mode") for r in rows
                         if r.get("event") == "weakening_mode"), "basic")
            requested_mode = next((r.get("mode") for r in worker_rows
                                   if r.get("event") == "weakening_requested_mode"), mode)
            source = next((r for r in rows if r.get("event") == "weakening_source"), {})
            proofs = [r for r in rows if r.get("event") == "weakening_proof"]
            successful_ids = {r.get("run_id") for r in ends if r.get("outcome") == "proof"}
            proof_ids = {r.get("run_id") for r in proofs}
            successes = sum(r.get("outcome") == "proof" for r in ends)
            bindings = {(r.get("run_id"), r.get("kind")) for r in rows
                        if r.get("event") == "weakening_binding"}
            exact &= len(proofs) == len(proof_ids) and proof_ids == successful_ids
            exact &= all((run_id, "candidate") in bindings for run_id in generated_ids)
            exact &= all((run_id, "runner_objective") in bindings for run_id in successful_ids)
            exact &= bool(source and (0, "simplified_original") in bindings)
            cancellations = sum(r.get("outcome") == "cancellation" for r in ends)
            starved = None
            if exact:
                if fallback:
                    remaining = fallback.get("remaining_ns")
                    starved = (remaining == 0 if remaining is not None else
                               False if fallback.get("deadline_ns") == 0 else None)
                elif successes or terminal.get("reason") == "winner_cancelled":
                    starved = False
                else:
                    # A closed lifecycle with no proof/fallback used up the
                    # worker's opportunity to run the original. External timeout
                    # reaches older binaries as "interrupted", without a local
                    # deadline; this is still starvation, not an observed fallback.
                    # Winner cancellation is the explicit exception: another arm
                    # discharged the invocation. Incomplete telemetry stays UNKNOWN.
                    exhausted = any(r.get("remaining_ns") == 0 for r in rows)
                    deadline = next((r.get("deadline_ns") for r in rows
                                     if r.get("event") == "weakening_entry"), 0)
                    exhausted |= bool(deadline and terminal.get("mono_ns", 0) >= deadline)
                    starved = (exhausted or terminal.get("reason") == "deadline" or
                               bool(starts) and terminal.get("reason") == "interrupted")
            completed = sum(r.get("observer") == "worker" for r in ends)
            cpu_known = all(r.get("cpu_ns") is not None for r in ends)
            result.append({
                "worker_pid": pid, "prepass": prepass, "mode": requested_mode,
                "effective_mode": mode,
                "source_fnv1a64": source.get("source_fnv1a64"),
                "eligible": eligibility.get("eligible"), "reason": eligibility.get("reason"),
                "largest_global_conjunction_size": eligibility.get(
                    "largest_global_conjunction_size"),
                "generated": eligibility.get("generated") if exact else None,
                "started": len(starts) if exact else None,
                "completed": completed if exact else None,
                "successes": successes if exact else None,
                "observed_generated": len(generated),
                "observed_started": len(starts), "observed_completed": completed,
                "observed_successes": successes,
                "attempt_wall_ns": sum(r["wall_ns"] for r in ends) if exact else None,
                "attempt_cpu_ns": sum(r["cpu_ns"] for r in ends) if exact and cpu_known else None,
                "prepass_wall_ns": finish.get("wall_ns"),
                "full_solver_started": True if fallback else False if exact else None,
                "full_solver_remaining_ns": fallback.get("remaining_ns") if fallback else None,
                "deadline_ns": next((r.get("deadline_ns") for r in rows
                                     if r.get("event") == "weakening_entry"), None),
                "prepass_outcome": finish.get("outcome"),
                "parent_reason": terminal.get("reason") if terminal else None,
                "full_solver_starved": starved,
                "cancellations": cancellations if exact else None,
                "observed_cancellations": cancellations,
                "observed_dropped_records": max((r.get("dropped_records", 0)
                                                 for r in worker_rows), default=0),
                "writer_failed_records": sum(r["failed_records"] for r in writers)
                if writers and all(type(r.get("failed_records")) is int for r in writers)
                else None,
                "delivery": "complete" if delivery else "incomplete",
                "census": "complete" if exact else "incomplete",
            })
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, help="TSV output; default stdout")
    args = parser.parse_args(argv)
    rows = []
    for directory in args.directories:
        records, problems = read_records(directory)
        for problem in problems:
            print(f"{directory}: {problem}", file=sys.stderr)
        rows.extend(dict(invocation=str(directory), **r) for r in census(records, problems))
    if not rows:
        parser.error("no worker records; absence does not establish zero attempts")
    stream = args.output.open("w", newline="") if args.output else sys.stdout
    try:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows({k: "UNKNOWN" if v is None else v for k, v in row.items()}
                         for row in rows)
    finally:
        if args.output:
            stream.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
