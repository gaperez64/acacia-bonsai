#!/usr/bin/env python3
"""Summarize opt-in P1 phase JSONL beside one coverage-runner TSV."""
import argparse
import csv
import json
import re
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def read_tsv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader((line for line in stream if not line.startswith("#")),
                                   delimiter="\t"))


def write_tsv(path, columns, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def median(values):
    return round(statistics.median(values), 6) if values else ""


def number(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--panel", type=Path, required=True)
    parser.add_argument("--runs", type=Path, required=True)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--out-prefix", type=Path, required=True)
    args = parser.parse_args()
    panel = {row["instance"]: row["category"] for row in read_tsv(args.panel)}
    runs = read_tsv(args.runs)
    directory_to_instance = {
        f"{re.sub(r'[^A-Za-z0-9_.-]', '_', row['instance'])}-{row['run_index']}": row["instance"]
        for row in runs
    }
    grouped_runs = defaultdict(list)
    for row in runs:
        if row["instance"] in panel:
            grouped_runs[panel[row["instance"]]].append(row)
    run_summary = []
    for category, rows in sorted(grouped_runs.items()):
        unknown = [r for r in rows if r["result"] == "UNKNOWN"]
        run_summary.append({
            "category": category, "n": len(rows),
            "results": ",".join(f"{k}:{v}" for k, v in sorted(Counter(
                r["result"] for r in rows).items())),
            "median_wall_s": median([number(r["seconds"]) for r in rows]),
            "median_cpu_s": median([number(r["cpu_seconds"]) for r in rows
                                    if r["cpu_seconds"]]),
            "cpu_observed_n": sum(bool(r["cpu_seconds"]) for r in rows),
            "unknown_core_seconds_lower_bound": round(sum(number(r["cpu_seconds"])
                                                           for r in unknown), 6),
            "unknown_cpu_observed_n": sum(bool(r["cpu_seconds"]) for r in unknown),
            "median_unknown_cpu_s": median([number(r["cpu_seconds"]) for r in unknown
                                            if r["cpu_seconds"]]),
            "max_process_rss_mib": round(max(number(r["max_process_rss_bytes"])
                                               for r in rows) / 1048576, 3),
            "max_scope_peak_mib": round(max(number(r["scope_memory_peak_bytes"])
                                              for r in rows) / 1048576, 3),
        })
    records = defaultdict(list)
    record_drops = 0
    for path in args.records.glob("*/60/*/*.jsonl"):
        instance = directory_to_instance.get(path.parent.name)
        if instance not in panel:
            continue
        for line in path.read_text().splitlines():
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if row.get("phase") == "record_summary":
                record_drops += row.get("dropped_records", 0)
                continue
            records[(panel[instance], row["arm"], row["phase"], instance)].append(row)
    phase_groups = defaultdict(list)
    for (category, arm, phase, _instance), events in records.items():
        combined = dict(events[-1])
        combined["wall_ns"] = sum(r["wall_ns"] for r in events)
        combined["cpu_ns"] = sum(r["cpu_ns"] for r in events)
        combined["peak_rss_kb"] = max(r["peak_rss_kb"] for r in events)
        combined["bdd_nodes"] = max(r["bdd_nodes"] for r in events)
        combined["work_count"] = sum(r["work_count"] for r in events)
        combined["size_bytes"] = max(r["size_bytes"] for r in events)
        combined["monitor_count"] = max(r["monitor_count"] for r in events)
        combined["monitor_states"] = max(r["monitor_states"] for r in events)
        phase_groups[(category, arm, phase)].append(combined)
    phases = []
    for (category, arm, phase), rows in sorted(phase_groups.items()):
        phases.append({
            "category": category, "arm": arm, "phase": phase, "n": len(rows),
            "median_wall_s": median([r["wall_ns"] / 1e9 for r in rows]),
            "median_cpu_s": median([r["cpu_ns"] / 1e9 for r in rows]),
            "median_rss_mib": median([r["rss_kb"] / 1024 for r in rows
                                      if r["rss_kb"] >= 0]),
            "max_peak_rss_mib": round(max(r["peak_rss_kb"] for r in rows) / 1024, 3),
            "median_live_heap_mib": median([r["mallinfo2"]["uordblks"] / 1048576
                                            for r in rows]),
            "max_bdd_nodes": max(r["bdd_nodes"] for r in rows),
            "sum_work_count": sum(r["work_count"] for r in rows),
            "max_size_bytes": max(r["size_bytes"] for r in rows),
            "max_monitor_count": max(r["monitor_count"] for r in rows),
            "max_monitor_states": max(r["monitor_states"] for r in rows),
        })
    write_tsv(args.out_prefix.with_name(args.out_prefix.name + "-runs.tsv"),
              list(run_summary[0]) if run_summary else [], run_summary)
    write_tsv(args.out_prefix.with_name(args.out_prefix.name + "-phases.tsv"),
              list(phases[0]) if phases else [], phases)
    print(f"runs={sum(map(len, grouped_runs.values()))} phase_records={sum(map(len, records.values()))} reported_drops={record_drops}")


if __name__ == "__main__":
    main()
