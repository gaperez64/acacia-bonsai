#!/usr/bin/env python3
"""Compare one measured N selection leg with both frozen B epochs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import pathlib
import re
import sys
from collections import Counter


ROOT = pathlib.Path(__file__).resolve().parents[3]
CAMPAIGN = pathlib.Path(__file__).resolve().parent
B_SHA256 = "398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a"
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import SOLVED, par2  # noqa: E402


def coverage_module():
    spec = importlib.util.spec_from_file_location(
        "syntcomp26_coverage", ROOT / "benchmarking/run-syntcomp26-coverage.py"
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def score(rows: dict[str, dict[str, str]], ids: list[str], cap: int) -> tuple[int, float]:
    solved = [float(rows[instance]["seconds"]) for instance in ids
              if rows[instance]["result"] in SOLVED]
    return len(solved), par2(sum(solved), len(ids) - len(solved), cap)


def change(base: dict[str, str], candidate: dict[str, str]) -> str:
    base_yes, candidate_yes = base["result"] in SOLVED, candidate["result"] in SOLVED
    if base_yes and candidate_yes and base["result"] != candidate["result"]:
        return "verdict-conflict"
    if candidate_yes and not base_yes:
        return "gain"
    if base_yes and not candidate_yes:
        return "loss"
    return "same"


def route_record(directory: pathlib.Path, instance: str, digest: str) -> dict | None:
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", instance)
    pattern = re.compile(re.escape(safe) + r"\.[0-9a-f]{32}\.json")
    paths = [p for p in directory.glob(f"{safe}.*.json") if pattern.fullmatch(p.name)]
    if len(paths) > 1:
        raise ValueError(f"multiple route records for {instance}: {paths}")
    if not paths:
        return None
    record = json.loads(paths[0].read_text(encoding="utf-8"))
    if record.get("input_sha256") != digest:
        raise ValueError(f"route source hash differs for {instance}: {paths[0]}")
    return record


def write_tsv(path: pathlib.Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cap", required=True, type=int, choices=(17, 60))
    parser.add_argument("--variant", required=True, choices=("N-a", "N-b"))
    parser.add_argument("--selection", type=pathlib.Path,
                        default=CAMPAIGN / "eligible.list")
    parser.add_argument("--campaign-root", type=pathlib.Path,
                        default=CAMPAIGN / "selection")
    args = parser.parse_args()
    coverage = coverage_module()
    ids = coverage.read_instance_list(args.selection)
    if not ids or len(ids) != len(set(ids)):
        raise ValueError("selection list is empty or contains duplicate IDs")
    full_list = ROOT / "tests/suites/benchmarks/syntcomp26/all.list"
    if not set(ids) <= set(coverage.read_instance_list(full_list)):
        raise ValueError("selection contains IDs outside SYNTCOMP26")

    outdir = args.campaign_root / f"{args.cap}s" / args.variant
    raw = outdir / f"{args.variant}-cap{args.cap}.tsv"
    n = coverage.load_uniform_observations(
        raw.with_name(f"{raw.stem}-summary.tsv"), raw, args.selection, args.cap
    )
    if any(row["memory_max"] != "8G" or row["memory_swap_max"] != "0"
           or "winner" not in row for row in n.values()):
        raise ValueError("N selection needs 8G, zero swap, and route-record columns")
    b = {}
    for epoch in (1, 2):
        if args.cap == 60:
            path = (CAMPAIGN / "derived-60s" / f"epoch-{epoch}" /
                    f"B-cap60-epoch{epoch}.tsv")
            source = (ROOT / "benchmarking/witness-lifting-20260918/opening/120s" /
                      f"epoch-{epoch}/B-cap120-epoch{epoch}.tsv")
            provenance = json.loads(path.with_name(
                f"{path.stem}-provenance.json").read_text(encoding="utf-8"))
            if (provenance.get("statement") !=
                "derived by censoring a C_high s observation; not a C_low s run"
                    or provenance.get("C_high_s") != 120
                    or provenance.get("C_low_s") != 60
                    or provenance.get("source_file_sha256") !=
                    hashlib.sha256(source.read_bytes()).hexdigest()):
                raise ValueError(f"B epoch {epoch} lacks 120-to-60 s censoring provenance")
        else:
            path = (ROOT / "benchmarking/witness-lifting-20260918/opening" /
                    f"17s/epoch-{epoch}/B-cap17-epoch{epoch}.tsv")
        b[epoch] = coverage.load_uniform_observations(
            path.with_name(f"{path.stem}-summary.tsv"), path, full_list, args.cap
        )
        if any(row["binary_sha256"] != B_SHA256 or row["memory_max"] != "8G"
               or row["memory_swap_max"] != "0" or row["flags"]
               for row in b[epoch].values()):
            raise ValueError(f"B epoch {epoch} does not match the frozen baseline")

    source_map = coverage.resolve_targets(
        ids, coverage.read_tlsf_map(ROOT / "tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"),
        ROOT / "tlsf-corpus"
    )
    route_dir = outdir / "route-records" / args.variant / str(args.cap)
    rows: list[dict[str, object]] = []
    route_counts: Counter[str] = Counter()
    for instance in ids:
        nr, b1, b2 = n[instance], b[1][instance], b[2][instance]
        n_yes, b_yes = nr["result"] in SOLVED, b1["result"] in SOLVED
        comparison = change(b1, nr)
        digest = hashlib.sha256(source_map[instance][1].read_bytes()).hexdigest()
        route = route_record(route_dir, instance, digest)
        if route is None:
            winner = "missing-record"
        elif route.get("winner") == "lifting":
            if not n_yes:
                raise ValueError(f"lifting winner has no decisive outer result: {instance}")
            winner = "lifting"
        elif route.get("winner") == "fallback-pending":
            winner = "B" if n_yes else "fallback-nonanswer"
        else:
            raise ValueError(f"unknown route winner for {instance}: {route.get('winner')!r}")
        route_counts[winner] += 1
        rows.append(dict(instance=instance, n_result=nr["result"],
                         n_seconds=nr["seconds"], b_epoch1_result=b1["result"],
                         b_epoch1_seconds=b1["seconds"], b_epoch2_result=b2["result"],
                         b_epoch2_seconds=b2["seconds"],
                         change_vs_b_epoch1=comparison,
                         b_epoch2_vs_epoch1=change(b1, b2),
                         seconds_delta_vs_b_epoch1=(
                             f"{float(nr['seconds']) - float(b1['seconds']):.6f}"
                             if n_yes and b_yes and comparison == "same" else ""),
                         memory_peak_delta_bytes=(
                             int(nr["scope_memory_peak_bytes"]) - int(b1["scope_memory_peak_bytes"])
                             if nr["scope_memory_peak_bytes"] and b1["scope_memory_peak_bytes"]
                             else ""),
                         winner=winner))

    write_tsv(outdir / "comparison.tsv", rows)
    counts = Counter(row["change_vs_b_epoch1"] for row in rows)
    lines = [f"# {args.variant} at {args.cap} s, eligible subset ({len(ids)} IDs)", "",
             "| Series | Solved | PAR-2 total (s) | PAR-2 mean (s) |",
             "|---|---:|---:|---:|"]
    b_suffix = " (derived from 120 s)" if args.cap == 60 else ""
    for name, data in ((args.variant, n), (f"B epoch 1{b_suffix}", b[1]),
                       (f"B epoch 2{b_suffix}", b[2])):
        solved, total = score(data, ids, args.cap)
        lines.append(f"| {name} | {solved} | {total:.6f} | {total/len(ids):.6f} |")
    b_repeatability = Counter(row["b_epoch2_vs_epoch1"] for row in rows)
    lines += ["", "N versus B epoch 1: " + ", ".join(
        f"{name}={counts[name]}" for name in ("gain", "loss", "same", "verdict-conflict")),
        "B epoch 2 versus epoch 1: " + ", ".join(
            f"{name}={b_repeatability[name]}" for name in
            ("gain", "loss", "same", "verdict-conflict")),
        "", "Route winners: " + ", ".join(
            f"{name}={route_counts[name]}" for name in
            ("lifting", "B", "fallback-nonanswer", "missing-record")), "",
        "Per-ID results and gains/losses: `comparison.tsv`. Missing records can occur when the outer scope kills the wrapper before its atomic write.", ""]
    if args.cap == 60:
        lines += ["B 60 s rows are derived by censoring the archived uniform 120 s epochs; "
                  "they are not 60 s solver runs.", ""]
    (outdir / "comparison.md").write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
