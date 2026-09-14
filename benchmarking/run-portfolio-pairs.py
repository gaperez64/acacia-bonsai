#!/usr/bin/env python3
"""Measure actual two-arm portfolio invocations under one shared scope budget.

Near-cap instances on this machine vary by about a second between runs of the
SAME binary, measured over five alternating paired rounds. That is enough to
decide a verdict at a 17 second cap; measuring one configuration fully and then
the next charges that within-session spread to whichever went second. Therefore
interleaved order runs every pair back to back on each instance, rotating the
first pair across instances and again within each repetition. Listed order also
keeps pairs adjacent, but always starts with the first pair in the file.

Each row times ONE binary invocation with an explicit --arms arm1,arm2 argument.
Isolated per-arm minima are descriptive oracles, never pair-performance labels.
The coverage runner's columns are retained, with pair_id, arm_tokens,
repetition_id (one-based), and order_index (zero-based campaign schedule slot).
run_index counts completed invocations, including those loaded on resume.

The summary has one row per (pair, instance), using the FIRST decisive
repetition and its actual invocation time, never a minimum across repetitions.
Incomplete unsolved groups have failure_kind_at_max_cap=MISSING and an empty
still_unsolved_at_max_cap, not an invented timeout. Raw rows retain all repeats.
Opposite decisive verdicts for an instance, including across repetitions, make
the campaign fail; the conflicts sidecar contains the decisive evidence rows
and the summary withholds decisive labels for that instance.

A metadata sidecar guards the ordered pairs, selected instances, repetitions,
binary, flags, and resources on resume. Rows are flushed and fsynced per run;
summary/conflict sidecars are rebuilt from those rows, including after a clean
interruption. --preset and --acacia-sha record provenance, not solver options.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import json
import math
import os
import pathlib
import re
import shlex
import subprocess
import sys

from benchlib import build_preset, campaign_scope_guard, classify_run, run_systemd_scope


ROOT = pathlib.Path(__file__).resolve().parents[1]
DECISIVE_RESULTS = {"REALIZABLE", "UNREALIZABLE"}
NORMALIZED_RESULTS = DECISIVE_RESULTS | {"UNKNOWN", "TIMEOUT", "MEMOUT", "CRASH", "ERROR"}
# Keep the coverage runner's schema and append only the pair coordinates.
PAIR_COLUMNS = ["pair_id", "arm_tokens", "repetition_id", "order_index"]
OUTPUT_COLUMNS = [
    "solver_label", "instance", "tlsf_file", "cap_s", "result", "seconds",
    "exit_code", "timed_out", "resource_reason", "expectation_source",
    "stdout_bytes", "stderr_bytes", "run_index", "acacia_sha", "binary_sha256",
    "preset", "timestamp_utc", "flags", "cpu_seconds", "max_process_rss_bytes",
    "scope_memory_peak_bytes", "memory_max", "memory_swap_max", "allowed_cpus",
    "cpu_quota", "collect_rusage", "worker_records_dir", "scope_unit",
] + PAIR_COLUMNS
SUMMARY_COLUMNS = [
    "solver_label", "instance", "smallest_cap_solved", "decisive_result",
    "decisive_seconds", "still_unsolved_at_max_cap", "failure_kind_at_max_cap", "max_cap_s",
] + PAIR_COLUMNS


class PairError(Exception):
    """An actionable campaign input, binary, or resume error."""


def positive_seconds(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return number


def nonnegative_int(value: str) -> int:
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return number


def positive_int(value: str) -> int:
    number = nonnegative_int(value)
    if number == 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def data_lines(path: pathlib.Path):
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if line:
            yield line_number, line


def read_pairs(path: pathlib.Path) -> dict[str, str]:
    pairs = {}
    for line_number, line in data_lines(path):
        fields = line.split("|")
        if len(fields) != 2 or not re.fullmatch(r"[A-Za-z0-9_.-]+", fields[0].strip()):
            raise PairError(f"{path}:{line_number}: expected label|arm1,arm2")
        label, arm_list = (field.strip() for field in fields)
        arms = [arm.strip() for arm in arm_list.split(",")]
        if len(arms) != 2 or any(
            not re.fullmatch(r"[^\s:,|]+:[^\s:,|]+:[^\s:,|]+(?::[^\s:,|]+)?", arm)
            for arm in arms
        ):
            raise PairError(
                f"{path}:{line_number}: expected exactly two "
                "polarity:transform:backend[:provider] tokens"
            )
        if arms[0] == arms[1] or label in pairs:
            raise PairError(f"{path}:{line_number}: duplicate arm or pair label")
        # The binary remains authoritative about backends/providers and build support.
        pairs[label] = ",".join(arms)
    if not pairs:
        raise PairError(f"no pairs in {path}")
    return pairs


def read_targets(args: argparse.Namespace) -> dict[str, pathlib.Path]:
    instances = [line for _, line in data_lines(args.list)]
    if len(set(instances)) != len(instances):
        raise PairError("instance list contains duplicates")
    if args.limit is not None:
        instances = instances[:args.limit]
    mapping = {}
    with args.tlsf_map.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if not {"instance", "tlsf"} <= set(reader.fieldnames or []):
            raise PairError("TLSF map requires instance and tlsf columns")
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise PairError(f"TLSF map line {line_number} is malformed")
            instance, filename = row["instance"].strip(), row["tlsf"].strip()
            if not instance or not filename or instance in mapping:
                raise PairError(f"TLSF map line {line_number} has empty or duplicate entries")
            mapping[instance] = filename
    if not args.tlsf_corpus.is_dir():
        raise PairError(f"TLSF corpus is not a directory: {args.tlsf_corpus}")
    targets = {}
    for instance in instances:
        filename = mapping.get(instance)
        if not filename or pathlib.PurePath(filename).name != filename:
            raise PairError(f"missing or non-flat TLSF map entry for {instance}")
        path = (args.tlsf_corpus / filename).resolve()
        if not path.is_file():
            raise PairError(f"TLSF source missing for {instance}: {path}")
        targets[instance] = path
    return targets


def schedule(pairs: dict[str, str], targets: dict[str, pathlib.Path], repetitions: int,
             order: str):
    labels = list(pairs)
    order_index = 0
    for instance_index, instance in enumerate(targets):
        for repetition in range(1, repetitions + 1):
            offset = (
                (instance_index + repetition - 1) % len(labels) if order == "interleaved" else 0
            )
            for label in labels[offset:] + labels[:offset]:
                yield order_index, label, instance, repetition
                order_index += 1


def sha256_file(path: pathlib.Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git_head() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise PairError("cannot read acacia revision; supply --acacia-sha")
    return result.stdout.strip()


def atomic_write(path: pathlib.Path, write) -> None:
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as stream:
            write(stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_tsv(path: pathlib.Path, columns: list[str], rows: list[dict[str, str]]) -> None:
    def write(stream):
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    atomic_write(path, write)


def pair_metadata(label: str, arms: str) -> dict[str, str]:
    return {"solver_label": label, "pair_id": label, "arm_tokens": arms,
            "flags": shlex.join(["--arms", arms])}


def load_output(path, metadata, pairs, targets, plan) -> list[dict[str, str]]:
    rows = []
    seen = set()
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != OUTPUT_COLUMNS:
            raise PairError(f"resume output {path} has an unexpected header")
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise PairError(f"resume output {path}:{line_number} is malformed")
            label, instance = row["pair_id"], row["instance"]
            if label not in pairs or instance not in targets:
                raise PairError("resume output contains an unselected pair or instance")
            expected = {**metadata, **pair_metadata(label, pairs[label]),
                        "tlsf_file": targets[instance].name, "expectation_source": "none"}
            if any(row[key] != value for key, value in expected.items()):
                raise PairError("resume configuration or binary differs from recorded campaign")
            try:
                slot = int(row["order_index"])
                repetition = int(row["repetition_id"])
                seconds = float(row["seconds"])
                valid = (
                    0 <= slot < len(plan) and plan[slot] == (slot, label, instance, repetition)
                    and slot not in seen and int(row["run_index"]) == len(rows)
                    and math.isfinite(seconds) and seconds >= 0
                    and row["result"] in NORMALIZED_RESULTS and bool(row["scope_unit"])
                    and row["timed_out"] == str(row["result"] == "TIMEOUT").lower()
                )
                int(row["exit_code"])
            except ValueError:
                valid = False
            if not valid:
                raise PairError(f"resume output {path}:{line_number} has invalid run data")
            seen.add(slot)
            rows.append(row)
    return rows


def write_sidecars(output, pairs, targets, rows, cap, repetitions) -> set[str]:
    grouped = {(label, instance): [] for label in pairs for instance in targets}
    verdicts = {instance: set() for instance in targets}
    for row in rows:
        grouped[row["pair_id"], row["instance"]].append(row)
        if row["result"] in DECISIVE_RESULTS:
            verdicts[row["instance"]].add(row["result"])
    conflicts = {instance for instance, answers in verdicts.items() if len(answers) > 1}
    summary = []
    for (label, instance), measured in grouped.items():
        measured = sorted(measured, key=lambda row: int(row["repetition_id"]))
        decisive = next((row for row in measured if row["result"] in DECISIVE_RESULTS), None)
        complete = len(measured) == repetitions
        failure = measured[-1]["result"] if complete else "MISSING"
        if instance in conflicts:
            decisive, failure = None, "CONFLICT"
        summary.append({
            "solver_label": label, "instance": instance, "pair_id": label,
            "arm_tokens": pairs[label],
            "smallest_cap_solved": cap if decisive else "",
            "decisive_result": decisive["result"] if decisive else "",
            "decisive_seconds": decisive["seconds"] if decisive else "",
            "still_unsolved_at_max_cap": (
                "" if instance in conflicts else "false" if decisive else "true" if complete else ""
            ),
            "failure_kind_at_max_cap": failure, "max_cap_s": cap,
            "repetition_id": decisive["repetition_id"] if decisive else "",
            "order_index": decisive["order_index"] if decisive else "",
        })
    write_tsv(output.with_name(f"{output.stem}-summary.tsv"), SUMMARY_COLUMNS, summary)
    write_tsv(output.with_name(f"{output.stem}-conflicts.tsv"), OUTPUT_COLUMNS, [
        row for row in rows
        if row["instance"] in conflicts and row["result"] in DECISIVE_RESULTS
    ])
    return conflicts


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin", required=True, type=pathlib.Path, metavar="PATH")
    parser.add_argument("--pairs", required=True, type=pathlib.Path, metavar="FILE",
                        help="label|arm1,arm2 per line; # comments allowed")
    parser.add_argument("--list", required=True, type=pathlib.Path, metavar="FILE")
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path, metavar="FILE")
    parser.add_argument("--tlsf-corpus", required=True, type=pathlib.Path, metavar="DIR")
    parser.add_argument("--cap", type=positive_seconds, default=17, metavar="SECONDS")
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--cpu-quota", help="whole-invocation systemd CPUQuota, e.g. 200%%")
    parser.add_argument("--output", required=True, type=pathlib.Path, metavar="TSV")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--limit", type=nonnegative_int, metavar="N")
    parser.add_argument("--repetitions", type=positive_int, default=1, metavar="N")
    parser.add_argument("--preset", metavar="NAME",
                        help="provenance only (default: binary's Meson preset, or unknown)")
    parser.add_argument("--acacia-sha", metavar="S",
                        help="provenance revision (default: git rev-parse HEAD)")
    parser.add_argument("--order", choices=("listed", "interleaved"), default="interleaved",
                        help="pair order per instance/repetition (default: rotating interleaved)")
    return parser


def run(args: argparse.Namespace) -> int:
    binary = args.bin.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise PairError(f"binary is missing or not executable: {binary}")
    pairs, targets = read_pairs(args.pairs), read_targets(args)
    plan = list(schedule(pairs, targets, args.repetitions, args.order))
    metadata = {
        "acacia_sha": args.acacia_sha if args.acacia_sha is not None else git_head(),
        "binary_sha256": sha256_file(binary),
        "preset": args.preset if args.preset is not None else build_preset(binary.parent.parent)
        or "unknown",
        "cap_s": format(args.cap, ".15g"), "memory_max": args.memory_max,
        "memory_swap_max": args.memory_swap_max, "allowed_cpus": "",
        "cpu_quota": args.cpu_quota or "", "collect_rusage": "false", "worker_records_dir": "",
    }
    campaign = {
        "metadata": metadata,
        "pairs": [pair_metadata(label, arms) for label, arms in pairs.items()],
        "targets": [[instance, str(path)] for instance, path in targets.items()],
        "repetitions": args.repetitions, "order": args.order,
    }
    output = args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    metadata_path = output.with_name(f"{output.stem}-metadata.json")
    if args.resume and output.exists():
        if not metadata_path.is_file() or json.loads(metadata_path.read_text()) != campaign:
            raise PairError(
                "resume configuration, schedule or binary differs from recorded campaign"
            )
        rows = load_output(output, metadata, pairs, targets, plan)
    else:
        rows = []
        atomic_write(metadata_path, lambda stream: json.dump(campaign, stream, indent=2))
        write_tsv(output, OUTPUT_COLUMNS, rows)

    conflicts = write_sidecars(output, pairs, targets, rows, metadata["cap_s"], args.repetitions)
    if conflicts:
        raise PairError(
            f"recorded verdict conflicts require adjudication: {', '.join(sorted(conflicts))}"
        )
    if any(row["result"] in {"ERROR", "CRASH"} for row in rows):
        raise PairError(
            "recorded binary error/crash; inspect the campaign before starting a new output"
        )
    completed = {int(row["order_index"]) for row in rows}
    try:
        with output.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=OUTPUT_COLUMNS, delimiter="\t",
                                    lineterminator="\n")
            for slot, label, instance, repetition in plan:
                if slot in completed:
                    continue
                arms, tlsf_path = pairs[label], targets[instance]
                # One argv, one invocation, one scope. No preset or fallback arm selection.
                cmd = [str(binary), "--arms", arms, "-T", str(tlsf_path)]
                solver_run = run_systemd_scope(
                    cmd, timeout=args.cap, memory_max=args.memory_max,
                    memory_swap_max=args.memory_swap_max, env=None,
                    unit_prefix="acacia-portfolio-pairs", allowed_cpus=None,
                    cpu_quota=args.cpu_quota,
                )
                result, resource_reason = classify_run(solver_run, tool="acacia"), ""
                if result == "TIMEOUT":
                    resource_reason = "timeout"
                elif result == "RESOURCE_LIMIT":
                    result, resource_reason = "MEMOUT", "memory"
                elif solver_run.returncode < 0:
                    result, resource_reason = "CRASH", f"signal:{-solver_run.returncode}"
                row = {
                    **metadata, **pair_metadata(label, arms), "instance": instance,
                    "tlsf_file": tlsf_path.name, "result": result,
                    "seconds": str(solver_run.seconds), "exit_code": str(solver_run.returncode),
                    "timed_out": str(solver_run.timed_out).lower(),
                    "resource_reason": resource_reason,
                    "expectation_source": "none", "stdout_bytes": str(solver_run.stdout_bytes),
                    "stderr_bytes": str(solver_run.stderr_bytes), "run_index": str(len(rows)),
                    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()
                    .replace("+00:00", "Z"),
                    "cpu_seconds": "", "max_process_rss_bytes": "",
                    "scope_memory_peak_bytes": str(solver_run.memory_peak_bytes)
                    if solver_run.memory_peak_bytes is not None else "",
                    "scope_unit": solver_run.scope_unit, "repetition_id": str(repetition),
                    "order_index": str(slot),
                }
                writer.writerow(row)
                stream.flush()
                os.fsync(stream.fileno())
                rows.append(row)
                print(f"order={slot} repetition={repetition} pair={label} instance={instance} "
                      f"cap={metadata['cap_s']}s result={result} seconds={solver_run.seconds:.3f}",
                      flush=True)
                if result in {"ERROR", "CRASH"}:
                    diagnostic = "\n".join((solver_run.stdout, solver_run.stderr)).strip()
                    raise PairError(f"binary failed for pair {label}; --arms {arms!r}; "
                                    f"exit={solver_run.returncode}\n{diagnostic}")
                if result in DECISIVE_RESULTS and any(
                    previous["instance"] == instance
                    and previous["result"] in DECISIVE_RESULTS and previous["result"] != result
                    for previous in rows[:-1]
                ):
                    print(f"verdict conflict: {instance}; see {output.stem}-conflicts.tsv",
                          file=sys.stderr, flush=True)
                    return 1
    finally:
        write_sidecars(output, pairs, targets, rows, metadata["cap_s"], args.repetitions)
    print(f"wrote {output}\nwrote {output.with_name(f'{output.stem}-summary.tsv')}")
    return 0


def main() -> int:
    args = build_parser().parse_args()
    try:
        with campaign_scope_guard("run-portfolio-pairs"):
            return run(args)
    except (PairError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("interrupted; completed rows are saved for --resume", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
