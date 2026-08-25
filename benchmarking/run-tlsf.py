#!/usr/bin/env python3
"""Run Acacia's native TLSF frontend, optionally comparing with ltlsynt."""

from __future__ import annotations

import argparse
import csv
import pathlib
import shlex
import subprocess
import sys
import tempfile

from benchlib import RunResult, classify_acacia_run, run_process_group, run_systemd_scope


SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def collect_instances(args: argparse.Namespace) -> list[pathlib.Path]:
    if args.list:
        paths = [
            pathlib.Path(line.strip())
            for line in pathlib.Path(args.list).read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
    else:
        paths = sorted(pathlib.Path(args.root).rglob("*.tlsf"))
    if args.pattern:
        paths = [path for path in paths if args.pattern.lower() in str(path).lower()]
    return paths[: args.limit] if args.limit else paths


def instance_label(path: pathlib.Path, root: pathlib.Path) -> str:
    """Return a stable dataset-relative label without leaking host paths."""
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.name


def run_bounded(
    args: argparse.Namespace, command: list[str], unit_prefix: str
) -> RunResult:
    if args.systemd_scope:
        return run_systemd_scope(
            command,
            args.timeout,
            args.memory_max,
            args.memory_swap_max,
            unit_prefix=unit_prefix,
        )
    return run_process_group(args.runner_prefix + command, args.timeout)


def acacia_result(run: RunResult) -> str:
    return classify_acacia_run(run)


def ltlsynt_result(run: RunResult) -> str:
    if run.timed_out:
        return "TIMEOUT"
    return {0: "REALIZABLE", 1: "UNREALIZABLE", 2: "UNKNOWN"}.get(
        run.returncode, "ERROR"
    )


def ltlsynt_comparison(
    acacia: str, acacia_seconds: float, ltlsynt: str, ltlsynt_seconds: float
) -> str:
    if ltlsynt in SOLVED and acacia not in SOLVED:
        return "SOLVED_ONLY_BY_LTLSYNT"
    if ltlsynt in SOLVED and acacia in SOLVED and ltlsynt != acacia:
        return "VERDICT_MISMATCH"
    if ltlsynt == acacia and ltlsynt in SOLVED and ltlsynt_seconds < acacia_seconds:
        return "LTLSYNT_FASTER"
    return ""


def tool_output(command: list[str]) -> str:
    completed = subprocess.run(command, capture_output=True, text=True, check=True)
    return completed.stdout.strip()


def convert_for_ltlsynt(
    args: argparse.Namespace, tlsf: pathlib.Path, workdir: pathlib.Path
) -> tuple[pathlib.Path, str, str]:
    ltl = workdir / "formula.ltl"
    formula = tool_output([args.tlsf2ltl, "--parenthesize", str(tlsf)])
    ltl.write_text(formula + "\n")
    inputs = tool_output([args.tlsfinfo, "--expanded-ins", str(tlsf)])
    outputs = tool_output([args.tlsfinfo, "--expanded-outs", str(tlsf)])
    return ltl, inputs, outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True)
    parser.add_argument("--root", default="../benchmarks/tlsf")
    parser.add_argument("--list", help="file containing TLSF paths")
    parser.add_argument("--pattern", help="case-insensitive path substring filter")
    parser.add_argument("--flags", default="", help="extra Acacia flags")
    parser.add_argument("--timeout", type=float, default=17.0)
    parser.add_argument("--csv")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--ltlsynt", help="also run this ltlsynt executable")
    parser.add_argument("--tlsf2ltl", default="tlsf2ltl")
    parser.add_argument("--tlsfinfo", default="tlsfinfo")
    parser.add_argument(
        "--systemd-scope",
        action="store_true",
        help="run every solver in its own memory-limited user scope",
    )
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument(
        "--runner-prefix",
        default="",
        help="external wrapper used only without --systemd-scope",
    )
    args = parser.parse_args()
    args.runner_prefix = shlex.split(args.runner_prefix)
    if args.systemd_scope and args.runner_prefix:
        parser.error("--runner-prefix and --systemd-scope are mutually exclusive")

    instances = collect_instances(args)
    if not instances:
        sys.exit("no TLSF instances selected")

    root = pathlib.Path(args.root)
    extra = shlex.split(args.flags)
    rows: list[dict[str, object]] = []
    ltlsynt_wins: list[tuple[str, str]] = []
    fieldnames = [
        "instance",
        "acacia_result",
        "acacia_seconds",
        "acacia_exit",
        "ltlsynt_result",
        "ltlsynt_seconds",
        "ltlsynt_exit",
        "ltlsynt_comparison",
    ]
    csv_handle = None
    csv_writer = None
    if args.csv:
        csv_handle = pathlib.Path(args.csv).open("w", newline="")
        csv_writer = csv.DictWriter(csv_handle, fieldnames=fieldnames)
        csv_writer.writeheader()
        csv_handle.flush()
    print(f"# bin={pathlib.Path(args.bin).name}")
    print(
        f"# dataset={root.name} pattern={args.pattern!r} timeout={args.timeout}s "
        f"n={len(instances)} cgroup={args.systemd_scope}"
    )

    for tlsf in instances:
        label = instance_label(tlsf, root)
        native = run_bounded(
            args,
            [args.bin, "-T", str(tlsf), *extra],
            "acacia-tlsf-native",
        )
        row: dict[str, object] = {
            "instance": label,
            "acacia_result": acacia_result(native),
            "acacia_seconds": round(native.seconds, 3),
            "acacia_exit": native.returncode,
            "ltlsynt_result": "NOT_RUN",
            "ltlsynt_seconds": "",
            "ltlsynt_exit": "",
            "ltlsynt_comparison": "",
        }

        if args.ltlsynt:
            try:
                with tempfile.TemporaryDirectory(prefix="ab-tlsf-") as raw_tmp:
                    ltl, inputs, outputs = convert_for_ltlsynt(
                        args, tlsf, pathlib.Path(raw_tmp)
                    )
                    reference = run_bounded(
                        args,
                        [
                            args.ltlsynt,
                            "--realizability",
                            "-F",
                            str(ltl),
                            f"--ins={inputs}",
                            f"--outs={outputs}",
                        ],
                        "acacia-tlsf-ltlsynt",
                    )
                row.update(
                    ltlsynt_result=ltlsynt_result(reference),
                    ltlsynt_seconds=round(reference.seconds, 3),
                    ltlsynt_exit=reference.returncode,
                )
            except (OSError, subprocess.CalledProcessError):
                row["ltlsynt_result"] = "TRANSLATE_ERROR"

            comparison = ltlsynt_comparison(
                str(row["acacia_result"]),
                float(row["acacia_seconds"]),
                str(row["ltlsynt_result"]),
                float(row["ltlsynt_seconds"] or 0),
            )
            row["ltlsynt_comparison"] = comparison
            if comparison:
                ltlsynt_wins.append((label, comparison))

        rows.append(row)
        if csv_writer is not None and csv_handle is not None:
            csv_writer.writerow(row)
            csv_handle.flush()
        result_line = f"acacia={row['acacia_result']}"
        if args.ltlsynt:
            result_line += f", ltlsynt={row['ltlsynt_result']}"
        print(f"  {label:70s} {result_line}", flush=True)

    print(f"\nltlsynt advantages/mismatches: {len(ltlsynt_wins)}")
    for label, comparison in ltlsynt_wins:
        print(f"LTLSYNT {comparison} instance={label}")

    if args.csv:
        assert csv_handle is not None
        csv_handle.close()
        print(f"wrote {pathlib.Path(args.csv).name}")
    return int(any(comparison == "VERDICT_MISMATCH" for _, comparison in ltlsynt_wins))


if __name__ == "__main__":
    raise SystemExit(main())
