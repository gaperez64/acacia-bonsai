#!/usr/bin/env python3
"""Check cap-independent lift decisions and measured work for fast 60 s rows."""

import argparse
import csv
import json
from pathlib import Path
import re


VOLATILE_FIELDS = frozenset({
    "wall_ns", "cpu_ns", "elapsed_ns", "rss_kb", "peak_rss_kb",
    "peak_rss_bytes", "mallinfo2",
})
ARM = "real:param-lift:oxidd"


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--short", type=Path, required=True)
    parser.add_argument("--long", type=Path, required=True)
    parser.add_argument("--short-records", type=Path, required=True)
    parser.add_argument("--long-records", type=Path, required=True)
    args = parser.parse_args()
    short, long = rows(args.short), rows(args.long)
    failures = []
    if short.keys() != long.keys():
        failures.append(f"panel instances differ: short-only={sorted(short.keys() - long.keys())}, "
                        f"long-only={sorted(long.keys() - short.keys())}")
    short_records, long_records = {}, {}
    for row_set, root, records in ((short, args.short_records, short_records),
                                   (long, args.long_records, long_records)):
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
        for field in ("result", "exit_code", "timed_out", "resource_reason"):
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


if __name__ == "__main__":
    main()
