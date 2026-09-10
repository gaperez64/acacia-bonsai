#!/usr/bin/env python3
"""Compare two full-corpus campaigns per instance, regressions first.

Totals hide the thing that decides a backend switch.  A configuration that gains
twelve and loses eleven nets +1 and looks like a wash; one that gains one and
loses nothing is a different proposition entirely, because the losses are what a
default flip would inflict on users while the gains are what it would buy.

This reports, in order:
  1. instances the candidate LOSES that the baseline decides   <- the blocker
  2. instances the candidate GAINS
  3. verdict disagreements on instances both decide            <- correctness
  4. net and totals

The ordering is deliberate: with the forward backend running on a flat vector and
a linear subsumption scan while the backward backend runs on a tuned posets
downset, a small margin either way says less than the absence of regressions does.
The data-structure headroom is larger than the current margin, so "does it lose
anything" is the question that should gate the decision.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from benchlib import par2 as par2_score

import pathlib
import statistics
import sys

DECISIVE = {"REALIZABLE", "UNREALIZABLE"}


def load(path: pathlib.Path) -> dict[str, tuple[str, float]]:
    out: dict[str, tuple[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if row["decisive_result"] in DECISIVE:
                out[row["instance"]] = (row["decisive_result"],
                                        float(row["decisive_seconds"] or 0.0))
    return out


def family(instance: str) -> str:
    stem = instance.removesuffix(".ltl")
    marker = stem.find("_pb_")
    return stem[:marker] if marker != -1 else stem


def load_campaign(path: pathlib.Path, cap: float) -> dict[str, dict]:
    """Accept coverage rows or their summaries; retain every inconclusive row."""
    rows = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            if row.get("cap_s") and float(row["cap_s"]) != cap:
                continue
            name = row["instance"]
            if name in rows:
                raise ValueError(f"{path}: repeated instance {name}; separate repetitions")
            result = row.get("result", row.get("decisive_result")) or "UNKNOWN"
            seconds = float(row.get("seconds", row.get("decisive_seconds")) or 0)
            if "result" not in row and row.get("smallest_cap_solved") and float(row["smallest_cap_solved"]) > cap:
                result, seconds = "UNKNOWN", 0.0
            if not math.isfinite(seconds) or seconds < 0:
                raise ValueError(f"{path}: invalid duration for {name}")
            rows[name] = dict(row, result=result, seconds=seconds)
    if not rows:
        raise ValueError(f"{path}: no rows at cap {cap}")
    return rows


def score(rows: dict[str, dict], cap: float) -> dict:
    answered = [r for r in rows.values() if r["result"] in DECISIVE]
    metrics = dict(total=len(rows), answered=len(answered),
                   real=sum(r["result"] == "REALIZABLE" for r in answered),
                   unreal=sum(r["result"] == "UNREALIZABLE" for r in answered),
                   par2_seconds=par2_score(
                       sum(r["seconds"] for r in answered),
                       len(rows) - len(answered), cap),
                   answered_seconds=sum(r["seconds"] for r in answered))
    for column in ("cpu_seconds", "scope_cpu_seconds", "max_process_rss_bytes", "scope_memory_peak_bytes"):
        values = [float(r[column]) for r in rows.values() if r.get(column) not in (None, "")]
        if any(not math.isfinite(v) or v < 0 for v in values):
            raise ValueError(f"invalid resource measurement in {column}")
        metrics[column + "_observations"] = len(values)
        # Missing/censored measurements are never fabricated as zero.
        metrics[column + ("_sum" if column.endswith("cpu_seconds") else "_max")] = (
            (sum(values) if column.endswith("cpu_seconds") else max(values)) if values else None)
    return metrics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--candidate-label", default="candidate")
    parser.add_argument("--cap", type=float, default=17)
    parser.add_argument("--family-metadata", type=pathlib.Path,
                        help="exact logical_instance/family_key provenance TSV")
    parser.add_argument("--output", type=pathlib.Path, help="per-instance comparison TSV")
    parser.add_argument("--metrics-json", type=pathlib.Path)
    args = parser.parse_args()

    if not math.isfinite(args.cap) or args.cap <= 0:
        parser.error("--cap must be positive")
    try:
        base_rows = load_campaign(args.baseline, args.cap)
        cand_rows = load_campaign(args.candidate, args.cap)
        if base_rows.keys() != cand_rows.keys():
            raise ValueError("campaign instance sets differ; compare the same frozen cohort")
        families = {}
        if args.family_metadata:
            with args.family_metadata.open(newline="") as stream:
                families = {r["logical_instance"]: r["family_key"]
                            for r in csv.DictReader(stream, delimiter="\t")}
            if set(base_rows) - families.keys():
                raise ValueError("family metadata is missing campaign instances")
        base_metrics, cand_metrics = score(base_rows, args.cap), score(cand_rows, args.cap)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    def group(name):
        return families[name] if families else family(name)
    base = {n: (r["result"], r["seconds"]) for n, r in base_rows.items() if r["result"] in DECISIVE}
    cand = {n: (r["result"], r["seconds"]) for n, r in cand_rows.items() if r["result"] in DECISIVE}

    lost = sorted(set(base) - set(cand))
    gained = sorted(set(cand) - set(base))
    both = set(base) & set(cand)
    disagree = sorted(i for i in both if base[i][0] != cand[i][0])

    print(f"{args.baseline_label}: {len(base)} decisive")
    print(f"{args.candidate_label}: {len(cand)} decisive")
    print(f"net: {len(cand) - len(base):+d}\n")

    print(f"=== VERDICT DISAGREEMENTS (must be 0): {len(disagree)} ===")
    for i in disagree:
        print(f"  {i}: {args.baseline_label}={base[i][0]} {args.candidate_label}={cand[i][0]}")

    print(f"\n=== LOST by {args.candidate_label}: {len(lost)} ===")
    fams: dict[str, int] = {}
    for i in lost:
        fams[group(i)] = fams.get(group(i), 0) + 1
    for name, count in sorted(fams.items(), key=lambda kv: -kv[1]):
        print(f"  {name:<34} {count}")

    print(f"\n=== GAINED by {args.candidate_label}: {len(gained)} ===")
    fams = {}
    for i in gained:
        fams[group(i)] = fams.get(group(i), 0) + 1
    for name, count in sorted(fams.items(), key=lambda kv: -kv[1]):
        print(f"  {name:<34} {count}")

    if both:
        faster = sum(1 for i in both if cand[i][1] < base[i][1])
        print(f"\non the {len(both)} both decide: {args.candidate_label} faster on {faster}, "
              f"slower on {len(both) - faster}")
    print(f"PAR-2: {base_metrics['par2_seconds']:.3f}s -> {cand_metrics['par2_seconds']:.3f}s")
    comparison = []
    for name in sorted(base_rows):
        kind = ("conflict" if name in disagree else "lost" if name in lost else
                "gained" if name in gained else "both" if name in both else "neither")
        comparison.append(dict(instance=name, family_key=group(name), classification=kind,
                               baseline_result=base_rows[name]["result"],
                               candidate_result=cand_rows[name]["result"],
                               baseline_seconds=base_rows[name]["seconds"],
                               candidate_seconds=cand_rows[name]["seconds"]))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(comparison[0]), delimiter="\t")
            writer.writeheader()
            writer.writerows(comparison)
    if args.metrics_json:
        ratios = [cand[n][1] / base[n][1] for n in both if n not in disagree and base[n][1] > 0]
        metrics = dict(baseline=base_metrics, candidate=cand_metrics,
                       gains=len(gained), losses=len(lost), conflicts=len(disagree),
                       paired_answers=len(both),
                       median_paired_time_ratio=statistics.median(ratios) if ratios else None)
        args.metrics_json.parent.mkdir(parents=True, exist_ok=True)
        args.metrics_json.write_text(json.dumps(metrics, indent=2) + "\n")
    return 1 if disagree else 0


if __name__ == "__main__":
    sys.exit(main())
