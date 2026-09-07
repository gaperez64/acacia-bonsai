#!/usr/bin/env python3
"""Join coverage measurements to final systemd journal accounting, without rerunning.

Prefer the recorded scope unit. For older coverage rows, match the exact solver
argv and TLSF basename plus a unique end timestamp within two seconds. Never
infer memory use from a cap or turn missing accounting into a measured zero.
The original verdict and resource reason remain in the annotated output.
"""
from __future__ import annotations

import argparse
import csv
import datetime
import json
import pathlib
import shlex

DECISIVE = {"REALIZABLE", "UNREALIZABLE"}
EXTRA = ["scope_unit", "scope_accounting_source", "scope_cpu_seconds",
         "scope_memory_peak_bytes", "scope_memory_swap_peak_bytes",
         "scope_oom_kill", "original_result", "original_resource_reason"]


def read_journal(path):
    units = {}
    with path.open() as stream:
        for line in stream:
            entry = json.loads(line)
            unit = entry.get("USER_UNIT", "")
            if not unit.startswith("acacia-"):
                continue
            value = units.setdefault(unit, dict(unit=unit, argv=None, end=None,
                                                cpu=None, memory=None, swap=None, oom=False))
            message = entry.get("MESSAGE", "")
            if message.startswith("Started ") and " - [systemd-run] " in message:
                command = message.split(" - [systemd-run] ", 1)[1].removesuffix(".")
                try:
                    value["argv"] = shlex.split(command)
                except ValueError:
                    pass
            if entry.get("UNIT_RESULT") == "oom-kill":
                value["oom"] = True
            for field, key in (("CPU_USAGE_NSEC", "cpu"), ("MEMORY_PEAK", "memory"),
                               ("MEMORY_SWAP_PEAK", "swap")):
                raw = entry.get(field)
                if isinstance(raw, str) and raw.isdigit() and int(raw) < (1 << 64) - 1:
                    number = int(raw)
                    value[key] = max(number, value[key] if value[key] is not None else 0)
            if ("CPU_USAGE_NSEC" in entry or "UNIT_RESULT" in entry or
                    message.startswith("Stopped ")):
                stamp = int(entry["__REALTIME_TIMESTAMP"]) / 1e6
                value["end"] = max(stamp, value["end"] or stamp)
    return units


def matches_argv(row, unit, manifest):
    record = manifest.get(row["solver_label"])
    if not record or not unit["argv"]:
        return False
    binary = str(pathlib.Path(record["binary"]).resolve())
    flags = shlex.split(row.get("flags", record.get("flags", "")))
    argv = unit["argv"]
    if binary not in argv:
        return False
    tail = argv[argv.index(binary):]
    return (len(tail) == len(flags) + 3 and tail[:-2] == [binary, *flags]
            and tail[-2] == "-T" and pathlib.Path(tail[-1]).name == row.get("tlsf_file"))


def match_unit(row, units, manifest):
    if row.get("scope_unit"):
        return units.get(row["scope_unit"]), "unit"
    if not row.get("timestamp_utc"):
        return None, "missing"
    timestamp = datetime.datetime.fromisoformat(row["timestamp_utc"].replace("Z", "+00:00")).timestamp()
    matches = [u for u in units.values() if u["end"] is not None
               and abs(u["end"] - timestamp) <= 2 and matches_argv(row, u, manifest)]
    if len(matches) != 1:
        return None, "ambiguous" if matches else "missing"
    return matches[0], "argv+timestamp"


def annotate(row, unit, source):
    out = dict(row)
    out.setdefault("original_result", row["result"])
    out.setdefault("original_resource_reason", row.get("resource_reason", ""))
    out["scope_accounting_source"] = source if unit else "unavailable:" + source
    if not unit:
        return out
    out["scope_unit"] = unit["unit"]
    out["scope_oom_kill"] = str(unit["oom"]).lower()
    if unit["cpu"] is not None:
        out["scope_cpu_seconds"] = f"{unit['cpu'] / 1e9:.9f}"
    if unit["memory"] is not None:
        out["scope_memory_peak_bytes"] = str(unit["memory"])
    if unit["swap"] is not None:
        out["scope_memory_swap_peak_bytes"] = str(unit["swap"])
    if (unit["oom"] and row["result"] not in DECISIVE | {"TIMEOUT"} and
            row.get("timed_out", "false") not in ("true", "1")):
        out["result"], out["resource_reason"] = "MEMOUT", "memory"
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--journal", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("write a separate annotated file; preserve the raw measurements")
    units = read_journal(args.journal)
    manifest = json.loads(args.manifest.read_text()) if args.manifest else {}
    with args.input.open(newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        fields = list(reader.fieldnames or [])
        rows = list(reader)
    used = set()
    annotated = []
    for row in rows:
        unit, source = match_unit(row, units, manifest)
        if unit:
            if unit["unit"] in used:
                raise ValueError("one scope matched multiple measurements")
            used.add(unit["unit"])
        annotated.append(annotate(row, unit, source))
    fields += [k for k in EXTRA + ["resource_reason"] if k not in fields]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(annotated)
    changed = sum(a["result"] != r["result"] for a, r in zip(annotated, rows))
    print(f"matched {len(used)}/{len(rows)} scopes; {changed} OOM classifications corrected; wrote {args.output}")


if __name__ == "__main__":
    main()
