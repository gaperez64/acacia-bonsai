#!/usr/bin/env python3
"""Assemble M6 sidecars and exact-instance inputs for the existing reporters.

This script does no scoring.  It validates the campaign target set, writes the
per-instance TSV/CSV, and subsets the saved W1 observations so that
``export-cactus`` and ``cactus-report.py`` can score exactly the same rows.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
M0 = HERE / "m0-instances.tsv"
DEFAULT_EVIDENCE = ROOT / "build_scratch" / "param-lift-m6-campaign" / "evidence"
W1 = ROOT / "benchmarking" / "witness-lifting-20260918" / "opening" / "120s"

REAL_FAMILIES = frozenset((
    "arbiter", "prioritized_arbiter", "load_balancer", "arbiter_with_cancel",
    "collector_v1", "arbiter_with_buffer", "simple_arbiter_with_hints",
    "amba_decomposed_lock", "abcg_arbiter", "arbiter_on_inpchange",
))
UNREAL_FAMILIES = frozenset((
    "round_robin_arbiter_unreal2", "prioritized_arbiter_unreal2",
    "load_balancer_unreal2", "amba_case_study_unreal",
))

CAMPAIGN_COLUMNS = (
    "side", "family", "n", "logical_instance", "census_class",
    "measured_arity", "seeds_used", "path_kind", "stage_reached", "verdict",
    "target_verified", "seed_monitor_s", "seed_solve_s", "generalization_s",
    "instantiation_s", "target_monitor_s", "target_solve_s", "target_check_s",
    "probe_check_s", "driver_overhead_s", "wrapper_overhead_s", "cold_total_s",
    "within_120s", "peak_rss_kib", "reason", "evidence",
)


def read_tsv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        return list(reader.fieldnames or ()), list(reader)


def write_tsv(path: Path, columns: list[str] | tuple[str, ...], rows) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def target_rows() -> list[dict[str, str]]:
    _columns, rows = read_tsv(M0)
    families = REAL_FAMILIES | UNREAL_FAMILIES
    selected = [row for row in rows
                if row["family_display"] in families and row["status_120s"] == "unsolved"]
    selected.sort(key=lambda row: (row["family_display"],
                                   int(json.loads(row["parameters"])["n"])))
    return selected


def reported_stage(payload: dict) -> str:
    stage = payload["result"]["stage"]
    if stage == "CEGIS" and "tlsfcertcheck" in payload["result"]["reason"]:
        return "target_check"
    if stage != "generalize_gr1":
        return stage
    progress = payload.get("generalizer_progress") or {}
    if progress.get("active_stage"):
        return progress["active_stage"]
    costs = progress.get("cost_times") or {}
    if (costs.get("target_check") or costs.get("target_solve") or
            costs.get("probe_check") or costs.get("stage_instantiate")):
        return "target_check"
    if costs.get("stage_ranks"):
        return "instantiate"
    if costs.get("stage_anti_unify"):
        return "ranks"
    return stage


def assemble(evidence_dir: Path, output: Path, comparison_dir: Path) -> None:
    targets = target_rows()
    expected = {(row["family_display"], int(json.loads(row["parameters"])["n"])): row
                for row in targets}
    evidence_by_key = {}
    for path in sorted(evidence_dir.glob("*.json")):
        payload = json.loads(path.read_text(encoding="utf-8"))
        request = payload.get("request") or {}
        key = (request.get("family"), request.get("target"))
        if key not in expected:
            raise ValueError(f"unexpected campaign sidecar {path}: {key}")
        if key in evidence_by_key:
            raise ValueError(f"duplicate campaign sidecar for {key}")
        evidence_by_key[key] = (path.resolve(), payload)
    missing = sorted(set(expected) - set(evidence_by_key))
    if missing:
        raise ValueError(f"missing campaign sidecars: {missing}")

    rows = []
    for key, target in expected.items():
        path, payload = evidence_by_key[key]
        request = payload["request"]
        result = payload["result"]
        costs = payload["cost_accounting"]
        side = "REAL" if key[0] in REAL_FAMILIES else "UNREAL"
        decisive = result["verdict"] in ("REALIZABLE", "UNREALIZABLE")
        verified = payload.get("target_verified") is True
        if decisive != verified:
            raise ValueError(f"{path}: decisive/VERIFIED invariant disagrees")
        if request["status_120s"] != "unsolved" or target["status_120s"] != "unsolved":
            raise ValueError(f"{path}: campaign contains a previously solved target")
        rows.append({
            "side": side, "family": key[0], "n": key[1],
            "logical_instance": target["logical_instance"],
            "census_class": request["census_class"],
            "measured_arity": (request["measured_arity"]
                               if request["measured_arity"] is not None else "not-measured"),
            "seeds_used": ",".join(map(str, request["seeds"])) or "-",
            "path_kind": payload["path_kind"], "stage_reached": reported_stage(payload),
            "verdict": result["verdict"], "target_verified": str(verified).lower(),
            "seed_monitor_s": f"{costs['seed_monitor_s']:.6f}",
            "seed_solve_s": f"{costs['seed_solve_s']:.6f}",
            "generalization_s": f"{costs['generalization_s']:.6f}",
            "instantiation_s": f"{costs['instantiation_s']:.6f}",
            "target_monitor_s": f"{costs['target_monitor_s']:.6f}",
            "target_solve_s": f"{costs['target_solve_s']:.6f}",
            "target_check_s": f"{costs['target_check_s']:.6f}",
            "probe_check_s": f"{costs['probe_check_s']:.6f}",
            "driver_overhead_s": f"{costs['driver_overhead_s']:.6f}",
            "wrapper_overhead_s": f"{costs['wrapper_overhead_s']:.6f}",
            "cold_total_s": f"{costs['cold_total_s']:.6f}",
            "within_120s": str(decisive and costs["cold_total_s"] <= 120.0).lower(),
            "peak_rss_kib": payload["peak_rss_kib"], "reason": result["reason"],
            "evidence": str(path),
        })
    write_tsv(output, CAMPAIGN_COLUMNS, rows)

    baseline_sources = {
        "B": (W1 / "epoch-1" / "B-cap120-epoch1-summary.tsv",
              W1 / "epoch-1" / "B-cap120-epoch1.tsv"),
        "S": (W1 / "epoch-1" / "S-cap120-epoch1-summary.tsv",
              W1 / "epoch-1" / "S-cap120-epoch1.tsv"),
    }
    with (W1 / "ltlsynt-cap120.csv").open(encoding="utf-8", newline="") as stream:
        ltl_rows = {row["instance"]: row for row in csv.DictReader(stream)}

    for side in ("REAL", "UNREAL"):
        side_dir = comparison_dir / side.lower()
        side_rows = [row for row in rows if row["side"] == side]
        names = [row["logical_instance"] for row in side_rows]
        wanted = set(names)
        side_dir.mkdir(parents=True, exist_ok=True)
        (side_dir / "targets.list").write_text("\n".join(names) + "\n", encoding="utf-8")
        with (side_dir / "campaign.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(("instance", "result", "seconds", "exit"))
            for row in side_rows:
                solved = row["verdict"] in ("REALIZABLE", "UNREALIZABLE")
                writer.writerow((row["logical_instance"], row["verdict"] if solved else "UNKNOWN",
                                 row["cold_total_s"] if solved else "120",
                                 {"REALIZABLE": 0, "UNREALIZABLE": 1}.get(row["verdict"], 2)))
        with (side_dir / "ltlsynt.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=("instance", "result", "seconds", "exit"))
            writer.writeheader()
            writer.writerows(ltl_rows[name] for name in names)
        for label, (summary_path, runs_path) in baseline_sources.items():
            summary_columns, summary_rows = read_tsv(summary_path)
            runs_columns, run_rows = read_tsv(runs_path)
            write_tsv(side_dir / f"{label}-summary.tsv", summary_columns,
                      (row for row in summary_rows if row["instance"] in wanted))
            write_tsv(side_dir / f"{label}.tsv", runs_columns,
                      (row for row in run_rows if row["instance"] in wanted))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument("--output", type=Path, default=HERE / "m6-campaign.tsv")
    parser.add_argument("--comparison-dir", type=Path,
                        default=HERE / "m6-comparison-inputs")
    args = parser.parse_args()
    assemble(args.evidence_dir.resolve(), args.output.resolve(),
             args.comparison_dir.resolve())
    print(f"wrote {args.output}")
    print(f"wrote {args.comparison_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
