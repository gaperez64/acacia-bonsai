#!/usr/bin/env python3
"""Generate partial per-arm phase, memory, duplication and worker-lifetime tables."""

import argparse
from collections import defaultdict
from pathlib import Path
from prof_lib import analyze_invocation, duplicate_estimates, read_tsv, write_tsv, quantile

D = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rows", type=Path, default=D / "out/PROF.tsv")
    p.add_argument("--panel", type=Path, default=D / "panel.tsv")
    p.add_argument("--phases", type=Path, default=D / "out/phases")
    p.add_argument("--output-dir", type=Path, default=D / "tables/prof")
    a = p.parse_args()
    a.output_dir.mkdir(parents=True, exist_ok=True)
    panel = {r["instance"]: r["stratum"] for r in read_tsv(a.panel)}
    rows = read_tsv(a.rows)
    if len({(r["solver_label"], r["instance"], r["cap_s"], r["run_index"]) for r in rows}) != len(rows):
        p.error("duplicate invocation rows")
    merged = defaultdict(list)
    for row in rows:
        if row["instance"] not in panel:
            p.error("row outside frozen panel: " + row["instance"])
        result = analyze_invocation(dict(row, stratum=panel[row["instance"]]), a.phases)
        for key, value in result.items():
            merged[key].extend([value] if key == "quality" else value)
    schemas = {
        "workers": "record_directory instance solver_label stratum arm worker_pid lifetime_wall_s peak_rss_observed_bytes peak_status dropped_records terminal_observed user_s sys_s self_cpu_s descendant_cpu_s",
        "phases": "record_directory instance solver_label stratum arm worker_pid phase wall_s cpu_s wall_share_of_worker_lifetime completed_scopes status",
        "lifetimes": "record_directory instance solver_label stratum arm worker_pid trigger reason trigger_mono_ns live_until_reaped_s_upper_bound completed_cpu_after_trigger_s_lower_bound continued_phase_observed memory_after_trigger_bytes cpu_boundary_to_reap_s boundary_cpu_resolution_ns cpu_boundary_to_reap_s_lower_bound cpu_boundary_to_reap_s_upper_bound retained_memory_integral_byte_s memory_sampling interpretation",
        "stages": "record_directory instance solver_label stratum arm worker_pid stage stage_id subjob run_id wall_s cpu_s exclusive_wall_s exclusive_cpu_s complete entry_mono_ns end_mono_ns entry_cpu_ns end_process_cpu_ns parent_phase_id",
        "native": "instance worker_pid arm phase wall_s cpu_s status",
        "segments": "record_directory instance event worker_pid phase_id parent_phase_id phase entry_ns entry_cpu_ns mono_ns process_cpu_ns wall_ns cpu_ns dropped_records",
        "identities": "record_directory instance event worker_pid mono_ns subjob run_id phase_id formula_sha256 alphabet_sha256 source_sha256 artifact_sha256 provider translation_options translation_options_sha256 preprocessing_options_sha256",
        "descendants": "record_directory instance event worker_pid descendant_pid mono_ns user_ns sys_ns peak_rss_kb exit_code signal",
        "quality": "instance record_directory missing_directory workers parse_errors dropped_records writer_failed_records writer_summary_present censored_stages unmatched_stage_entries scope_peak_bytes invocation_cpu_s wait4_workers parent_summary_present descendant_censored",
    }
    for name, schema in schemas.items():
        write_tsv(a.output_dir / (name + ".tsv"), merged[name], schema.split())
    duplicate = duplicate_estimates(merged["duplicates"])
    write_tsv(
        a.output_dir / "duplicates.tsv",
        duplicate,
        "instance record_directory worker_pid subjob run_id phase arm group_arms group_cpu_s duplicated_cpu_s_estimate identity status".split(),
    )
    summary = []
    groups = defaultdict(list)
    for row in merged["phases"]:
        groups[("all", row["arm"], row["phase"])].append(row)
        groups[(row["stratum"], row["arm"], row["phase"])].append(row)
    for (stratum, arm, phase), group in sorted(groups.items()):
        instances = {g["instance"] for g in group}
        cpu = sum(g["cpu_s"] or 0 for g in group)
        denom = sum(
            (g["cpu_s"] or 0)
            for g in merged["phases"]
            if g["arm"] == arm and g["instance"] in instances
        )
        final_workers = [
            w for w in merged["workers"] if w["arm"] == arm and w["instance"] in instances
        ]
        final_cpu = (
            sum(w["self_cpu_s"] for w in final_workers)
            if final_workers and all(w["self_cpu_s"] is not None for w in final_workers)
            else None
        )
        summary.append(
            dict(
                stratum=stratum,
                arm=arm,
                phase=phase,
                workers=len(group),
                workers_with_wall=sum(g["wall_s"] is not None for g in group),
                workers_with_cpu=sum(g["cpu_s"] is not None for g in group),
                wall_median_s=quantile([g["wall_s"] for g in group], 0.5),
                wall_p90_s=quantile([g["wall_s"] for g in group], 0.9),
                cpu_median_s=quantile([g["cpu_s"] for g in group], 0.5),
                cpu_p90_s=quantile([g["cpu_s"] for g in group], 0.9),
                median_wall_share_of_worker_lifetime=quantile(
                    [g["wall_share_of_worker_lifetime"] for g in group], 0.5
                ),
                share_of_observed_arm_cpu=(
                    cpu / denom if denom and any(g["cpu_s"] is not None for g in group) else None
                ),
                final_worker_self_cpu_s=final_cpu,
                share_of_final_worker_cpu=(
                    cpu / final_cpu
                    if final_cpu and any(g["cpu_s"] is not None for g in group)
                    else None
                ),
                status="partial_completed_scopes_missing_is_NA",
            )
        )
    write_tsv(
        a.output_dir / "per-arm.tsv",
        summary,
        "stratum arm phase workers workers_with_wall workers_with_cpu wall_median_s wall_p90_s cpu_median_s cpu_p90_s median_wall_share_of_worker_lifetime share_of_observed_arm_cpu final_worker_self_cpu_s share_of_final_worker_cpu status".split(),
    )
    print(
        f"{len(rows)}/{len(panel)} panel inputs analyzed; {sum(x['missing_directory'] for x in merged['quality'])} missing record directories"
    )
    print(
        "See quality.tsv for loss and wait4 coverage. NA is unknown; boundary RSS does not establish a retained-memory integral."
    )


if __name__ == "__main__":
    main()
