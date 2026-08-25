#!/usr/bin/env python3
"""Run a controlled zero-tail versus bare-vector Acacia comparison.

The variants are run as pairs.  Their order alternates every repetition to
limit temperature and load-order bias.  Each sample is committed to disk
before the next solver starts, so an interrupted campaign can be resumed.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import pathlib
import shlex
import statistics
import subprocess
import tempfile
from dataclasses import asdict, dataclass

from benchlib import classify_acacia_run, read_part, run_systemd_scope
from run_diag_targets import DiagnosticAccumulator
from suite_paths import load_source_map


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_TARGETS = ROOT / "benchmarking/state-vector-tail-targets.tsv"
PERF_EVENTS = ("cycles", "instructions", "LLC-load-misses", "branch-misses")
SAMPLE_FIELDS = (
    "label",
    "suite",
    "instance",
    "repetition",
    "position",
    "variant",
    "verdict",
    "seconds",
    "returncode",
    "timed_out",
    "resource_limited",
    "cycles",
    "instructions",
    "llc_load_misses",
    "branch_misses",
    "stdout_sha256",
    "stderr_sha256",
    "diagnostics_json",
)


@dataclass(frozen=True)
class Target:
    label: str
    suite: str
    instance: str
    formula: pathlib.Path


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def text_sha256(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def classify_sample(run) -> str:
    """Classify both outer-scope and inner GNU timeout deadlines."""
    if run.returncode == 124 and not run.resource_limited:
        return "TIMEOUT"
    return classify_acacia_run(run)


def read_targets(path: pathlib.Path) -> list[Target]:
    targets: list[Target] = []
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != ["label", "suite", "instance"]:
            raise ValueError(f"{path}: expected label, suite, instance columns")
        maps: dict[str, dict[str, pathlib.Path]] = {}
        for row in reader:
            suite = row["suite"]
            if suite not in maps:
                source_map = ROOT / f"tests/suites/benchmarks/{suite}/sources.tsv"
                maps[suite] = load_source_map(source_map)
            try:
                formula = maps[suite][row["instance"]]
            except KeyError as error:
                raise ValueError(
                    f"{suite}: unknown instance {row['instance']!r}"
                ) from error
            if not formula.is_file() or not formula.with_suffix(".part").is_file():
                raise FileNotFoundError(formula)
            targets.append(Target(row["label"], suite, row["instance"], formula))
    if not targets:
        raise ValueError(f"{path}: no targets")
    return targets


def parse_perf_stat(path: pathlib.Path) -> dict[str, int | None]:
    counters = {event: None for event in PERF_EVENTS}
    if not path.exists():
        return counters
    for raw in path.read_text(encoding="utf-8").splitlines():
        fields = raw.split("\t")
        if len(fields) < 3:
            continue
        event = fields[2].strip().split(":", 1)[0]
        if event not in counters:
            continue
        value = fields[0].strip()
        try:
            counters[event] = int(value)
        except ValueError:
            counters[event] = None
    return counters


def write_tsv(path: pathlib.Path, rows: list[dict], fields: tuple[str, ...]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(path)


def read_samples(path: pathlib.Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if tuple(reader.fieldnames or ()) != SAMPLE_FIELDS:
            raise ValueError(f"{path}: incompatible sample schema")
        return list(reader)


def ratio(numerator: float | int | None, denominator: float | int | None) -> str:
    if numerator is None or denominator in (None, 0):
        return ""
    return f"{float(numerator) / float(denominator):.9f}"


def paired_rows(samples: list[dict]) -> list[dict]:
    grouped: dict[tuple[str, str], dict[str, dict]] = {}
    for row in samples:
        grouped.setdefault((row["label"], row["repetition"]), {})[
            row["variant"]
        ] = row
    pairs: list[dict] = []
    for (label, repetition), variants in sorted(grouped.items()):
        if set(variants) != {"zero", "bare"}:
            continue
        zero, bare = variants["zero"], variants["bare"]
        same_verdict = zero["verdict"] == bare["verdict"]
        same_solved = (
            same_verdict
            and zero["verdict"] in {"REALIZABLE", "UNREALIZABLE"}
        )
        pairs.append(
            {
                "label": label,
                "repetition": repetition,
                "zero_verdict": zero["verdict"],
                "bare_verdict": bare["verdict"],
                "verdict_match": str(zero["verdict"] == bare["verdict"]).lower(),
                "zero_seconds": zero["seconds"],
                "bare_seconds": bare["seconds"],
                "bare_over_zero_seconds": ratio(
                    float(bare["seconds"]) if same_solved else None,
                    float(zero["seconds"]) if same_solved else None,
                ),
                "zero_cycles": zero["cycles"],
                "bare_cycles": bare["cycles"],
                "bare_over_zero_cycles": ratio(
                    int(bare["cycles"]) if same_verdict and bare["cycles"] else None,
                    int(zero["cycles"]) if same_verdict and zero["cycles"] else None,
                ),
                "bare_over_zero_instructions": ratio(
                    int(bare["instructions"])
                    if same_verdict and bare["instructions"]
                    else None,
                    int(zero["instructions"])
                    if same_verdict and zero["instructions"]
                    else None,
                ),
                "bare_over_zero_llc_load_misses": ratio(
                    int(bare["llc_load_misses"])
                    if same_verdict and bare["llc_load_misses"]
                    else None,
                    int(zero["llc_load_misses"])
                    if same_verdict and zero["llc_load_misses"]
                    else None,
                ),
                "bare_over_zero_branch_misses": ratio(
                    int(bare["branch_misses"])
                    if same_verdict and bare["branch_misses"]
                    else None,
                    int(zero["branch_misses"])
                    if same_verdict and zero["branch_misses"]
                    else None,
                ),
            }
        )
    return pairs


def summary_rows(samples: list[dict], pairs: list[dict]) -> list[dict]:
    labels = sorted({row["label"] for row in samples})
    result: list[dict] = []
    for label in labels:
        label_samples = [row for row in samples if row["label"] == label]
        label_pairs = [row for row in pairs if row["label"] == label]
        row: dict[str, str | int] = {"label": label}
        for variant in ("zero", "bare"):
            selected = [sample for sample in label_samples if sample["variant"] == variant]
            solved = [
                sample
                for sample in selected
                if sample["verdict"] in {"REALIZABLE", "UNREALIZABLE"}
            ]
            row[f"{variant}_solved"] = len(solved)
            row[f"{variant}_median_seconds"] = (
                f"{statistics.median(float(sample['seconds']) for sample in solved):.9f}"
                if solved
                else ""
            )
            counters = [int(sample["cycles"]) for sample in solved if sample["cycles"]]
            row[f"{variant}_median_cycles"] = (
                f"{statistics.median(counters):.0f}" if counters else ""
            )
        time_ratios = [
            float(pair["bare_over_zero_seconds"])
            for pair in label_pairs
            if pair["bare_over_zero_seconds"]
        ]
        cycle_ratios = [
            float(pair["bare_over_zero_cycles"])
            for pair in label_pairs
            if pair["bare_over_zero_cycles"]
        ]
        instruction_ratios = [
            float(pair["bare_over_zero_instructions"])
            for pair in label_pairs
            if pair["bare_over_zero_instructions"]
        ]
        row["paired_median_time_ratio"] = (
            f"{statistics.median(time_ratios):.9f}" if time_ratios else ""
        )
        row["paired_median_cycle_ratio"] = (
            f"{statistics.median(cycle_ratios):.9f}" if cycle_ratios else ""
        )
        row["paired_median_instruction_ratio"] = (
            f"{statistics.median(instruction_ratios):.9f}"
            if instruction_ratios
            else ""
        )
        result.append(row)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zero-bin", required=True, type=pathlib.Path)
    parser.add_argument("--bare-bin", required=True, type=pathlib.Path)
    parser.add_argument("--targets", type=pathlib.Path, default=DEFAULT_TARGETS)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument(
        "--limit", type=int, default=0, help="run only the first N targets"
    )
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--flags", default="")
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--no-perf", action="store_true")
    parser.add_argument(
        "--diagnostics",
        action="store_true",
        help="capture progress from diagnostics-enabled binaries",
    )
    parser.add_argument("--progress-every", default="64")
    parser.add_argument("--resume", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    binaries = {"zero": args.zero_bin.resolve(), "bare": args.bare_bin.resolve()}
    for binary in binaries.values():
        if not binary.is_file():
            raise FileNotFoundError(binary)
    if args.repetitions < 1 or args.timeout <= 0:
        raise ValueError("repetitions and timeout must be positive")

    targets = read_targets(args.targets.resolve())
    if args.limit:
        targets = targets[: args.limit]
    args.output.mkdir(parents=True, exist_ok=True)
    sample_path = args.output / "samples.tsv"
    if sample_path.exists() and not args.resume:
        raise FileExistsError(f"{sample_path} exists; pass --resume to continue")
    samples = read_samples(sample_path)
    completed = {
        (row["label"], int(row["repetition"]), row["variant"]) for row in samples
    }

    metadata = {
        "schema": 1,
        "harness_sha256": file_sha256(pathlib.Path(__file__)),
        "git_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=True,
        ).stdout.strip(),
        "targets": str(args.targets.resolve()),
        "target_sha256": file_sha256(args.targets.resolve()),
        "timeout_seconds": args.timeout,
        "repetitions": args.repetitions,
        "memory_max": args.memory_max,
        "memory_swap_max": args.memory_swap_max,
        "flags": shlex.split(args.flags),
        "cpu": args.cpu,
        "perf_events": [] if args.no_perf else list(PERF_EVENTS),
        "diagnostics": args.diagnostics,
        "diagnostics_progress_every": (
            args.progress_every if args.diagnostics else None
        ),
        "binaries": {
            variant: {"path": str(path), "sha256": file_sha256(path)}
            for variant, path in binaries.items()
        },
        "resolved_targets": [
            {**asdict(target), "formula": str(target.formula)} for target in targets
        ],
    }
    metadata_path = args.output / "metadata.json"
    if metadata_path.exists() and args.resume:
        existing = json.loads(metadata_path.read_text(encoding="utf-8"))
        if existing != metadata:
            raise ValueError("resume metadata does not match this invocation")
    else:
        metadata_path.write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    extra_flags = shlex.split(args.flags)
    for repetition in range(1, args.repetitions + 1):
        order = ("bare", "zero") if repetition % 2 else ("zero", "bare")
        for target in targets:
            inputs, outputs = read_part(target.formula.with_suffix(".part"))
            for position, variant in enumerate(order, 1):
                key = (target.label, repetition, variant)
                if key in completed:
                    continue
                solver_cmd = [
                    str(binaries[variant]),
                    "-F",
                    str(target.formula),
                    "-i",
                    inputs,
                    "-o",
                    outputs,
                    *extra_flags,
                ]
                if args.cpu is not None:
                    solver_cmd = ["taskset", "-c", str(args.cpu), *solver_cmd]
                # Keep perf outside the deadline process.  GNU timeout exits
                # normally with status 124, allowing perf to flush counters;
                # the enclosing scope remains a cleanup backstop.
                solver_cmd = [
                    "timeout",
                    "--signal=TERM",
                    "--kill-after=1s",
                    f"{args.timeout:.9g}s",
                    *solver_cmd,
                ]
                with tempfile.TemporaryDirectory(prefix="acacia-tail-perf-") as temporary:
                    perf_path = pathlib.Path(temporary) / "perf.tsv"
                    cmd = solver_cmd
                    if not args.no_perf:
                        cmd = [
                            "perf",
                            "stat",
                            "--no-big-num",
                            "-x",
                            "\t",
                            "-o",
                            str(perf_path),
                            "-e",
                            ",".join(PERF_EVENTS),
                            "--",
                            *solver_cmd,
                        ]
                    print(
                        f"[{repetition}/{args.repetitions}] {target.label} "
                        f"{variant} ({position}/2)",
                        flush=True,
                    )
                    accumulator = DiagnosticAccumulator() if args.diagnostics else None
                    retained_lines: list[str] = []

                    def consume(line: str) -> None:
                        assert accumulator is not None
                        accumulator.add_line(line)
                        # Diagnostics can emit millions of progress lines on
                        # a capped solve.  The accumulator keeps only parsed
                        # snapshots; retain just verdict text for the normal
                        # classifier and output hash.
                        if any(
                            verdict in line
                            for verdict in ("REALIZABLE", "UNREALIZABLE", "UNKNOWN")
                        ):
                            retained_lines.append(line)

                    run_env = None
                    if args.diagnostics:
                        run_env = os.environ.copy()
                        run_env.update(
                            {
                                "ACACIA_DIAG": "1",
                                "ACACIA_DIAG_INSTANCE": target.label,
                                "ACACIA_DIAG_PROGRESS_EVERY": args.progress_every,
                            }
                        )
                    run = run_systemd_scope(
                        cmd,
                        args.timeout + 3,
                        args.memory_max,
                        args.memory_swap_max,
                        env=run_env,
                        unit_prefix="acacia-tail-study",
                        capture_consumer=(consume if args.diagnostics else None),
                    )
                    if retained_lines:
                        # A capture consumer intentionally keeps benchlib's
                        # stdout/stderr buffers empty.  Restore the small
                        # retained stream for verdict classification/hashing.
                        run = type(run)(
                            "".join(retained_lines),
                            "",
                            run.returncode,
                            run.seconds,
                            run.timed_out,
                            run.stdout_bytes,
                            run.stderr_bytes,
                            run.resource_limited,
                        )
                    counters = parse_perf_stat(perf_path)
                samples.append(
                    {
                        "label": target.label,
                        "suite": target.suite,
                        "instance": target.instance,
                        "repetition": repetition,
                        "position": position,
                        "variant": variant,
                        "verdict": classify_sample(run),
                        "seconds": f"{run.seconds:.9f}",
                        "returncode": run.returncode,
                        "timed_out": str(run.timed_out).lower(),
                        "resource_limited": str(run.resource_limited).lower(),
                        "cycles": counters["cycles"] or "",
                        "instructions": counters["instructions"] or "",
                        "llc_load_misses": counters["LLC-load-misses"] or "",
                        "branch_misses": counters["branch-misses"] or "",
                        "stdout_sha256": text_sha256(run.stdout),
                        "stderr_sha256": text_sha256(run.stderr),
                        "diagnostics_json": json.dumps(
                            accumulator.rows() if accumulator is not None else [],
                            sort_keys=True,
                            separators=(",", ":"),
                        ),
                    }
                )
                completed.add(key)
                write_tsv(sample_path, samples, SAMPLE_FIELDS)
                pairs = paired_rows(samples)
                if pairs:
                    write_tsv(
                        args.output / "pairs.tsv",
                        pairs,
                        tuple(pairs[0]),
                    )
                    summaries = summary_rows(samples, pairs)
                    write_tsv(
                        args.output / "summary.tsv",
                        summaries,
                        tuple(summaries[0]),
                    )
                print(
                    f"  {samples[-1]['verdict']} {samples[-1]['seconds']}s "
                    f"cycles={samples[-1]['cycles'] or '-'}",
                    flush=True,
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
