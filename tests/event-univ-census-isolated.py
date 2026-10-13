#!/usr/bin/env python3
"""Run the frontend census serially in a fresh, bounded process per source-map row.

Use --resume with the same inputs and settings to continue an interrupted run.
"""

import argparse
import csv
import importlib.util
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import uuid


SUMMARY = Path(__file__).with_name("event-univ-census-summary.py")
spec = importlib.util.spec_from_file_location("event_univ_census_summary", SUMMARY)
summary_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary_tool)


def run_input(command, timeout, stdout, stderr, scope=False):
    unit = f"event-univ-census-{uuid.uuid4().hex}.scope" if scope else None
    if scope:
        command = ["systemd-run", "--user", "--scope", "--quiet", f"--unit={unit}",
                   "-p", "MemoryMax=8G", "-p", "MemorySwapMax=0", *command]
    started = time.monotonic()
    with stdout.open("w") as output, stderr.open("w") as errors:
        try:
            process = subprocess.Popen(command, stdout=output, stderr=errors,
                                       start_new_session=True)
        except OSError as error:
            errors.write(str(error) + "\n")
            return "error_127", time.monotonic() - started
        try:
            code = process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            interrupted = sys.exc_info()[0] is KeyboardInterrupt
            if scope:
                try:
                    subprocess.run(["systemctl", "--user", "kill", "--signal=SIGKILL",
                                    "--kill-whom=all", unit], stdout=subprocess.DEVNULL,
                                   stderr=errors, timeout=10, check=False)
                except (OSError, subprocess.TimeoutExpired) as error:
                    errors.write(str(error) + "\n")
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            if interrupted:
                raise
            return "exhausted", time.monotonic() - started
    code = code if code >= 0 else 128 - code
    return ("ok" if code == 0 else f"error_{code}"), time.monotonic() - started


def append_rows(path, ordinal, writer):
    """Keep interrupted rows visible; the summary excludes their whole input."""
    with path.open() as source:
        reader = csv.reader(source, delimiter="\t")
        header = next(reader, None)
        if header is None:
            return
        if header != list(summary_tool.FIELDS):
            raise ValueError("invalid census header")
        for row in reader:
            if not row or row[0] != "1" or len(row) > len(header):
                raise ValueError(f"invalid isolated census row: {row}")
            row[0] = str(ordinal)
            writer.writerow(row)


def write_input_status(path, statuses):
    """Publish completed-input statuses atomically so interruption leaves a usable record."""
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w") as output:
        for ordinal, (status, seconds) in sorted(statuses.items()):
            output.write(f"{ordinal}\t{status}\t{seconds:.6f}\n")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_map", type=Path)
    parser.add_argument("corpus_root", type=Path)
    parser.add_argument("tool", type=Path)
    parser.add_argument("--output", type=Path, required=True,
                        help="artifact directory (must be new unless --resume is used)")
    parser.add_argument("--resume", action="store_true",
                        help="reuse finished inputs from an interrupted run with matching "
                             "settings")
    parser.add_argument("--timeout", type=float, default=120,
                        help="evaluation bound per input in seconds (default: 120)")
    parser.add_argument("--systemd-scope", action="store_true",
                        help="use one 8 GiB/no-swap user scope per input")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    with args.source_map.open() as source:
        reader = csv.reader(source, delimiter="\t")
        header = next(reader, None)
        rows = list(reader)
    if (header != ["instance", "tlsf"] or not rows or
            any(len(row) != 2 or not all(row) for row in rows)):
        parser.error("source map must have instance and tlsf columns and nonempty data rows")
    if not args.tool.is_file() or not args.corpus_root.is_dir():
        parser.error("tool and corpus root must exist")
    output = args.output.resolve()
    settings = {
        "source_map": str(args.source_map.resolve()),
        "source_map_sha256": summary_tool.digest(args.source_map),
        "corpus_root": str(args.corpus_root.resolve()), "tool": str(args.tool.resolve()),
        "tool_sha256": summary_tool.digest(args.tool), "timeout_seconds": args.timeout,
        "systemd_scope": args.systemd_scope, "memory_max": "8G" if args.systemd_scope else None,
        "memory_swap_max": 0 if args.systemd_scope else None,
        "isolated": True, "serial": True, "source_inputs": len(rows),
    }
    statuses = output / "input-status.tsv"
    finished = {}
    if args.resume:
        try:
            recorded = json.loads((output / "run.json").read_text())
            if recorded != settings:
                parser.error("cannot resume: source/tool identity or run settings differ "
                             "from run.json")
            if statuses.exists():
                finished = summary_tool.read_input_status(statuses, len(rows))
        except (OSError, ValueError) as error:
            parser.error(f"cannot resume: {error}")
    else:
        output.mkdir(parents=True, exist_ok=False)
        (output / "run.json").write_text(json.dumps(settings, indent=2) + "\n")
    inputs = output / "inputs"
    inputs.mkdir(exist_ok=args.resume)
    finished = {ordinal: result for ordinal, result in finished.items()
                if (inputs / f"{ordinal:04d}.tsv").is_file() and
                (inputs / f"{ordinal:04d}.stderr").is_file()}
    write_input_status(statuses, finished)
    census = output / "census.tsv"
    with census.open("w") as combined:
        writer = csv.writer(combined, delimiter="\t", lineterminator="\n")
        writer.writerow(summary_tool.FIELDS)
        for ordinal, row in enumerate(rows, 1):
            stdout = inputs / f"{ordinal:04d}.tsv"
            stderr = inputs / f"{ordinal:04d}.stderr"
            if ordinal in finished:
                status, seconds = finished[ordinal]
            else:
                source_map = inputs / "source-map.tsv"
                with source_map.open("w") as source:
                    source_writer = csv.writer(source, delimiter="\t", lineterminator="\n")
                    source_writer.writerows([header, row])
                status, seconds = run_input(
                    [str(args.tool.resolve()), str(source_map), str(args.corpus_root.resolve())],
                    args.timeout, stdout, stderr, args.systemd_scope)
            try:
                append_rows(stdout, ordinal, writer)
            except ValueError as error:
                with stderr.open("a") as errors:
                    errors.write(str(error) + "\n")
                if status == "ok":
                    status = "error_1"
            finished[ordinal] = (status, seconds)
            write_input_status(statuses, finished)
            combined.flush()
            if status != "ok" or ordinal % 50 == 0 or ordinal == len(rows):
                print(f"{ordinal}/{len(rows)}: {status}", flush=True)
    subprocess.run([sys.executable, str(SUMMARY), str(args.source_map.resolve()),
                    str(census), str(args.tool.resolve()), "--input-status", str(statuses),
                    "--allow-incomplete"], check=True)
    summary = json.loads((output / "summary.json").read_text())
    return int(any(status.startswith("error_") for status in summary["input_status_counts"]) or
               summary["unknown_inputs"] != len(summary["exhausted_input_ordinals"]))


if __name__ == "__main__":
    sys.exit(main())
