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
    env = os.environ.copy()
    env.update(
        {
            "ACACIA_DIAG": "1",
            "ACACIA_DIAG_PROGRESS_EVERY": args.progress_every,
            "ACACIA_TEST_CGROUP": "1",
            "ACACIA_TEST_CGROUP_MEMORY_MAX": args.memory_max,
            "ACACIA_TEST_CGROUP_SWAP_MAX": args.memory_swap_max,
            "ACACIA_TEST_RESOURCE_UNKNOWN": "1",
        }
    )

    rows: list[dict[str, str]] = []
    fieldnames = [
        "target",
        "diag_index",
        "diag_kind",
        "checkpoint",
        "wrapper_returncode",
        "wrapper_timed_out",
        "pid",
        "instance",
        "path",
        "rsimp_ms",
        "rsimp_changed",
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
        "bool_threshold",
        "bitset_threshold",
        "max_f",
        "loops",
        "k_attempts",
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
        if args.systemd_scope:
            result = run_systemd_scope(
                cmd,
                args.timeout,
                args.memory_max,
                args.memory_swap_max,
                env=run_env,
                unit_prefix="acacia-diag",
            )
        else:
            result = run_process_group(cmd, args.timeout, env=run_env)
        text = f"{result.stdout}\n{result.stderr}"
        diag_rows = []
        for line in text.splitlines():
            parsed = parse_diag(line)
            if parsed is not None:
                diag_rows.append(parsed)
        if not diag_rows:
            diag_rows = [{"instance": target_path.name, "result": "no-diagnostic-line"}]

        for index, diag in enumerate(diag_rows, start=1):
            row = {name: "" for name in fieldnames}
            row.update(diag)
            row["target"] = target_path.name
            row["diag_index"] = str(index)
            row["wrapper_returncode"] = str(result.returncode)
            row["wrapper_timed_out"] = "1" if result.timed_out else "0"
            rows.append(row)
        print(
            f"{target_path.name}: return={result.returncode} "
            f"timeout={int(result.timed_out)} diag_lines={len(diag_rows)}"
        )

    with pathlib.Path(args.csv).open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} diagnostic rows to {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
