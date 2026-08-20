#!/usr/bin/env python3
"""Compare baseline and candidate benchmark CSVs using Acacia's landing bar."""

from __future__ import annotations

import argparse
import csv
import os
import pathlib
import shlex
import sys
from collections import Counter
from dataclasses import dataclass

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import parse_acacia_result, read_part, run_process_group, run_systemd_scope
from suite_paths import load_source_map


SOLVED = {"REALIZABLE", "UNREALIZABLE"}
KINDS = ("solved", "timeout", "unknown", "error")


@dataclass(frozen=True)
class Result:
    verdict: str | None
    kind: str
    seconds: float


def _first(row: dict[str, str], names: tuple[str, ...]) -> str:
    for name in names:
        value = row.get(name, "").strip()
        if value:
            return value
    return ""


def classify(row: dict[str, str]) -> Result:
    raw = _first(row, ("result", "outcome", "status")).upper()
    verdict = _first(row, ("verdict",)).upper()
    if raw in SOLVED:
        verdict = raw
    elif raw == "OK" and verdict in SOLVED:
        pass
    else:
        verdict = ""

    seconds_raw = _first(row, ("seconds", "duration", "time")) or "0"
    try:
        seconds = float(seconds_raw)
    except ValueError as exc:
        raise ValueError(f"invalid duration {seconds_raw!r}") from exc
    if seconds < 0:
        raise ValueError(f"negative duration {seconds}")

    if verdict in SOLVED:
        return Result(verdict, "solved", seconds)
    if "TIMEOUT" in raw:
        return Result(None, "timeout", seconds)
    if "UNKNOWN" in raw or "RESOURCE" in raw or "NO VERDICT" in raw:
        return Result(None, "unknown", seconds)
    return Result(None, "error", seconds)


def load_csv(path: pathlib.Path) -> dict[str, Result]:
    rows: dict[str, Result] = {}
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"{path}: missing CSV header")
        for line_no, row in enumerate(reader, start=2):
            instance = _first(row, ("instance", "name", "target"))
            if not instance:
                raise ValueError(f"{path}:{line_no}: missing instance")
            suite = row.get("suite", "").strip()
            key = f"{suite}/{instance}" if suite else instance
            if key in rows:
                raise ValueError(f"{path}:{line_no}: duplicate instance {key}")
            try:
                rows[key] = classify(row)
            except ValueError as exc:
                raise ValueError(f"{path}:{line_no} ({key}): {exc}") from exc
    if not rows:
        raise ValueError(f"{path}: no benchmark rows")
    return rows


def summary(rows: dict[str, Result], timeout: float) -> tuple[Counter, float]:
    counts = Counter(result.kind for result in rows.values())
    par2 = sum(
        result.seconds if result.kind == "solved" else 2 * timeout
        for result in rows.values()
    )
    return counts, par2


def print_summary(label: str, rows: dict[str, Result], timeout: float) -> None:
    counts, par2 = summary(rows, timeout)
    print(
        f"{label}: total={len(rows)} solved={counts['solved']} "
        f"timeout={counts['timeout']} unknown={counts['unknown']} "
        f"error={counts['error']} PAR-2={par2:.3f}s"
    )


def run_solver(
    binary: pathlib.Path,
    instances_dir: pathlib.Path,
    key: str,
    timeout: float,
    memory_max: str,
    memory_swap_max: str,
) -> tuple[Result, str, str, list[str]]:
    if instances_dir.is_file():
        source_map = load_source_map(instances_dir)
        ltl = source_map.get(pathlib.Path(key).name, pathlib.Path())
    else:
        ltl = instances_dir / key
        if not ltl.is_file():
            ltl = instances_dir / pathlib.Path(key).name
    if not ltl.is_file():
        raise ValueError(f"cannot locate remeasurement target {key} through {instances_dir}")
    part = ltl.with_suffix(".part")
    if not part.is_file():
        raise ValueError(f"missing partition file for remeasurement: {part}")
    inputs, outputs = read_part(part)
    if not inputs or not outputs:
        raise ValueError(f"incomplete partition file for remeasurement: {part}")
    command = [
        str(binary),
        "-F",
        str(ltl),
        "-i",
        inputs,
        "-o",
        outputs,
    ]
    if os.environ.get("ACACIA_OUTER_CGROUP", "").lower() in {
        "1",
        "true",
        "yes",
        "on",
    }:
        run = run_process_group(command, timeout)
    else:
        run = run_systemd_scope(
            command,
            timeout,
            memory_max,
            memory_swap_max,
            unit_prefix="acacia-landing-remeasure",
        )
    raw = "TIMEOUT" if run.timed_out else parse_acacia_result(run.stdout + run.stderr)
    if raw in SOLVED:
        result = Result(raw, "solved", run.seconds)
    elif raw == "TIMEOUT":
        result = Result(None, "timeout", run.seconds)
    elif raw == "UNKNOWN":
        result = Result(None, "unknown", run.seconds)
    else:
        result = Result(None, "error", run.seconds)
    return result, run.stdout, run.stderr, command


def print_rerun(
    label: str,
    result: Result,
    stdout: str,
    stderr: str,
    command: list[str],
) -> None:
    answer = result.verdict or result.kind.upper()
    print(f"REMEASURE {label}: command={shlex.join(command)}")
    print(f"REMEASURE {label}: result={answer} seconds={result.seconds:.3f}")
    if stdout:
        print(f"REMEASURE {label} STDOUT BEGIN")
        print(stdout, end="" if stdout.endswith("\n") else "\n")
        print(f"REMEASURE {label} STDOUT END")
    if stderr:
        print(f"REMEASURE {label} STDERR BEGIN")
        print(stderr, end="" if stderr.endswith("\n") else "\n")
        print(f"REMEASURE {label} STDERR END")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=17.0)
    parser.add_argument("--baseline-bin", type=pathlib.Path)
    parser.add_argument("--candidate-bin", type=pathlib.Path)
    parser.add_argument(
        "--instances-dir",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1] / "tests" / "ltl",
        help="flat corpus directory or a suite sources.tsv for cap remeasurements",
    )
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    args = parser.parse_args(argv)

    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        baseline = load_csv(args.baseline)
        candidate = load_csv(args.candidate)
    except (OSError, ValueError) as exc:
        print(f"GATE FAIL: {exc}")
        return 1

    print_summary("baseline", baseline, args.timeout)
    print_summary("candidate", candidate, args.timeout)

    failures: list[str] = []
    baseline_keys = set(baseline)
    candidate_keys = set(candidate)
    for key in sorted(baseline_keys - candidate_keys):
        failures.append(f"missing candidate row: {key}")
    for key in sorted(candidate_keys - baseline_keys):
        failures.append(f"unexpected candidate row: {key}")

    for key in sorted(baseline_keys & candidate_keys):
        before = baseline[key]
        after = candidate[key]
        if before.kind != "solved":
            continue
        if after.kind != "solved":
            eligible = (
                after.kind == "timeout"
                and before.seconds > 0.8 * args.timeout
            )
            if not eligible:
                failures.append(
                    f"lost {key}: {before.verdict} -> {after.kind.upper()}"
                )
                continue
            if args.baseline_bin is None or args.candidate_bin is None:
                failures.append(
                    f"lost {key}: cap remeasurement required but --baseline-bin and "
                    "--candidate-bin were not both provided"
                )
                continue
            extended_timeout = 3.0 * args.timeout
            print(
                f"REMEASURE {key}: baseline {before.seconds:.3f}s exceeds "
                f"80% of {args.timeout:g}s; rerunning both sides at "
                f"{extended_timeout:g}s"
            )
            try:
                baseline_rerun = run_solver(
                    args.baseline_bin,
                    args.instances_dir,
                    key,
                    extended_timeout,
                    args.memory_max,
                    args.memory_swap_max,
                )
                candidate_rerun = run_solver(
                    args.candidate_bin,
                    args.instances_dir,
                    key,
                    extended_timeout,
                    args.memory_max,
                    args.memory_swap_max,
                )
            except (OSError, ValueError) as exc:
                failures.append(f"lost {key}: remeasurement failed: {exc}")
                continue
            print_rerun("baseline", *baseline_rerun)
            print_rerun("candidate", *candidate_rerun)
            candidate_result = candidate_rerun[0]
            if candidate_result.kind != "solved":
                failures.append(
                    f"lost {key} after {extended_timeout:g}s remeasurement: "
                    f"{before.verdict} -> {candidate_result.kind.upper()}"
                )
            elif candidate_result.verdict != before.verdict:
                failures.append(
                    f"verdict changed {key} after remeasurement: "
                    f"{before.verdict} -> {candidate_result.verdict}"
                )
        elif before.verdict != after.verdict:
            failures.append(
                f"verdict changed {key}: {before.verdict} -> {after.verdict}"
            )

    if failures:
        for failure in failures:
            print(f"- {failure}")
        print(f"GATE FAIL: {len(failures)} instance comparison failure(s)")
        return 1

    print("GATE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
