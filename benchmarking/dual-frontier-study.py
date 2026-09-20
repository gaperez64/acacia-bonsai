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
    parser.add_argument("--task", required=True, choices=("convert", "cpre", "solve"))
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
    if task in {"convert", "cpre"} and row.get("loop", ""):
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
            "stderr": row.get("skip_reason", "incomplete capture"),
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
            "stderr": (error.stderr or "")[-2000:],
        }

    elapsed = time.monotonic() - started
    try:
        metrics = parse_tsv(completed.stdout, row, args.task)
        process_outcome = "ok" if completed.returncode == 0 else "replay_nonzero"
    except ValueError as error:
        metrics = {}
        process_outcome = "invalid_output"
        completed.stderr += f"\n{error}"
    replay_status = metrics.get("completion", metrics.get("status", ""))
    return {
        "task": args.task,
        "mode": mode,
        "process_outcome": process_outcome,
        "exit_code": str(completed.returncode),
        "elapsed_s": f"{elapsed:.6f}",
        "replay_status": replay_status,
        "replay_metrics_json": json.dumps(metrics, sort_keys=True, separators=(",", ":")),
        "stderr": completed.stderr.strip()[-2000:],
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
    lines.extend(f"| {mode} | {outcome} | {count} |" for (mode, outcome), count in sorted(outcomes.items()))
    lines += ["", "## Replay statuses", "", "| mode | status | rows |", "|---|---|---:|"]
    lines.extend(f"| {mode} | {status} | {count} |" for (mode, status), count in sorted(statuses.items()))
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
