#!/usr/bin/env python3
"""Obfuscate a TLSF panel serially, checkpointing checked outputs and exclusions."""

from __future__ import annotations

import argparse
import csv
import math
import os
import pathlib
import random
import sys

from benchlib import load_tool, run_process_group

coverage = load_tool("benchmarking/run-syntcomp26-coverage.py")
MANIFEST_COLUMNS = [
    "instance", "original_tlsf", "original_sha256", "neutral_tlsf", "neutral_sha256",
    "mode", "seed", "tlsfobf", "tlsfobf_sha256", "timeout_s", "expected",
    "expectation_source", "map_sha256", "exit_status", "reason",
]
EXCEPTION_COLUMNS = ["instance", "annotated_status", "corrected_status", "evidence"]


def read_manifest(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != MANIFEST_COLUMNS:
            raise coverage.CoverageError(f"{path}: unexpected manifest header")
        rows = {}
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise coverage.CoverageError(f"{path}: malformed manifest row")
            if not row["instance"] or row["instance"] in rows:
                raise coverage.CoverageError(f"{path}: empty or duplicate instance")
            if not row["exit_status"] or (row["exit_status"] == "0") == bool(row["reason"]):
                raise coverage.CoverageError(f"{path}: inconsistent exit_status/reason")
            rows[row["instance"]] = row
    return rows


def positive_seconds(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tlsfobf", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-corpus", required=True, type=pathlib.Path)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True, choices=("rename", "reorder", "all"))
    parser.add_argument("--seed", required=True, type=coverage.nonnegative_int)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=positive_seconds, default=60,
                        help="per-file timeout in seconds (default: 60)")
    parser.add_argument("--status-exceptions", type=pathlib.Path,
                        default=pathlib.Path(__file__).with_name("syntcomp26-status-exceptions.tsv"),
                        help="reviewed corrections for the original corpus; default: repository table")
    return parser


def run(args):
    binary = args.tlsfobf.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise coverage.CoverageError(f"tlsfobf is not executable: {binary}")
    instances = coverage.read_instance_list(args.list)
    if not instances or len(set(instances)) != len(instances):
        raise coverage.CoverageError("list must contain unique instances and must not be empty")
    targets = coverage.resolve_targets(
        instances, coverage.read_tlsf_map(args.tlsf_map), args.tlsf_corpus.resolve(),
    )
    exceptions = coverage.read_status_exceptions(
        args.status_exceptions, args.tlsf_corpus.resolve(), required=True,
    )
    shuffled = instances.copy()
    random.Random(args.seed).shuffle(shuffled)
    neutral = {instance: f"n{index:06d}.tlsf" for index, instance in enumerate(shuffled)}
    binary_sha = coverage.sha256_file(binary)
    planned = {}
    for instance, (tlsf, source) in targets.items():
        expected = exceptions[tlsf] if tlsf in exceptions else coverage.expected_verdict(source)
        planned[instance] = {
            "instance": instance, "original_tlsf": str(source),
            "original_sha256": coverage.sha256_file(source), "neutral_tlsf": neutral[instance],
            "mode": args.mode, "seed": str(args.seed), "tlsfobf": str(binary),
            "tlsfobf_sha256": binary_sha, "timeout_s": f"{args.timeout:g}",
            "expected": expected or "",
            "expectation_source": ("exception" if tlsf in exceptions else "status")
                                  if expected else "none",
        }
    output = args.output.resolve()
    manifest = output / "manifest.tsv"
    previous = read_manifest(manifest) if manifest.exists() else {}
    for instance, row in previous.items():
        if instance not in planned or any(row[key] != value
                                          for key, value in planned[instance].items()):
            raise coverage.CoverageError(
                f"{instance}: resume inputs changed; use a new --output directory",
            )
    corpus = output / "corpus"
    maps = output / "maps"
    if corpus.resolve() == args.tlsf_corpus.resolve():
        raise coverage.CoverageError("output corpus must differ from the original corpus")
    corpus.mkdir(parents=True, exist_ok=True)
    maps.mkdir(parents=True, exist_ok=True)
    records = previous.copy()
    for instance in instances:
        identity = planned[instance]
        target = corpus / neutral[instance]
        name_map = maps / target.with_suffix(".map").name
        row = previous.get(instance)
        if row is not None and row["exit_status"] == "0":
            if not (target.is_file() and name_map.is_file()
                    and coverage.sha256_file(target) == row["neutral_sha256"]
                    and coverage.sha256_file(name_map) == row["map_sha256"]):
                row = None
        if row is None:
            target.unlink(missing_ok=True)
            name_map.unlink(missing_ok=True)
            row = {**identity, "neutral_sha256": "", "map_sha256": "",
                   "exit_status": "", "reason": ""}
            result = run_process_group([
                str(binary), "--mode", args.mode, "--seed", str(args.seed),
                "--map", str(name_map), "--output", str(target), identity["original_tlsf"],
            ], timeout=args.timeout, env=os.environ.copy())
            row["exit_status"] = str(result.returncode)
            if result.timed_out:
                row["reason"] = f"timeout after {args.timeout:g} s"
            elif result.returncode != 0:
                row["reason"] = result.stderr.strip() or f"tlsfobf exited {result.returncode}"
            elif not target.is_file() or target.stat().st_size == 0 or not name_map.is_file():
                row["reason"] = "tlsfobf succeeded without TLSF output or renaming map"
                row["exit_status"] = "invalid-output"
            else:
                row["neutral_sha256"] = coverage.sha256_file(target)
                row["map_sha256"] = coverage.sha256_file(name_map)
        if row["reason"]:
            target.unlink(missing_ok=True)
            name_map.unlink(missing_ok=True)
        if records.get(instance) != row:
            records[instance] = row
            coverage.atomic_write_tsv(manifest, MANIFEST_COLUMNS,
                                      [records[name] for name in instances if name in records])

    completed = [records[name] for name in instances]
    included = [row for row in completed if row["exit_status"] == "0"]
    coverage.atomic_write_tsv(output / "tlsf-sources.tsv", ["instance", "tlsf"], [
        {"instance": row["instance"], "tlsf": row["neutral_tlsf"]} for row in included
    ])
    coverage.atomic_write_tsv(output / "instances.list", ["# Successful instances"], [
        {"# Successful instances": row["instance"]} for row in included
    ])
    coverage.atomic_write_tsv(output / "status-exceptions.tsv", EXCEPTION_COLUMNS, [
        {"instance": row["neutral_tlsf"], "annotated_status": "<none>",
         "corrected_status": row["expected"].lower(),
         "evidence": f"Carried from {row['expectation_source']} for {row['instance']}; "
                     f"source SHA-256 {row['original_sha256']}; see manifest.tsv"}
        for row in included if row["expected"]
    ])
    print(f"obfuscated {len(included)}/{len(completed)}; "
          f"excluded {len(completed) - len(included)} (see {manifest})")
    return 0


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        return run(args)
    except (coverage.CoverageError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
