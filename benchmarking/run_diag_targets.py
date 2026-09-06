#!/usr/bin/env python3
"""Run selected LTL instances through a diagnostics-enabled Acacia build."""

from __future__ import annotations

import argparse
import csv
import os
import pathlib
import re
import shlex
import sys

from benchlib import campaign_scope_guard, read_part, run_process_group, run_systemd_scope
from suite_paths import load_source_map, read_tlsf_source_entries


DIAG_RE = re.compile(r"\bACACIA_DIAG\b(?P<body>.*)$")
FIELD_RE = re.compile(r"(?:^|\s)(?P<key>[A-Za-z_][A-Za-z0-9_]*)=(?P<value>.*?)(?=\s[A-Za-z_][A-Za-z0-9_]*=|$)")


def parse_diag(line: str) -> dict[str, str] | None:
    match = DIAG_RE.search(line)
    if not match:
        return None
    row: dict[str, str] = {}
    for field in FIELD_RE.finditer(match.group("body")):
        row[field.group("key")] = field.group("value").strip()
    return row


def compact_diagnostics(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    """Bound progress output while retaining each child's latest checkpoint."""
    latest_progress: dict[tuple[str, str], dict[str, str]] = {}
    terminal: list[dict[str, str]] = []
    for row in rows:
        if row.get("diag_kind") == "progress":
            key = (row.get("pid", ""), row.get("checkpoint", ""))
            latest_progress[key] = row
        else:
            terminal.append(row)
    progress = [latest_progress[key] for key in sorted(latest_progress)]
    return terminal + progress


class DiagnosticAccumulator:
    """Compact diagnostic lines as they stream from a long-running child."""

    def __init__(self) -> None:
        self.latest_progress: dict[tuple[str, str], dict[str, str]] = {}
        self.terminal: list[dict[str, str]] = []

    def add_line(self, line: str) -> None:
        row = parse_diag(line)
        if row is None:
            return
        if row.get("diag_kind") == "progress":
            key = (row.get("pid", ""), row.get("checkpoint", ""))
            self.latest_progress[key] = row
        else:
            self.terminal.append(row)

    def rows(self) -> list[dict[str, str]]:
        progress = [
            self.latest_progress[key] for key in sorted(self.latest_progress)
        ]
        return self.terminal + progress


def write_checkpoint(
    output: pathlib.Path, rows: list[dict[str, str]], fieldnames: list[str]
) -> None:
    """Atomically persist all completed targets after each solver run."""
    known = set(fieldnames)
    extra_fields = sorted({key for row in rows for key in row if key not in known})
    temporary = output.with_name(f".{output.name}.tmp")
    output.parent.mkdir(parents=True, exist_ok=True)
    with temporary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames + extra_fields)
        writer.writeheader()
        writer.writerows(rows)
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(output)


def diagnostic_environment(
    base: dict[str, str],
    *,
    progress_every: str,
    memory_max: str,
    memory_swap_max: str,
    preprocessing_census_only: bool,
    alphabet_census_only: bool,
    semantic_dominance: bool,
    semantic_decode: bool,
) -> dict[str, str]:
    """Build the child environment, keeping the expensive census opt-in."""
    env = base.copy()
    env.update(
        {
            "ACACIA_DIAG": "1",
            "ACACIA_DIAG_PROGRESS_EVERY": progress_every,
            "ACACIA_TEST_CGROUP": "1",
            "ACACIA_TEST_CGROUP_MEMORY_MAX": memory_max,
            "ACACIA_TEST_CGROUP_SWAP_MAX": memory_swap_max,
            "ACACIA_TEST_RESOURCE_UNKNOWN": "1",
        }
    )
    if preprocessing_census_only:
        env["ACACIA_DIAG_PREPROCESSING_CENSUS"] = "only"
    else:
        env.pop("ACACIA_DIAG_PREPROCESSING_CENSUS", None)
    if alphabet_census_only:
        env["ACACIA_DIAG_ALPHABET_CENSUS_ONLY"] = "1"
    else:
        env.pop("ACACIA_DIAG_ALPHABET_CENSUS_ONLY", None)
    # Both semantic-action censuses cost real work on top of the alphabet walk,
    # so they stay off unless asked for.
    if semantic_dominance:
        env["ACACIA_DIAG_SEMANTIC_DOMINANCE"] = "1"
    else:
        env.pop("ACACIA_DIAG_SEMANTIC_DOMINANCE", None)
    if semantic_decode:
        env["ACACIA_DIAG_SEMANTIC_DECODE"] = "1"
    else:
        env.pop("ACACIA_DIAG_SEMANTIC_DECODE", None)
    return env


def read_tlsf_map(
    path: pathlib.Path, tlsf_corpus: pathlib.Path
) -> dict[str, pathlib.Path]:
    """Resolve a headered logical-instance map against a TLSF corpus."""
    return {
        instance: pathlib.Path(tlsf_corpus) / source
        for instance, source in read_tlsf_source_entries(path).items()
    }


def resolve_tlsf_target(
    instance: str, tlsf_map: dict[str, pathlib.Path]
) -> pathlib.Path:
    """Return one mapped native TLSF input, with fatal-quality errors."""
    try:
        tlsf_path = tlsf_map[instance]
    except KeyError as error:
        raise ValueError(
            f"logical instance is absent from TLSF map: {instance}"
        ) from error
    if not tlsf_path.is_file():
        raise FileNotFoundError(
            f"mapped TLSF file for {instance} does not exist: {tlsf_path}"
        )
    return tlsf_path


def build_native_tlsf_command(
    binary: pathlib.Path, extra_flags: list[str], tlsf_path: pathlib.Path
) -> list[str]:
    """Build Acacia's native TLSF command without a conversion step."""
    return [str(binary), *extra_flags, "-T", str(tlsf_path)]


@campaign_scope_guard("run_diag_targets")
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build_best_decomp_mona_diag")
    parser.add_argument("--suite-dir", help="flat corpus override")
    parser.add_argument(
        "--source-map",
        default="tests/suites/benchmarks/syntcomp24/sources.tsv",
        help="suite sources.tsv used when --suite-dir is omitted",
    )
    parser.add_argument(
        "--tlsf-map",
        metavar="PATH",
        help="headered TSV with columns instance,tlsf",
    )
    parser.add_argument(
        "--tlsf-corpus",
        metavar="DIR",
        help="directory holding the flat .tlsf files named by --tlsf-map",
    )
    parser.add_argument("--timeout", type=float, default=25.0)
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--progress-every", default="64")
    parser.add_argument("--flags", default="", help="extra acacia-bonsai command-line flags")
    parser.add_argument(
        "--preprocessing-census-only",
        action="store_true",
        help="measure cap and direct-simulation reductions, then stop before solving",
    )
    parser.add_argument(
        "--alphabet-census-only",
        action="store_true",
        help="emit MONA alphabet census before preprocessing and stop",
    )
    parser.add_argument(
        "--semantic-dominance",
        action="store_true",
        help="also count inclusion-minimal residual relations (Sprint A stage A0); "
        "budget with ACACIA_DIAG_SEMANTIC_DOMINANCE_MS / _TESTS",
    )
    parser.add_argument(
        "--semantic-decode",
        action="store_true",
        help="also count the distinct residual relations the expansion decoded; "
        "requires a run that reaches the precomputer, so not with --alphabet-census-only",
    )
    parser.add_argument(
        "--via-wrapper",
        action="store_true",
        help="run check-real-correct.sh instead of the diagnostics binary directly",
    )
    parser.add_argument(
        "--systemd-scope",
        action="store_true",
        help="run the direct diagnostics binary in a transient systemd memory-limited scope",
    )
    parser.add_argument(
        "--stream-diagnostics",
        action="store_true",
        help="stream and retain only ACACIA_DIAG lines without a nested systemd scope",
    )
    parser.add_argument("--csv", required=True)
    parser.add_argument("targets", nargs="+", help="LTL filenames or paths")
    args = parser.parse_args()
    extra_flags = shlex.split(args.flags)

    if (args.tlsf_map is None) != (args.tlsf_corpus is None):
        parser.error("--tlsf-map and --tlsf-corpus must be supplied together")
    native_tlsf = args.tlsf_map is not None
    if native_tlsf and args.via_wrapper:
        parser.error("--via-wrapper cannot be used with native TLSF inputs")

    build = pathlib.Path(args.build)
    wrapper = build / "tests" / "check-real-correct.sh"
    binary = build / "src" / "acacia-bonsai"
    if args.via_wrapper and not wrapper.exists():
        sys.exit(f"missing wrapper: {wrapper}")
    if args.via_wrapper and args.systemd_scope:
        sys.exit("--via-wrapper and --systemd-scope are mutually exclusive")
    if not args.via_wrapper and not binary.exists():
        sys.exit(f"missing diagnostics binary: {binary}")

    suite_dir = pathlib.Path(args.suite_dir) if args.suite_dir else None
    suite_sources = (
        None
        if suite_dir or native_tlsf
        else load_source_map(pathlib.Path(args.source_map))
    )
    tlsf_sources = None
    if native_tlsf:
        try:
            tlsf_sources = read_tlsf_map(
                pathlib.Path(args.tlsf_map), pathlib.Path(args.tlsf_corpus)
            )
        except (OSError, ValueError) as error:
            sys.exit(str(error))
    env = diagnostic_environment(
        os.environ,
        progress_every=args.progress_every,
        memory_max=args.memory_max,
        memory_swap_max=args.memory_swap_max,
        preprocessing_census_only=args.preprocessing_census_only,
        alphabet_census_only=args.alphabet_census_only,
        semantic_dominance=args.semantic_dominance,
        semantic_decode=args.semantic_decode,
    )
    csv_path = pathlib.Path(args.csv)
    csv_path.parent.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, str]] = []
    fieldnames = [
        "target",
        "diag_index",
        "diag_kind",
        "checkpoint",
        "wrapper_returncode",
        "wrapper_timed_out",
        "wrapper_stdout_bytes",
        "wrapper_stderr_bytes",
        "pid",
        "instance",
        "path",
        "translation_pref",
        "rsimp_ms",
        "rsimp_changed",
        "syntactic_bypass",
        "syntactic_bypass_ms",
        "translation_ms",
        "aut_states",
        "aut_edges",
        "fast_class",
        "fast_class_ms",
        "fast_solve_ms",
        "fast_verdict",
        "preproc",
        "preproc_ms",
        "preproc_states_before",
        "preproc_states_after",
        "preproc_edges_before",
        "preproc_edges_after",
        "cap_census_ms",
        "cap_k",
        "cap_states_at_k",
        "cap_states_finite",
        "cap_states_zero",
        "cap_counting_states",
        "cap_finite_counting_states",
        "simulation_census_ms",
        "simulation_states_after",
        "simulation_states_removed",
        "bool_threshold",
        "max_f",
        "max_f_size",
        "loops",
        "k_attempts",
        "local_probe_runs",
        "local_probe_status",
        "local_probe_forward_apps",
        "local_probe_skipped_over_budget",
        "local_probe_nodes",
        "cpre_skipped",
        "k_bumped_by_local_refutation",
        "cpre_ms",
        "picker_ms",
        "apply_ms",
        "downset_ms",
        "actions_seen",
        "meets_computed",
        "meet_batches",
        "alphabet_input_paths",
        "alphabet_input_nodes",
        "alphabet_output_paths",
        "alphabet_output_nodes",
        "alphabet_bdd_nodes",
        "alphabet_max_output_paths",
        "alphabet_max_output_nodes",
        "alphabet_minimal_output_nodes",
        "alphabet_dominance_tests",
        "alphabet_dominance_declines",
        "alphabet_census_ms",
        "alphabet_dominance_ms",
        "decoded_transition_sets",
        "decoded_unique_transition_sets",
        "decode_ms",
        "equivariant",
        "eq_clients",
        "eq_blocks",
        "eq_orbits",
        "sym_families",
        "sym_indices",
        "sym_matrix",
        "sym_subsets",
        "sym_selected",
        "sym_orbit_sizes",
        "sym_blocks",
        "sym_shared",
        "solve_ms",
        "total_ms",
        "result",
        "final_reason",
    ]

    for target in args.targets:
        target_name = pathlib.Path(target).name
        if native_tlsf:
            assert tlsf_sources is not None
            try:
                target_path = resolve_tlsf_target(target, tlsf_sources)
            except (FileNotFoundError, ValueError) as error:
                sys.exit(str(error))
        else:
            target_path = pathlib.Path(target)
            if not target_path.exists():
                target_path = (
                    suite_dir / target if suite_dir else suite_sources.get(target)
                )
            if target_path is None or not target_path.exists():
                print(f"skip missing target: {target}", file=sys.stderr)
                continue

        run_env = env.copy()
        run_env["ACACIA_DIAG_INSTANCE"] = target_name
        if native_tlsf:
            cmd = build_native_tlsf_command(binary, extra_flags, target_path)
        elif args.via_wrapper:
            cmd = [
                "/bin/zsh",
                "-f",
                str(wrapper),
                "-p",
                "-a",
                "-F",
                str(target_path),
                *extra_flags,
            ]
        else:
            inputs, outputs = read_part(target_path.with_suffix(".part"))
            cmd = [
                str(binary),
                "-F",
                str(target_path),
                "-i",
                inputs,
                "-o",
                outputs,
                *extra_flags,
            ]
        accumulator = (
            DiagnosticAccumulator()
            if args.systemd_scope or args.stream_diagnostics
            else None
        )
        if args.systemd_scope:
            assert accumulator is not None
            result = run_systemd_scope(
                cmd,
                args.timeout,
                args.memory_max,
                args.memory_swap_max,
                env=run_env,
                unit_prefix="acacia-diag",
                capture_consumer=accumulator.add_line,
            )
        else:
            result = run_process_group(
                cmd,
                args.timeout,
                env=run_env,
                capture_consumer=(accumulator.add_line if accumulator else None),
            )
        if accumulator is not None:
            diag_rows = accumulator.rows()
        else:
            text = f"{result.stdout}\n{result.stderr}"
            diag_rows = []
            for line in text.splitlines():
                parsed = parse_diag(line)
                if parsed is not None:
                    diag_rows.append(parsed)
            if diag_rows:
                diag_rows = compact_diagnostics(diag_rows)
        if not diag_rows:
            diag_rows = [{"instance": target_name, "result": "no-diagnostic-line"}]

        for index, diag in enumerate(diag_rows, start=1):
            row = {name: "" for name in fieldnames}
            row.update(diag)
            row["target"] = target_name
            row["diag_index"] = str(index)
            row["wrapper_returncode"] = str(result.returncode)
            row["wrapper_timed_out"] = "1" if result.timed_out else "0"
            row["wrapper_stdout_bytes"] = str(result.stdout_bytes)
            row["wrapper_stderr_bytes"] = str(result.stderr_bytes)
            rows.append(row)
        write_checkpoint(csv_path, rows, fieldnames)
        print(
            f"{target_name}: return={result.returncode} "
            f"timeout={int(result.timed_out)} diag_lines={len(diag_rows)} "
            f"raw_bytes={result.stdout_bytes + result.stderr_bytes}",
            flush=True,
        )

    write_checkpoint(csv_path, rows, fieldnames)
    print(f"wrote {len(rows)} diagnostic rows to {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
