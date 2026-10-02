#!/usr/bin/env python3
"""Check that short-cap outcomes, decisions, decline stages and work are cap independent.

The default is the P1b-6 check: the lifting arm's records, over two runs of the
same panel, for the 60 s rows that finished within 17 s.  The comparator is also
imported by tools/recycle-cap.py, which validates a sample of recycled rows on
every arm, on every sampled row, whatever its long-cap time.

What is compared, per instance:
  outcome   result, exit code, timeout flag and resource reason of the TSV rows;
  records   per arm, every phase-record field except the volatile ones
            (benchlib.PHASE_VOLATILE_FIELDS: time, memory), which covers the
            proof-method decision events, the decline stage events and all work
            counts.  A mismatch is labelled decision, decline or work.

Race truncation: in a portfolio, losing arms are killed when the winner
answers, at a point that depends on timing, not on the cap.  An arm's records
are therefore compared only when the arm ran to completion (wrote a
record_summary) in both runs, or in neither run of a non-conclusive row.  An
arm that completed in exactly one run is compared strictly by default. The
P4 sampler can explicitly allow conclusive race truncation.
"""

import argparse
import csv
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
    load_phase_records,
    phase_record_dir,
)

VOLATILE_FIELDS = PHASE_VOLATILE_FIELDS
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


def mismatch_kind(first, second):
    """Classify how two canonical record sets differ, for the report."""
    def events(canonical, test):
        return sorted((value.get("phase", ""), value.get("stage", ""))
                      for sequence in canonical for event in sequence
                      if test((value := json.loads(event)).get("phase", "")))

    def is_decline(phase):
        return phase == "budget_decline" or phase.startswith(PHASE_DECLINE_PREFIXES)

    def is_decision(phase):
        return phase.startswith("lift_method_")

    if events(first, is_decline) != events(second, is_decline):
        return "decline"
    if events(first, is_decision) != events(second, is_decision):
        return "decision"
    return "work"


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

    def fail(instance, arm, kind, message):
        failures.append({"instance": instance, "arm": arm, "kind": kind, "message": message})

    extra = sorted(long.keys() - short.keys())
    missing = sorted(short.keys() - long.keys())
    if extra or (missing and not subset):
        fail(None, None, "panel", f"panel instances differ: short-only={missing}, "
             f"long-only={extra}")
    if audit_all_records and short_records is not None and long_records is not None:
        for label, row_set, root in (("short", short, short_records), ("long", long, long_records)):
            for name, row in row_set.items():
                try:
                    found = load_phase_records(phase_record_dir(
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
            first = load_phase_records(phase_record_dir(
                short_records, seventeen["solver_label"], seventeen["cap_s"], name,
                seventeen["run_index"]))
            second = load_phase_records(phase_record_dir(
                long_records, sixty["solver_label"], sixty["cap_s"], name, sixty["run_index"]))
        except (OSError, ValueError) as error:
            fail(name, None, "records", f"{name}: {error}")
            continue
        for label, records in (("short", first), ("long", second)):
            if not records.processes:
                fail(name, None, "records", f"{name}: missing or empty {label} records: "
                     f"{records.directory}")
            elif records.dropped:
                fail(name, None, "records", f"{name}: dropped {label} records: "
                     f"{records.directory}")
        if not first.processes or not second.processes or first.dropped or second.dropped:
            continue
        conclusive = seventeen["result"] in SOLVED and sixty["result"] in SOLVED
        selected = sorted(set(first.arms()) | set(second.arms())) if arms is None else arms
        for arm in selected:
            one, two = first.arm_processes(arm), second.arm_processes(arm)
            if not one or not two:
                if one or two or arms is not None:
                    where = "either run" if not one and not two else (
                        "the long run" if one else "the short run")
                    fail(name, arm, "records", f"{name}: no {arm} records in {where}")
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
                kind = mismatch_kind(a, b)
                fail(name, arm, kind, f"{name}: {arm} {kind} differs: {a} != {b}")
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
    arms = None if args.all_arms else (args.arm or [ARM])
    short, long = rows(args.short), rows(args.long)
    if not (args.arm or args.all_arms or args.all_rows or args.subset or
            args.allow_race_truncation or args.short_cap != 17.0):
        legacy_default(short, long, args.short_records, args.long_records)
        return
    checked, failures, notes = compare(
        short, long, args.short_records, args.long_records, arms=arms,
        max_long_seconds=None if args.all_rows else args.short_cap, subset=args.subset,
        audit_all_records=not args.all_rows and not args.subset,
        allow_race_truncation=args.allow_race_truncation)
    scope = "long rows" if args.all_rows else f"completed 60 s rows at or below {args.short_cap:g} s"
    print(f"checked {checked} {scope}")
    for note in notes:
        print(f"note: {note}")
    for failure in failures:
        print(failure["message"])
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
