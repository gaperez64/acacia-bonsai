#!/usr/bin/env python3
"""Check that short-cap outcomes, decisions, decline stages and work are cap independent.

The default is the P1b-6 check: the lifting arm's records, over two runs of the
same panel, for the 60 s rows that finished within 17 s.  The comparator is also
imported by tools/recycle-cap.py, which validates a sample of recycled rows on
every arm, on every sampled row, whatever its long-cap time.

What is compared, per instance:
  outcome   result, exit code, timeout flag and resource reason of the TSV rows;
  records   per configured arm, decision, terminal outcome/reason and work;
            parent winner and configuration are checked independently. Only
            documented run identity, clocks, durations and RSS are excluded.
            See phase-record-comparison.md for the schema and exact exclusions.

Race truncation: in a portfolio, losing arms are killed when the winner
answers, at a point that depends on timing, not on the cap. An arm's records
are compared when it completed in both runs (current parent_terminal, or old
record_summary), or in neither run of a non-conclusive row.  An
arm that completed in exactly one run is compared strictly by default. The
P4 sampler can explicitly allow conclusive race truncation.
"""

import argparse
import csv
from dataclasses import dataclass
from itertools import zip_longest
import importlib.util
import json
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import (  # noqa: E402
    PHASE_DECLINE_PREFIXES,
    PHASE_VOLATILE_FIELDS,
    PhaseProcess,
    PhaseRecords,
    load_phase_records,
    phase_record_dir,
)

from recycle_adjudication import adjudicate, paired_evidence, section  # noqa: E402

# Exact exclusions and their meanings are documented in phase-record-comparison.md.
VOLATILE_FIELDS = PHASE_VOLATILE_FIELDS | frozenset({
    "pid", "worker_pid", "seq", "scope", "unit", "scope_name", "unit_name",
    "mono_ns", "entry_ns", "completion_ns", "stage_start_ns", "deadline_ns",
    "outer_deadline_ns", "absolute_ns", "reference_ns", "remaining_ns",
    "initial_remaining_ns", "allowance_ns", "consumed_ns", "total_ns",
})
VOLATILE_METRICS = frozenset({
    "bdd_gc_wall_ns", "first_useful_rank_query_ms", "row_generation_ms",
    "row_started_clock_ms", "stage_started_clock_ms", "verification_row_ms",
})
PARENT_ARM = "legacy_parent"
ARM = "real:param-lift:oxidd"
OUTCOME_FIELDS = ("result", "exit_code", "timed_out", "resource_reason")
SOLVED = frozenset({"REALIZABLE", "UNREALIZABLE"})


def rows(path):
    with path.open(newline="") as source:
        result = {}
        for row in csv.DictReader(source, delimiter="\t"):
            name = row["instance"]
            if name in result:
                raise ValueError(f"{path}: duplicate instance {name}")
            result[name] = row
    if not result:
        raise ValueError(f"{path}: no rows")
    return result


def record(row, root):
    """Legacy single-arm view: the lifting arm's events, in file order."""
    safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", row["instance"])
    base = (root / row["solver_label"] / row["cap_s"] /
            f'{safe_name}-{row["run_index"]}')
    paths = sorted(base.glob("*.jsonl"))
    if not paths:
        raise ValueError(f"{row['instance']}: missing or empty records: {base}")
    entries = []
    for path in paths:
        for line in path.read_text().splitlines():
            event = json.loads(line)
            if event.get("arm") == ARM:
                entries.append(event)
    if not entries:
        raise ValueError(f"{row['instance']}: no {ARM} records: {base}")
    if any(event.get("phase") == "record_summary" and
           event.get("dropped_records") for event in entries):
        raise ValueError(f"{row['instance']}: dropped records: {base}")
    return tuple({key: value for key, value in event.items()
                  if key not in VOLATILE_FIELDS and
                  key != "dropped_records"} for event in entries)


@dataclass(frozen=True)
class ComparisonRecords(PhaseRecords):
    transport_failures: tuple = ()


def comparison_records(directory):
    """Join the current transport by PID, then compare logical configured arms.

    The old per-arm format is preserved. Current worker specs bind parent
    lifecycle events and PID-named child files; neither file order nor PID is
    an arm identity. Parent context is checked separately from race losers.
    """
    raw = load_phase_records(directory)
    events = [event for process in raw.processes for event in process.events]
    specs = [event for event in events if event.get("event") == "worker_spec"]
    if not specs:
        if any(event.get("event") in {"worker_spawn", "parent_terminal", "parent_winner"}
               for event in events):
            raise ValueError(f"{directory}: worker lifecycle has no worker_spec records")
        return raw

    def invalid(message):
        raise ValueError(f"{directory}: {message}")

    writers = [event for event in events if event.get("event") == "writer_summary"]
    if len(writers) != 1:
        invalid("missing or duplicate writer_summary")
    writer = writers[0]
    if writer.get("failed_records") != 0 or writer.get("incomplete_packet") is not False:
        invalid("failed or incomplete phase-record delivery")
    if writer.get("delivered_records") != len(events) - 1:
        invalid("writer delivered_records does not match records present")
    spec_workers = {str(e.get("worker_pid")): e.get("worker") for e in specs}
    drops = {}
    for process in raw.processes:
        for event in process.events:
            if event.get("dropped_records", 0):
                pid = str(event.get("worker_pid", Path(process.name).stem))
                worker = event.get("worker", spec_workers.get(pid, event.get("arm", pid)))
                drops[worker] = max(drops.get(worker, 0), event["dropped_records"])

    by_pid, by_worker = {}, {}
    groups, parent = {}, []
    for spec in specs:
        required = {"worker_pid", "worker", "kind", "requested_polarity",
                    "translation", "transform"}
        if not required <= spec.keys():
            invalid(f"worker_spec missing fields: {sorted(required - spec.keys())}")
        pid, worker = str(spec["worker_pid"]), spec["worker"]
        if pid in by_pid or worker in by_worker:
            invalid("duplicate worker_spec identity")
        spawn = [event for event in events if event.get("event") == "worker_spawn"
                 and str(event.get("worker_pid")) == pid]
        if (len(spawn) != 1 or spawn[0].get("worker") != worker
                or "requested_backend" not in spawn[0]):
            invalid(f"worker {worker}: missing or conflicting worker_spawn")
        if spec["kind"] == "legacy":
            polarity = spec["requested_polarity"].lower()
            method = spec["translation"] if polarity == "real" else spec["transform"]
            arm = f"{polarity}:{method}:{spawn[0]['requested_backend']}"
        else:
            arm = spec["kind"]
        by_pid[pid], by_worker[worker] = arm, pid
        groups[pid] = []
        # Arm configuration is checked even when its worker is race-truncated.
        parent.extend([{**spawn[0], "arm": arm, "configured_arm": arm},
                       {**spec, "arm": arm, "configured_arm": arm}])

    terminals = {}
    for process in raw.processes:
        file_pid = Path(process.name).stem
        for event in process.events:
            kind = event.get("event")
            if kind in {"writer_summary", "worker_spawn", "worker_spec"}:
                continue
            pid = str(event.get("worker_pid", event.get("pid", file_pid)))
            if (pid not in by_pid and "worker" in event
                    and "worker_pid" not in event and "pid" not in event):
                pid = by_worker.get(event["worker"])
            if pid in by_pid:
                if "worker" in event and by_worker.get(event["worker"]) != pid:
                    invalid("worker index and PID disagree")
                arm = by_pid[pid]
                if event.get("arm") not in {None, "legacy", arm}:
                    invalid(f"{arm}: conflicting recorded arm {event['arm']}")
                # 'legacy' phases have no PID field: their producer filename binds them.
                bound = {**event, "arm": arm}
                if kind == "parent_winner":
                    # Delivery snapshots belong to per-worker transport completeness.
                    parent.append({k: v for k, v in bound.items()
                                   if k not in {"telemetry", "dropped_records"}})
                else:
                    groups[pid].append(bound)
                if kind == "parent_terminal":
                    if pid in terminals:
                        invalid(f"{arm}: duplicate parent_terminal")
                    terminals[pid] = event
            elif event.get("arm") == PARENT_ARM:
                parent.append(event)
            else:
                invalid(f"unbound phase record: {event}")

    processes = []
    for pid, arm in by_pid.items():
        group = groups[pid]
        worker = next(w for w, worker_pid in by_worker.items() if worker_pid == pid)
        end = terminals.get(pid)
        if end is None:
            invalid(f"{arm}: missing parent_terminal")
        finished = (end.get("telemetry") == "complete" and end.get("signal") == 0
                    and end.get("exit_code", -1) >= 0)
        if finished and not drops.get(worker, 0) and not any(
                e.get("event") == "worker_start" for e in group):
            invalid(f"{arm}: completed worker has no child records")
        sequence = sorted({e["seq"] for e in events
                           if str(e.get("worker_pid")) == pid and "seq" in e})
        if not drops.get(worker, 0) and sequence and (
                sequence[0] != 1 or any(b != a + 1 for a, b in zip(sequence, sequence[1:]))):
            invalid(f"{arm}: missing worker sequence records")
        # Parent records are in a different producer file. Append lifecycle
        # snapshots after the child's ordered work, independent of filename order.
        child = [e for e in group if e.get("event") not in
                 {"parent_terminal", "stage_censored"}]
        tail = [e for e in group if e.get("event") in
                {"parent_terminal", "stage_censored"}]
        stage_ids = {}
        normalized = []
        for event in child + tail:
            if event.get("event") == "stage_metric" and event.get("key") in VOLATILE_METRICS:
                continue
            event = dict(event)
            if "stage_id" in event:
                # Preserve the occurrence/reference relation, not its serial number.
                event["stage_id"] = stage_ids.setdefault(event["stage_id"], len(stage_ids))
            normalized.append(event)
        processes.append(PhaseProcess(arm, tuple(normalized), finished, drops.get(worker, 0)))
    # These independent parent facts can be interleaved by reaping order.
    parent.sort(key=lambda e: (e.get("event", e.get("phase", "")),
                               str(e.get("worker", "")),
                               json.dumps({k: v for k, v in e.items()
                                           if k not in VOLATILE_FIELDS}, sort_keys=True)))
    processes.append(PhaseProcess(PARENT_ARM, tuple({**e, "arm": PARENT_ARM,
                                                   "winning_arm": e["arm"]}
                                                  if e.get("event") == "parent_winner"
                                                  else {**e, "arm": PARENT_ARM}
                                                  for e in parent), True,
                                  drops.get(PARENT_ARM, 0)))
    transport = ()
    if drops:
        transport = ({
            "message": f"{directory}: dropped phase records (dropped_records): " +
                       ", ".join(f"worker {worker}={count}" for worker, count in drops.items()),
            "differences": [{"category": "transport-completeness", "worker": worker,
                             "field": "dropped_records", "count": count}
                            for worker, count in drops.items()],
        },)
    return ComparisonRecords(raw.directory, tuple(processes), transport)


def solver_arms(records):
    """Configured workers; parent context is always compared but is not an arm."""
    return set(records.arms()) - {PARENT_ARM}


def difference_kind(one, two, field):
    """Only named winner fields and exit/cancellation reasons are adjudicable."""
    event = one.get("event")
    if event != two.get("event") or one.get("phase") != two.get("phase"):
        return "work"
    if event == "parent_winner" and field in {
            "worker", "winning_arm", "requested_backend", "effective_backend", "stage"}:
        return "decision"
    if event == "parent_terminal" and field == "reason" and {
            one.get(field), two.get(field)} <= {"exit", "winner_cancelled"}:
        return "decision"
    if field in {"result", "verdict", "exit_code", "signal", "timed_out", "resource_reason"}:
        return "outcome"
    if field in {"original_polarity", "proof_polarity"} or any(
            token in field for token in ("source", "proof", "certificate", "sha256", "digest")):
        return "proof_binding"
    phase = one.get("phase", "")
    if field in {"phase", "event", "reason", "stage"} and (
            phase == "budget_decline" or phase.startswith(PHASE_DECLINE_PREFIXES)
            or event == "decline"):
        return "decline"
    if field in {
                "arm", "configured_arm", "requested_backend", "effective_backend",
                "requested_polarity", "kind", "translation", "transform", "provider",
                "route", "limit", "budget", "policy", "fraction", "r_prepass", "equivariance"}:
        return "configuration"
    if event in {"worker_spec", "worker_spawn"} and field == "worker":
        return "configuration"
    if event in {"parent_terminal", "parent_winner", "terminal_result"} and \
            field in {"reason", "telemetry"}:
        return "lifecycle"
    return "work"


def record_differences(first, second):
    """Keep every structured difference; diagnostics may be shortened separately."""
    found = {}

    def add(kind, **difference):
        found.setdefault(kind, []).append({"category": kind, **difference})

    for process, (one, two) in enumerate(zip_longest(first, second, fillvalue=())):
        if process >= min(len(first), len(second)):
            add("work", process=process, field="process_count", short=len(first), long=len(second))
        for index, (a, b) in enumerate(zip_longest(one, two)):
            if a == b:
                continue
            a, b = json.loads(a) if a is not None else {}, json.loads(b) if b is not None else {}
            context = a.get("event", a.get("phase", b.get("event", b.get("phase", "record"))))
            if "key" in a:
                context += f"[{a['key']}]"
            for field in sorted(a.keys() | b.keys()):
                if (field in a) != (field in b) or a.get(field) != b.get(field):
                    kind = difference_kind(a, b, field)
                    add(kind, process=process, record=index, event=context, field=field,
                        short=a.get(field), long=b.get(field),
                        short_present=field in a, long_present=field in b)
    return found


def record_difference(differences):
    """Bound diagnostic text without discarding the structured findings."""
    records = {}
    for diff in differences:
        label = (f"record {diff['record']} {diff['event']}" if "record" in diff else "structure")
        records.setdefault((diff["process"], label), []).append(
            f"{diff['field']}: {diff['short']!r} != {diff['long']!r}")
    lines = [f"{label}: " + ", ".join(fields) for (_, label), fields in records.items()]
    return "; ".join(lines[:8]) + ("; further differences omitted" if len(lines) > 8 else "")


def compare(short, long, short_records=None, long_records=None, *, arms=None,
            max_long_seconds=None, subset=False, volatile=VOLATILE_FIELDS,
            audit_all_records=False, allow_race_truncation=False):
    """Compare two runs of the same series; return (checked, failures, notes).

    Each failure is a dict with instance, arm (None for outcome or record
    availability problems), kind and message.  `arms` restricts the record
    comparison (None: every arm seen in either run).  `max_long_seconds`
    reproduces the historical filter; None compares every long row.
    `audit_all_records` also requires usable records for rows that are not
    compared, as the original single-arm check did.
    """
    failures, notes = [], []

    def fail(instance, arm, kind, message, *, differences=None):
        failure = {"instance": instance, "arm": arm, "kind": kind, "message": message}
        if differences is not None:
            failure["differences"] = differences
        failures.append(failure)

    extra = sorted(long.keys() - short.keys())
    missing = sorted(short.keys() - long.keys())
    if extra or (missing and not subset):
        fail(None, None, "panel", f"panel instances differ: short-only={missing}, "
             f"long-only={extra}")
    if audit_all_records and short_records is not None and long_records is not None:
        for label, row_set, root in (("short", short, short_records), ("long", long, long_records)):
            for name, row in row_set.items():
                try:
                    found = comparison_records(phase_record_dir(
                        root, row["solver_label"], row["cap_s"], name, row["run_index"]))
                except (OSError, ValueError) as error:
                    fail(name, None, "records", f"{name}: {error}")
                    continue
                if not found.processes:
                    fail(name, None, "records",
                         f"{name}: missing or empty {label} records: {found.directory}")
                for arm in arms or ():
                    if found.processes and not found.arm_processes(arm):
                        fail(name, arm, "records", f"{name}: no {arm} {label} records: "
                             f"{found.directory}")
    checked = 0
    for name, sixty in long.items():
        if max_long_seconds is not None and (
                sixty["timed_out"] == "true" or float(sixty["seconds"]) > max_long_seconds):
            continue
        seventeen = short.get(name)
        if seventeen is None:
            fail(name, None, "panel", f"{name}: missing short-cap row")
            continue
        checked += 1
        for field in OUTCOME_FIELDS:
            if sixty[field] != seventeen[field]:
                fail(name, None, "outcome", f"{name}: {field} {sixty[field]} != {seventeen[field]}")
        if short_records is None or long_records is None:
            continue
        try:
            first = comparison_records(phase_record_dir(
                short_records, seventeen["solver_label"], seventeen["cap_s"], name,
                seventeen["run_index"]))
            second = comparison_records(phase_record_dir(
                long_records, sixty["solver_label"], sixty["cap_s"], name, sixty["run_index"]))
        except (OSError, ValueError) as error:
            fail(name, None, "records", f"{name}: {error}")
            continue
        for label, records in (("short", first), ("long", second)):
            if not records.processes:
                fail(name, None, "records", f"{name}: missing or empty {label} records: "
                     f"{records.directory}")
            transport = getattr(records, "transport_failures", ())
            for finding in transport:
                fail(name, None, "records", f"{name}: {finding['message']}",
                     differences=finding["differences"])
            if records.dropped and not transport:
                fail(name, None, "records", f"{name}: dropped {label} records: "
                     f"{records.directory}", differences=[
                         {"category": "transport-completeness", "worker": p.name,
                          "field": "dropped_records", "count": p.dropped}
                         for p in records.processes if p.dropped])
        if not first.processes or not second.processes:
            continue
        conclusive = seventeen["result"] in SOLVED and sixty["result"] in SOLVED
        for label, records, row in (("short", first, seventeen), ("long", second, sixty)):
            context = [e for p in records.arm_processes(PARENT_ARM) for e in p.events]
            if any(e.get("event") == "worker_spec" for e in context):
                winners = [e for e in context if e.get("event") == "parent_winner"]
                if row["result"] in SOLVED:
                    if len(winners) != 1:
                        fail(name, PARENT_ARM, "records",
                             f"{name}: missing or duplicate {label} parent_winner")
                    elif str(winners[0].get("exit_code")) != row["exit_code"]:
                        fail(name, PARENT_ARM, "outcome",
                             f"{name}: {label} parent_winner exit_code differs from row")
        selected = sorted(set(first.arms()) | set(second.arms())) if arms is None else list(arms)
        if PARENT_ARM in first.arms() or PARENT_ARM in second.arms():
            selected = sorted(set(selected) | {PARENT_ARM})
        for arm in selected:
            one, two = first.arm_processes(arm), second.arm_processes(arm)
            if not one or not two:
                if one or two or arms is not None:
                    where = "either run" if not one and not two else (
                        "the long run" if one else "the short run")
                    fail(name, arm, "records", f"{name}: no {arm} records in {where}")
                continue
            if any(p.dropped for p in one + two):
                notes.append(f"{name}: {arm} transport incomplete; work remains unknown")
                continue
            done_one, done_two = first.arm_finished(arm), second.arm_finished(arm)
            if done_one != done_two or (not done_one and conclusive):
                if conclusive and allow_race_truncation:
                    notes.append(f"{name}: {arm} race-truncated; not compared")
                    continue
                if not conclusive:
                    fail(name, arm, "completion",
                         f"{name}: {arm} completed in {'short' if done_one else 'long'} run only")
                    continue
            a, b = first.canonical(arm, volatile), second.canonical(arm, volatile)
            if a != b:
                for kind, differences in record_differences(a, b).items():
                    fail(name, arm, kind,
                         f"{name}: {arm} {kind} differs: {record_difference(differences)}",
                         differences=differences)
    return checked, failures, notes


def legacy_default(short, long, short_root, long_root):
    """Preserve the original default gate's ordering, wording and exit code."""
    failures = []
    if short.keys() != long.keys():
        failures.append(f"panel instances differ: short-only={sorted(short.keys() - long.keys())}, "
                        f"long-only={sorted(long.keys() - short.keys())}")
    short_records, long_records = {}, {}
    for row_set, root, records in ((short, short_root, short_records),
                                   (long, long_root, long_records)):
        for name, row in row_set.items():
            try:
                records[name] = record(row, root)
            except (ValueError, OSError, json.JSONDecodeError) as error:
                failures.append(str(error))
    checked = 0
    for name, sixty in long.items():
        if sixty["timed_out"] == "true" or float(sixty["seconds"]) > 17:
            continue
        checked += 1
        seventeen = short.get(name)
        if seventeen is None:
            failures.append(f"{name}: missing 17 s row")
            continue
        for field in OUTCOME_FIELDS:
            if sixty[field] != seventeen[field]:
                failures.append(f"{name}: {field} {sixty[field]} != {seventeen[field]}")
        if name in short_records and name in long_records:
            a, b = short_records[name], long_records[name]
            if a != b:
                failures.append(f"{name}: decision, decline stage, or work differs: {a} != {b}")
    print(f"checked {checked} completed 60 s rows at or below 17 s")
    for failure in failures:
        print(failure)
    if failures:
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--short", type=Path, required=True)
    parser.add_argument("--long", type=Path, required=True)
    parser.add_argument("--short-records", type=Path, required=True)
    parser.add_argument("--long-records", type=Path, required=True)
    parser.add_argument("--adjudications", type=Path,
                        help="driver decisions bound to exact mismatches and SHA-256 evidence")
    parser.add_argument("--series", help="series identity required with --adjudications")
    parser.add_argument("--short-cap", type=float, default=17.0,
                        help="compare only long rows that finished within this cap (default 17)")
    parser.add_argument("--arm", action="append",
                        help=f"arm whose records are compared (default {ARM}); repeatable")
    parser.add_argument("--all-arms", action="store_true",
                        help="compare every arm that wrote records")
    parser.add_argument("--all-rows", action="store_true",
                        help="compare every long row, however long it took")
    parser.add_argument("--subset", action="store_true",
                        help="the long run covers a subset of the short run's instances")
    parser.add_argument("--allow-race-truncation", action="store_true",
                        help="P4 sampler: skip conclusive arms truncated by a race")
    args = parser.parse_args()
    if args.adjudications and not args.series:
        parser.error("--adjudications requires --series")
    arms = None if args.all_arms else (args.arm or [ARM])
    short, long = rows(args.short), rows(args.long)
    if not (args.arm or args.all_arms or args.all_rows or args.subset or
            args.allow_race_truncation or args.adjudications or args.short_cap != 17.0):
        legacy_default(short, long, args.short_records, args.long_records)
        return
    checked, failures, notes = compare(
        short, long, args.short_records, args.long_records, arms=arms,
        max_long_seconds=None if args.all_rows else args.short_cap, subset=args.subset,
        audit_all_records=not args.all_rows and not args.subset,
        allow_race_truncation=args.allow_race_truncation)
    try:
        failures, adjudicated = adjudicate(
            args.adjudications, args.series, failures, short, long,
            required_evidence=paired_evidence(args.short, args.long, short, long,
                                              args.short_records, args.long_records))
    except (OSError, ValueError) as error:
        parser.exit(2, f"adjudication rejected: {error}\n")
    scope = "long rows" if args.all_rows else f"completed 60 s rows at or below {args.short_cap:g} s"
    print(f"checked {checked} {scope}")
    for note in notes:
        print(f"note: {note}")
    for failure in failures:
        print(failure["message"])
    print(section(adjudicated))
    if failures:
        raise SystemExit(1)


def load_as_module(path=Path(__file__)):
    """Import this hyphenated script from another tool."""
    spec = importlib.util.spec_from_file_location("check_cap_independence", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


if __name__ == "__main__":
    main()
