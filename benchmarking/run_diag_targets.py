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

from benchlib import read_part, run_process_group, run_systemd_scope


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
    return env


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build_best_decomp_mona_diag")
    parser.add_argument("--suite-dir", default="tests/ltl/syntcomp24")
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
        "--via-wrapper",
        action="store_true",
        help="run check-real-correct.sh instead of the diagnostics binary directly",
    )
    parser.add_argument(
        "--systemd-scope",
        action="store_true",
        help="run the direct diagnostics binary in a transient systemd memory-limited scope",
    )
    parser.add_argument("--csv", required=True)
    parser.add_argument("targets", nargs="+", help="LTL filenames or paths")
    args = parser.parse_args()
    extra_flags = shlex.split(args.flags)

    build = pathlib.Path(args.build)
    wrapper = build / "tests" / "check-real-correct.sh"
    binary = build / "src" / "acacia-bonsai"
    if args.via_wrapper and not wrapper.exists():
        sys.exit(f"missing wrapper: {wrapper}")
    if args.via_wrapper and args.systemd_scope:
        sys.exit("--via-wrapper and --systemd-scope are mutually exclusive")
    if not args.via_wrapper and not binary.exists():
        sys.exit(f"missing diagnostics binary: {binary}")

    suite_dir = pathlib.Path(args.suite_dir)
    env = diagnostic_environment(
        os.environ,
        progress_every=args.progress_every,
        memory_max=args.memory_max,
        memory_swap_max=args.memory_swap_max,
        preprocessing_census_only=args.preprocessing_census_only,
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
        "bitset_threshold",
        "max_f",
        "max_f_size",
        "loops",
        "k_attempts",
        "cpre_ms",
        "picker_ms",
        "apply_ms",
        "downset_ms",
        "actions_seen",
        "meets_computed",
        "meet_batches",
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
        target_path = pathlib.Path(target)
        if not target_path.exists():
            target_path = suite_dir / target
        if not target_path.exists():
            print(f"skip missing target: {target}", file=sys.stderr)
            continue

        run_env = env.copy()
        run_env["ACACIA_DIAG_INSTANCE"] = target_path.name
        if args.via_wrapper:
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
        accumulator = DiagnosticAccumulator() if args.systemd_scope else None
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
            result = run_process_group(cmd, args.timeout, env=run_env)
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
            diag_rows = [{"instance": target_path.name, "result": "no-diagnostic-line"}]

        for index, diag in enumerate(diag_rows, start=1):
            row = {name: "" for name in fieldnames}
            row.update(diag)
            row["target"] = target_path.name
            row["diag_index"] = str(index)
            row["wrapper_returncode"] = str(result.returncode)
            row["wrapper_timed_out"] = "1" if result.timed_out else "0"
            row["wrapper_stdout_bytes"] = str(result.stdout_bytes)
            row["wrapper_stderr_bytes"] = str(result.stderr_bytes)
            rows.append(row)
        write_checkpoint(csv_path, rows, fieldnames)
        print(
            f"{target_path.name}: return={result.returncode} "
            f"timeout={int(result.timed_out)} diag_lines={len(diag_rows)} "
            f"raw_bytes={result.stdout_bytes + result.stderr_bytes}",
            flush=True,
        )

    write_checkpoint(csv_path, rows, fieldnames)
    print(f"wrote {len(rows)} diagnostic rows to {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
