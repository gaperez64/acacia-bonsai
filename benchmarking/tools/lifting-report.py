#!/usr/bin/env python3
"""Summarize one lifting arm's seed and target work from campaign phase records."""

from __future__ import annotations

import argparse
import collections
import csv
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_phase_records, phase_record_dir  # noqa: E402


def load_coverage():
    path = ROOT / "benchmarking/run-syntcomp26-coverage.py"
    spec = importlib.util.spec_from_file_location("coverage_p4_lifting", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


coverage = load_coverage()
SEED_PHASES = frozenset({"lift_seed_window", "lift_seed_solve", "lift_schema_learning",
                         "lift_candidate_instantiation", "lift_policy_export",
                         "lift_internal_check"})
TARGET_PHASES = frozenset({"lift_source", "lift_target_reduce", "game_rereduction",
                           "proof_binding", "outer_check"})
SOLVED = frozenset({"REALIZABLE", "UNREALIZABLE"})


def summarize_row(row, records, arm, *, single_arm_leg=False, seed_phases=SEED_PHASES,
                  target_phases=TARGET_PHASES, method_prefix="lift_method_",
                  outer_check_phase="outer_check"):
    processes = records.arm_processes(arm)
    events = [event for process in processes for event in process.events]
    phases = collections.defaultdict(list)
    for event in events:
        phases[str(event.get("phase", ""))].append(event)
    method_phases = [name for name in phases if name.startswith(method_prefix)]
    declines = []
    for event in events:
        phase = str(event.get("phase", ""))
        if phase == "budget_decline":
            declines.append(f"budget_decline:{event.get('stage', '')}")
        elif "_decline_" in phase:
            declines.append(phase)

    def totals(names):
        relevant = [event for name in names for event in phases[name]]
        return {
            "wall_ns": sum(int(event.get("wall_ns", 0)) for event in relevant),
            "cpu_ns": sum(int(event.get("cpu_ns", 0)) for event in relevant),
            "work": sum(int(event.get("work_count", 0)) for event in relevant),
        }

    method = ",".join(sorted(method_phases))
    target_checked = bool(phases[outer_check_phase])
    basis = ("single-arm leg" if single_arm_leg else
             "route record" if row.get("winner") == "lifting" else "unattributed race")
    attributed = (row["result"] in SOLVED and bool(method) and target_checked and
                  basis != "unattributed race")
    return {
        "instance": row["instance"], "outcome": row["result"],
        "provenance": row.get("provenance", "measured"),
        "arm": arm, "record_processes": len(processes),
        "records_complete": bool(processes) and records.dropped == 0 and
        all(process.finished for process in processes),
        "seed": totals(seed_phases), "target": totals(target_phases),
        "phase_work": {name: sum(int(event.get("work_count", 0)) for event in values)
                       for name, values in sorted(phases.items())},
        "seed_probes": sum(int(event.get("work_count", 0)) for name in seed_phases
                           if "seed_window" in name for event in phases[name]),
        "seed_solves": sum(len(phases[name]) for name in seed_phases if "seed_solve" in name),
        "check_method": method, "declines": declines,
        "target_checked": target_checked, "target_solve_attributed": attributed,
        "attribution_basis": basis,
    }


def report(rows, records_root, arm, *, single_arm_leg=False, short_records_root=None,
           seed_phases=SEED_PHASES, target_phases=TARGET_PHASES,
           method_prefix="lift_method_", outer_check_phase="outer_check"):
    if set(seed_phases) & set(target_phases):
        raise ValueError("seed and target phase groups must be disjoint")
    seen = set()
    detail = []
    for row in rows:
        name = row["instance"]
        if name in seen:
            raise ValueError(f"duplicate instance {name}")
        seen.add(name)
        provenance = row.get("provenance", "")
        recycled = provenance == f"recycled-{row.get('source_cap_s', '')}s"
        if provenance and not (recycled and int(row["source_cap_s"]) < int(row["cap_s"]) or
                               provenance == f"measured-{row['cap_s']}s" and
                               row.get("source_cap_s") == row["cap_s"]):
            raise ValueError(f"{name}: invalid derived provenance {provenance!r}")
        root = short_records_root if recycled and short_records_root else records_root
        directory = phase_record_dir(root, row["solver_label"],
                                     row["source_cap_s"] if recycled else row["cap_s"],
                                     name, row["run_index"])
        records = load_phase_records(directory)
        detail.append(summarize_row(row, records, arm, single_arm_leg=single_arm_leg,
                                    seed_phases=seed_phases, target_phases=target_phases,
                                    method_prefix=method_prefix,
                                    outer_check_phase=outer_check_phase))
    methods = collections.Counter(item["check_method"] or "none" for item in detail)
    declines = collections.Counter(kind for item in detail for kind in item["declines"])
    attributed = [item["instance"] for item in detail if item["target_solve_attributed"]]
    summary = {
        "arm": arm, "rows": len(detail),
        "rows_with_arm_records": sum(item["record_processes"] > 0 for item in detail),
        "rows_with_complete_arm_records": sum(item["records_complete"] for item in detail),
        "seed_wall_s": sum(item["seed"]["wall_ns"] for item in detail) / 1e9,
        "seed_cpu_s": sum(item["seed"]["cpu_ns"] for item in detail) / 1e9,
        "seed_work": sum(item["seed"]["work"] for item in detail),
        "seed_probes": sum(item["seed_probes"] for item in detail),
        "seed_solves": sum(item["seed_solves"] for item in detail),
        "target_wall_s": sum(item["target"]["wall_ns"] for item in detail) / 1e9,
        "target_cpu_s": sum(item["target"]["cpu_ns"] for item in detail) / 1e9,
        "target_work": sum(item["target"]["work"] for item in detail),
        "check_methods": dict(sorted(methods.items())),
        "declines": dict(sorted(declines.items())),
        "target_solve_attributed": attributed,
        "unattributed_solves": [item["instance"] for item in detail
                                if item["outcome"] in SOLVED and
                                not item["target_solve_attributed"]],
        "derived_rows": sum(item["provenance"].startswith("recycled-") for item in detail),
        "unclassified_phase_work": sum(
            work for item in detail for phase, work in item["phase_work"].items()
            if phase not in seed_phases and phase not in target_phases),
    }
    return summary, detail


def write_report(summary, detail, out):
    out.mkdir(parents=True, exist_ok=True)
    (out / "lifting.json").write_text(json.dumps({"summary": summary, "rows": detail},
                                                   indent=2, sort_keys=True) + "\n")
    columns = ("instance", "outcome", "provenance", "record_processes",
               "records_complete", "seed_probes", "seed_solves", "seed_wall_ns",
               "seed_work", "target_wall_ns", "target_work", "check_method",
               "declines", "target_checked", "target_solve_attributed",
               "attribution_basis")
    with (out / "lifting.tsv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for item in detail:
            writer.writerow({**item, "seed_wall_ns": item["seed"]["wall_ns"],
                             "seed_work": item["seed"]["work"],
                             "target_wall_ns": item["target"]["wall_ns"],
                             "target_work": item["target"]["work"],
                             "declines": ";".join(item["declines"])})
    lines = [f"# Lifting arm: {summary['arm']}", "",
             f"Rows: {summary['rows']} ({summary['derived_rows']} derived); "
             f"complete arm records: {summary['rows_with_complete_arm_records']}.", "",
             f"Seed: {summary['seed_wall_s']:.6f} wall s, {summary['seed_cpu_s']:.6f} "
             f"CPU s, {summary['seed_work']} work units, {summary['seed_probes']} probes, "
             f"{summary['seed_solves']} solves.",
             f"Target: {summary['target_wall_s']:.6f} wall s, "
             f"{summary['target_cpu_s']:.6f} CPU s, {summary['target_work']} work units.",
             f"Unclassified phase work: {summary['unclassified_phase_work']} work units "
             "(inspect lifting.json and supply phase groups if needed).",
             f"Checked target solves attributed: {len(summary['target_solve_attributed'])}.",
             f"Unattributed solved rows: {len(summary['unattributed_solves'])}.", "",
             "## Check methods", ""]
    lines += [f"- {name}: {count}" for name, count in summary["check_methods"].items()]
    lines += ["", "## Declines", ""]
    lines += [f"- {name}: {count}" for name, count in summary["declines"].items()]
    lines += ["", "Work units are the phase-record work field summed within each group; "
              "the per-instance JSON retains every count. Missing or interrupted records "
              "are disclosed, not imputed. A race solve is attributed to this arm only "
              "when a route record names lifting; standalone attribution requires "
              "--single-arm-leg."]
    (out / "lifting.md").write_text("\n".join(lines) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", required=True, type=pathlib.Path)
    parser.add_argument("--records", required=True, type=pathlib.Path)
    parser.add_argument("--short-records", type=pathlib.Path,
                        help="short-cap phase records for recycled rows, if stored separately")
    parser.add_argument("--arm", required=True)
    parser.add_argument("--seed-phase", action="append",
                        help="override the default seed phase group; repeatable")
    parser.add_argument("--target-phase", action="append",
                        help="override the default target phase group; repeatable")
    parser.add_argument("--method-prefix", default="lift_method_")
    parser.add_argument("--outer-check-phase", default="outer_check")
    parser.add_argument("--single-arm-leg", action="store_true",
                        help="the TSV was measured with only this arm enabled")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        rows = coverage.load_output(args.runs)
        summary, detail = report(rows, args.records, args.arm,
                                 single_arm_leg=args.single_arm_leg,
                                 short_records_root=args.short_records,
                                 seed_phases=args.seed_phase or SEED_PHASES,
                                 target_phases=args.target_phase or TARGET_PHASES,
                                 method_prefix=args.method_prefix,
                                 outer_check_phase=args.outer_check_phase)
        write_report(summary, detail, args.out)
    except (OSError, ValueError, coverage.CoverageError) as error:
        parser.error(str(error))
    print((args.out / "lifting.md").read_text(), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
