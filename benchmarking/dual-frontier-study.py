#!/usr/bin/env python3
"""Run the dual-frontier replay over a frozen manifest in bounded children."""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time
from collections import Counter
from collections import defaultdict
from statistics import median


RESULT_FIELDS = [
    "task",
    "mode",
    "process_outcome",
    "exit_code",
    "elapsed_s",
    "replay_status",
    "replay_metrics_json",
    "stderr",
]


def positive_int(text: str) -> int:
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def nonnegative_int(text: str) -> int:
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replay", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--task", required=True, choices=("convert", "cpre", "delta", "solve"))
    parser.add_argument(
        "--mode",
        action="append",
        choices=("positive", "negative", "auto"),
        help="solve mode; repeat to select several (default: all three)",
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    parser.add_argument("--timeout", type=positive_int, default=10)
    parser.add_argument("--memory-bytes", type=positive_int, default=512 * 1024 * 1024)
    parser.add_argument("--max-work", type=nonnegative_int, default=10_000_000)
    parser.add_argument("--max-workspace-bytes", type=positive_int, default=256 * 1024 * 1024)
    parser.add_argument("--max-frontier", type=positive_int, default=100_000)
    parser.add_argument("--deadline-ms", type=nonnegative_int, default=1000)
    return parser.parse_args()


def load_manifest(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames is None:
            raise ValueError("manifest has no header")
        required = {"instance", "family", "K", "event_path", "complete_capture"}
        missing = required - set(reader.fieldnames)
        if missing:
            raise ValueError(f"manifest misses columns: {', '.join(sorted(missing))}")
        rows = list(reader)
    return list(reader.fieldnames), rows


def replay_directory(row: dict[str, str], manifest: Path) -> Path:
    raw = Path(row["event_path"])
    path = raw if raw.is_absolute() else manifest.parent / raw
    return path if path.is_dir() else path.parent


def limit_memory(limit: int) -> None:
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))


def parse_tsv(stdout: str, row: dict[str, str], task: str) -> dict[str, str]:
    records = list(csv.DictReader(stdout.splitlines(), delimiter="\t"))
    if not records:
        raise ValueError("replay produced no TSV result rows")
    if task in {"convert", "cpre", "delta"} and row.get("loop", ""):
        records = [record for record in records if record.get("loop") == row["loop"]]
        if len(records) != 1:
            raise ValueError("replay did not produce exactly one row for the manifest loop")
    elif len(records) != 1:
        raise ValueError("replay produced multiple rows without a manifest loop selector")
    return records[0]


def run_one(
    args: argparse.Namespace,
    manifest_path: Path,
    row: dict[str, str],
    mode: str,
) -> dict[str, str]:
    if row["complete_capture"].lower() not in {"1", "yes", "true"}:
        return {
            "task": args.task,
            "mode": mode,
            "process_outcome": "skipped_incomplete_capture",
            "exit_code": "",
            "elapsed_s": "0",
            "replay_status": "",
            "replay_metrics_json": "{}",
            "stderr": row.get("skip_reason") or "incomplete capture",
        }

    command = [
        str(args.replay),
        "--dir",
        str(replay_directory(row, manifest_path)),
        "--task",
        args.task,
        "--max-work",
        str(args.max_work),
        "--max-workspace-bytes",
        str(args.max_workspace_bytes),
        "--max-frontier",
        str(args.max_frontier),
        "--deadline-ms",
        str(args.deadline_ms),
    ]
    if args.task == "solve":
        command += ["--mode", mode, "--k", row["K"]]
    elif row.get("loop", ""):
        command += ["--loop", row["loop"]]

    started = time.monotonic()
    try:
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=args.timeout,
            preexec_fn=lambda: limit_memory(args.memory_bytes),
        )
    except subprocess.TimeoutExpired as error:
        return {
            "task": args.task,
            "mode": mode,
            "process_outcome": "timeout",
            "exit_code": "",
            "elapsed_s": f"{time.monotonic() - started:.6f}",
            "replay_status": "",
            "replay_metrics_json": "{}",
            "stderr": (error.stderr or "")[-2000:] or "-",
        }

    elapsed = time.monotonic() - started
    try:
        metrics = parse_tsv(completed.stdout, row, args.task)
        process_outcome = "ok" if completed.returncode == 0 else "replay_nonzero"
    except ValueError as error:
        metrics = {}
        process_outcome = "invalid_output"
        completed.stderr += f"\n{error}"
    replay_status = metrics.get(
        "delta_construction_status", metrics.get("completion", metrics.get("status", ""))
    )
    return {
        "task": args.task,
        "mode": mode,
        "process_outcome": process_outcome,
        "exit_code": str(completed.returncode),
        "elapsed_s": f"{elapsed:.6f}",
        "replay_status": replay_status,
        "replay_metrics_json": json.dumps(metrics, sort_keys=True, separators=(",", ":")),
        "stderr": completed.stderr.strip()[-2000:] or "-",
    }


def atomic_write_tsv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent, text=True)
    try:
        with os.fdopen(descriptor, "w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fields, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def parse_metrics(row: dict[str, str]) -> dict[str, str]:
    try:
        value = json.loads(row["replay_metrics_json"])
    except (KeyError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def frontier_bucket(before: int) -> str:
    if before < 1024:
        return "<1024"
    if before < 4096:
        return "1024-4095"
    if before < 16384:
        return "4096-16383"
    return "16384+"


def ratio(metrics: dict[str, str], numerator: str, denominator: str) -> float | None:
    try:
        top = int(metrics[numerator])
        bottom = int(metrics[denominator])
    except (KeyError, TypeError, ValueError):
        return None
    return top / bottom if bottom else None


def median_label(values: list[float]) -> str:
    return f"{median(values):.6f}" if values else "-"


def write_delta_report(lines: list[str], rows: list[dict[str, str]]) -> None:
    records: list[tuple[dict[str, str], dict[str, str], bool]] = []
    semantic_mismatches = 0
    for row in rows:
        metrics = parse_metrics(row)
        complete = (
            row["process_outcome"] == "ok"
            and metrics.get("delta_construction_status") == "complete"
            and metrics.get("exact") == "yes"
        )
        if (
            metrics.get("exact") == "NO"
            or metrics.get("delta_construction_status") == "invalid_input"
        ):
            semantic_mismatches += 1
        records.append((row, metrics, complete))

    grouped: dict[tuple[str, str], list[tuple[dict[str, str], bool]]] = defaultdict(list)
    for row, metrics, complete in records:
        try:
            before = int(metrics["before_maxima"])
        except (KeyError, TypeError, ValueError):
            before = int(row.get("before_count") or 0)
        grouped[(row.get("family", "-"), frontier_bucket(before))].append((metrics, complete))

    lines += [
        "",
        "## Delta census by family and frontier bucket",
        "",
        "Censored rows remain in the denominator; medians use only exactly certified rows.",
        "",
        "| family | before bucket | events | complete | censored | censorship | "
        "median B_before/Max_before | median B_after/Max_after | "
        "median B_delta/Max_before | median unchanged maxima |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for (family, bucket), group in sorted(grouped.items()):
        completed = [metrics for metrics, complete in group if complete]
        censored = len(group) - len(completed)
        before_ratios = [
            value
            for metrics in completed
            if (value := ratio(metrics, "before_min_excluded", "before_maxima")) is not None
        ]
        after_ratios = [
            value
            for metrics in completed
            if (value := ratio(metrics, "after_min_excluded", "after_maxima")) is not None
        ]
        delta_ratios = [
            value
            for metrics in completed
            if (value := ratio(metrics, "delta_min_excluded", "before_maxima")) is not None
        ]
        survival = [
            float(metrics["positive_generator_survival_fraction"])
            for metrics in completed
        ]
        lines.append(
            f"| {family} | {bucket} | {len(group)} | {len(completed)} | {censored} | "
            f"{censored / len(group):.1%} | {median_label(before_ratios)} | "
            f"{median_label(after_ratios)} | {median_label(delta_ratios)} | "
            f"{median_label(survival)} |"
        )

    large = []
    for row, metrics, complete in records:
        try:
            before = int(metrics["before_maxima"])
        except (KeyError, TypeError, ValueError):
            continue
        if complete and before >= 1024:
            large.append((row, metrics))
    families = {row.get("family", "") for row, _ in large if row.get("family", "")}
    tiny = sum(
        int(metrics["delta_min_excluded"]) * 8 <= int(metrics["before_maxima"])
        for _, metrics in large
    )
    survival = [float(metrics["positive_generator_survival_fraction"]) for _, metrics in large]
    survival_median = median(survival) if survival else 0.0
    gate_checks = {
        "at least 3 unrelated families": len(families) >= 3,
        "at least 20 complete events with before_maxima >= 1024": len(large) >= 20,
        "delta <= before/8 on at least half": bool(large) and tiny * 2 >= len(large),
        "median unchanged-maxima fraction >= 0.5": bool(large) and survival_median >= 0.5,
        "all qualifying deltas exactly certified": bool(large)
        and all(metrics.get("exact") == "yes" for _, metrics in large),
        "zero semantic mismatches": semantic_mismatches == 0,
    }
    passed = all(gate_checks.values())
    lines += [
        "",
        "## Gate C0",
        "",
        f"**{'PASS' if passed else 'FAIL'}** — "
        f"{len(large)} complete large events across {len(families)} families; "
        f"{tiny}/{len(large)} have delta <= before/8; median unchanged maxima "
        f"{survival_median:.6f}; semantic mismatches {semantic_mismatches}.",
        "",
    ]
    lines.extend(
        f"- [{'x' if passed_check else ' '}] {name}"
        for name, passed_check in gate_checks.items()
    )
    lines += [
        "",
        "Decision: "
        + (
            "Gate C0 admits the offline lazy/subtractive prototype."
            if passed
            else "Gate C0 does not admit a lazy/subtractive prototype; stop at the measured census."
        ),
    ]


def write_markdown(path: Path, task: str, rows: list[dict[str, str]]) -> None:
    outcomes = Counter((row["mode"], row["process_outcome"]) for row in rows)
    statuses = Counter((row["mode"], row["replay_status"] or "-") for row in rows)
    lines = [
        "# Dual-frontier replay study",
        "",
        f"Task: `{task}`. Rows: {len(rows)}.",
        "",
        "## Process outcomes",
        "",
        "| mode | outcome | rows |",
        "|---|---|---:|",
    ]
    lines.extend(
        f"| {mode} | {outcome} | {count} |"
        for (mode, outcome), count in sorted(outcomes.items())
    )
    lines += ["", "## Replay statuses", "", "| mode | status | rows |", "|---|---|---:|"]
    lines.extend(
        f"| {mode} | {status} | {count} |"
        for (mode, status), count in sorted(statuses.items())
    )
    if task == "delta":
        write_delta_report(lines, rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    manifest_fields, manifest_rows = load_manifest(args.manifest)
    modes = args.mode or (["positive", "negative", "auto"] if args.task == "solve" else ["-"])
    output_rows: list[dict[str, str]] = []
    for manifest_row in manifest_rows:
        for mode in modes:
            result = run_one(args, args.manifest, manifest_row, mode)
            output_rows.append({**manifest_row, **result})
    atomic_write_tsv(args.output, manifest_fields + RESULT_FIELDS, output_rows)
    write_markdown(args.markdown, args.task, output_rows)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"dual-frontier-study: {error}", file=sys.stderr)
        raise SystemExit(2) from error
