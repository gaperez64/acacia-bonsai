#!/usr/bin/env python3
"""How much of the frozen eager graph does the current solver actually demand?

P1 / C1 records active SOURCE-row support during search and verification without
changing C0's graph, domain or decisions. Ratios use the worker's union across K;
they measure utilization of an optimized graph, not possible translation savings.
Each instance streams one row per worker (or a failure row if none reported).
Translation failures have absent graph sizes, demand numerators and ratios.

Example:

    benchmarking/spot-demand-campaign.py \\
        --build build_sprint_diag --preset best_decomp_mona \\
        --suite syntcomp24=tests/suites/benchmarks/syntcomp24/regress.list \\
        --source-map syntcomp24=tests/suites/benchmarks/syntcomp24/sources.tsv \\
        --flags='--real-backend forward --unreal-backend forward' \\
        --out benchmarking/spot-demand.tsv --timeout 25

--targets accepts select-frontier-targets.py's TSV, with --target-suite naming
its suite. --include-neighbours adds the selected targets' solved controls.
Use a separate output and a family-excluded targets TSV for the held-out cohort.
Action IDs are deterministic flat input/action ordinals within each worker.
Support median averages the middle two samples; p95 uses nearest rank over
actual applications (search + verify). Backward demand covers forward picker
and certificate checks only, not the different row footprint of CPre preimages.
Empty values mean unavailable; an empty edge denominator has no defined ratio.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import importlib.util
import os
import pathlib
import shlex
import subprocess
import sys
from dataclasses import replace

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from benchlib import campaign_scope_guard, read_part, run_systemd_scope  # noqa: E402
from benchlib import classify_run, verdict_from_output  # noqa: E402
from suite_paths import load_source_map, load_tlsf_source_map  # noqa: E402


def load_run_diag_targets():
    spec = importlib.util.spec_from_file_location(
        "run_diag_targets", HERE / "run_diag_targets.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


DIAG_COLUMNS = {
    "formula_fnv1a64": "support_formula_fnv1a64",
    "backend": "support_backend",
    "phase": "support_phase",
    "k_schedule": "k_schedule",
    "translation_pref": "translation_pref",
    "translation_ms": "translation_ms",
    "preprocessing_ms": "preproc_ms",
    "action_construction_ms": "support_action_construction_ms",
    "solve_ms": "solve_ms",
    "verification_ms": "support_verification_ms",
    "total_ms": "total_ms",
    "graph_ready": "support_graph_ready",
    "total_q": "support_graph_states",
    "total_e": "support_graph_edges",
    "search_rows": "support_search_rows",
    "verification_rows": "support_verification_rows",
    "verification_only_rows": "support_verification_only_rows",
    "union_rows": "support_union_rows",
    "union_edges": "support_union_edges",
    "k": "support_k",
    "k_union_rows": "support_k_union_rows",
    "k_union_edges": "support_k_union_edges",
    "search_applications": "support_search_applications",
    "verification_applications": "support_verification_applications",
    "support_median": "support_median",
    "support_p95": "support_p95",
    "support_max": "support_max",
    "decoded_profiles": "decoded_transition_sets",
    "action_profiles_used": "support_action_profiles_used",
    "action_profile_ids": "support_action_profile_ids",
    "forward_env_nodes": "forward_env_nodes",
    "forward_ctrl_nodes": "forward_ctrl_nodes",
    "actions_seen": "actions_seen",
    "max_f": "max_f",
    "forward_limit_reason": "forward_resource_reason",
    "worker_result": "result",
    "final_reason": "final_reason",
}

COLUMNS = [
    "suite", "instance", "family", "cohort", "worker", "pid", "route",
    "source_sha256", "options", "checkpoint",
] + list(DIAG_COLUMNS) + [
    "rho_q", "rho_e", "result", "seconds", "exit_code", "timed_out",
    "acacia_sha", "binary_sha256", "preset", "timestamp_utc", "status",
]

DEMAND_COLUMNS = {
    "total_q", "total_e", "search_rows", "verification_rows",
    "verification_only_rows", "union_rows", "union_edges", "k", "k_union_rows",
    "k_union_edges", "search_applications", "verification_applications",
    "support_median", "support_p95", "support_max", "action_profiles_used",
    "action_profile_ids", "rho_q", "rho_e",
}


def read_instance_list(path: pathlib.Path) -> list[str]:
    return [line for raw in path.read_text().splitlines()
            if (line := raw.split("#", 1)[0].strip())]


def parse_pairs(values: list[str], what: str) -> dict[str, pathlib.Path]:
    pairs = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"{what} must be SUITE=PATH, got {value!r}")
        suite, path = value.split("=", 1)
        if not suite or not path:
            raise ValueError(f"{what} must have a nonempty suite and path")
        pairs[suite] = pathlib.Path(path)
    return pairs


def target_cohort(path: pathlib.Path, neighbours: bool) -> dict[str, str]:
    """Map instance to family, preserving selector order and deduplicating controls."""
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source, delimiter="\t")
        if not {"instance", "family_key"} <= set(reader.fieldnames or []):
            raise ValueError(f"{path} needs instance and family_key columns")
        result = {}
        for row in reader:
            result[row["instance"]] = row["family_key"]
            if neighbours and row.get("neighbour_instance"):
                result[row["neighbour_instance"]] = row["family_key"]
        return result


def latest_workers(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    """Terminal snapshots win over speculative progress, including rollback.

    DiagnosticAccumulator groups progress by checkpoint rather than arrival, so
    select by elapsed time and monotone application/loop counters on killed runs.
    A completed parent and an unfinished child must remain separate workers.
    """
    workers = {}
    def key(row):
        return (row.get("diag_kind") == "final",
                float(row.get("total_ms", "0")),
                int(row.get("support_search_applications", "0"))
                + int(row.get("support_verification_applications", "0")),
                int(row.get("loops", "0")),
                row.get("support_graph_ready") == "1",
                int(row.get("aut_states", "0")),
                {"translation": 1, "preprocessing": 2, "action-construction": 3,
                 "search": 4, "preimage-search": 4}.get(row.get("support_phase"), 0))
    for row in rows:
        worker = (row.get("pid", ""), row.get("path", "unknown"))
        if worker not in workers or key(row) >= key(workers[worker]):
            workers[worker] = row
    return [workers[worker] for worker in sorted(workers)]


def ratio(numerator: str, denominator: str) -> str:
    if numerator in {"", "-"} or denominator in {"", "-", "0"}:
        return ""
    return format(int(numerator) / int(denominator), ".9g")


def demand_row(diag: dict[str, str], result: str) -> dict[str, str]:
    """Build a worker measurement; never convert missing translation to zero use."""
    row = {column: diag.get(field, "") for column, field in DIAG_COLUMNS.items()}
    row.update(worker=diag.get("path", "none"), pid=diag.get("pid", ""),
               checkpoint=diag.get("checkpoint", ""), result=result)
    if not row["backend"] and diag.get("forward_backend") == "1":
        row["backend"] = "forward"
    completed = diag.get("diag_kind") == "final"
    ready = diag.get("support_graph_ready") == "1"
    if ready:
        row["rho_q"] = ratio(row["union_rows"], row["total_q"])
        row["rho_e"] = ratio(row["union_edges"], row["total_e"])
        if row["search_applications"] in {"", "0"} and row["verification_applications"] in {"", "0"}:
            # No rank samples: percentile zero would imply an empty-support sample.
            for column in ("support_median", "support_p95", "support_max"):
                row[column] = ""
        row["status"] = ("complete" if diag.get("result") == "solved" else "unknown") if completed else result.lower()
        if not completed and result in {"REALIZABLE", "UNREALIZABLE"}:
            row["status"] = "cancelled-worker"
    else:
        for column in DEMAND_COLUMNS:
            row[column] = ""
        if not completed and result in {"REALIZABLE", "UNREALIZABLE"} and diag:
            row["status"] = "cancelled-worker"
        elif diag.get("support_phase") == "translation" or diag.get("checkpoint") == "support-before-translation":
            row["status"] = "translation-failure-" + result.lower()
        elif int(diag.get("aut_states", "0")) > 0:
            row["status"] = "no-demand-fast-path" if completed and diag.get("result") == "solved" else "preprocessing-" + result.lower()
        else:
            row["status"] = "no-demand" if completed else "no-diagnostics-" + result.lower()
    return row


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def provenance(binary: pathlib.Path, preset: str) -> dict[str, str]:
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=HERE.parent,
                              capture_output=True, text=True, check=False)
    return {"acacia_sha": revision.stdout.strip() if revision.returncode == 0 else "",
            "binary_sha256": sha256_file(binary), "preset": preset}


@campaign_scope_guard("spot-demand-campaign")
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", required=True)
    parser.add_argument("--suite", action="append", default=[], metavar="SUITE=LIST")
    parser.add_argument("--source-map", action="append", default=[], metavar="SUITE=PATH")
    parser.add_argument("--tlsf-source-map", action="append", default=[], metavar="SUITE=PATH")
    parser.add_argument("--tlsf-corpus", type=pathlib.Path)
    parser.add_argument("--targets", type=pathlib.Path)
    parser.add_argument("--target-suite", default="syntcomp26")
    parser.add_argument("--include-neighbours", action="store_true")
    parser.add_argument("--cohort", default="discovery")
    parser.add_argument("--preset", default="")
    parser.add_argument("--flags", default="", help="exact extra solver options (shell syntax)")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=25.0)
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    runner = load_run_diag_targets()
    binary = (pathlib.Path(args.build) / "src" / "acacia-bonsai").resolve()
    if not binary.is_file():
        parser.error(f"no diagnostics binary at {binary}")
    if not args.suite and not args.targets:
        parser.error("give --suite SUITE=LIST or --targets TSV")
    try:
        cohorts = {suite: dict.fromkeys(read_instance_list(path), "")
                   for suite, path in parse_pairs(args.suite, "--suite").items()}
        if args.targets:
            cohorts.setdefault(args.target_suite, {}).update(
                target_cohort(args.targets, args.include_neighbours))
        source_maps = {suite: load_source_map(path)
                       for suite, path in parse_pairs(args.source_map, "--source-map").items()}
        tlsf_pairs = parse_pairs(args.tlsf_source_map, "--tlsf-source-map")
        if tlsf_pairs and not args.tlsf_corpus:
            parser.error("--tlsf-source-map needs --tlsf-corpus")
        tlsf_maps = {suite: load_tlsf_source_map(path, tlsf_corpus=args.tlsf_corpus)
                     for suite, path in tlsf_pairs.items()}
    except (OSError, ValueError) as error:
        parser.error(str(error))
    missing = set(cohorts) - set(source_maps) - set(tlsf_maps)
    if missing:
        parser.error(f"no source map for suite(s): {', '.join(sorted(missing))}")
    env = runner.diagnostic_environment(
        dict(os.environ), progress_every="64", memory_max=args.memory_max,
        memory_swap_max=args.memory_swap_max, preprocessing_census_only=False,
        alphabet_census_only=False, semantic_dominance=False, semantic_decode=True,
        support_demand=True,
    )
    provenance_fields = provenance(binary, args.preset)
    flags = shlex.split(args.flags)
    written = 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as sink:
        writer = csv.DictWriter(sink, fieldnames=COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        sink.flush()
        for suite, cohort in sorted(cohorts.items()):
            instances = list(cohort.items())
            if args.limit:
                instances = instances[:args.limit]
            for name, family in instances:
                base = dict(provenance_fields, suite=suite, instance=name, family=family,
                            cohort=args.cohort, options=shlex.join(flags),
                            timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z"))
                ltl = source_maps.get(suite, {}).get(name)
                tlsf = tlsf_maps.get(suite, {}).get(name)
                if ltl is not None and ltl.is_file() and ltl.with_suffix(".part").is_file():
                    inputs, outputs = read_part(ltl.with_suffix(".part"))
                    command = [str(binary), *flags, "-F", str(ltl), "-i", inputs, "-o", outputs]
                    source, base["route"] = ltl, "ltl"
                elif tlsf is not None and tlsf.is_file():
                    command = [str(binary), *flags, "-T", str(tlsf)]
                    source, base["route"] = tlsf, "tlsf"
                else:
                    writer.writerow(base | {"status": "missing-source", "result": "ERROR"})
                    sink.flush()
                    written += 1
                    continue
                base["source_sha256"] = sha256_file(source)
                accumulator = runner.DiagnosticAccumulator()
                verdicts = set()
                def consume(line):
                    accumulator.add_line(line)
                    if (verdict := verdict_from_output(line)) is not None:
                        verdicts.add(verdict)
                run = run_systemd_scope(
                    command, args.timeout, args.memory_max, args.memory_swap_max,
                    unit_prefix="acacia-spot-demand",
                    env=env | {"ACACIA_DIAG_INSTANCE": name},
                    capture_consumer=consume,
                )
                result = classify_run(replace(run, stdout="\n".join(sorted(verdicts)), stderr=""))
                base.update(seconds=f"{run.seconds:.6f}", exit_code=str(run.returncode),
                            timed_out=str(int(run.timed_out)),
                            timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z"))
                for diag in latest_workers(accumulator.rows()) or [{}]:
                    row = base | demand_row(diag, result)
                    row["preset"] = args.preset or diag.get("preset", "")
                    writer.writerow(row)
                    sink.flush()
                    written += 1
    print(f"# wrote {written} rows to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
