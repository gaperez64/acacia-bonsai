#!/usr/bin/env python3
"""Regenerate the eight complete W1 opening-campaign summary sidecars."""

from __future__ import annotations

import csv
import importlib.util
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[3]
OPENING = pathlib.Path(__file__).resolve().parent
CORPUS_LIST = ROOT / "tests/suites/benchmarks/syntcomp26/all.list"
COVERAGE_RUNNER = ROOT / "benchmarking/run-syntcomp26-coverage.py"
EXPECTED_INSTANCES = 1524


def load_coverage_module():
    spec = importlib.util.spec_from_file_location(
        "coverage_for_witness_summary_repair", COVERAGE_RUNNER
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load coverage runner: {COVERAGE_RUNNER}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    sys.path.insert(0, str(COVERAGE_RUNNER.parent))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def validate_raw(rows, raw_path, solver_label, instances, cap):
    if len(rows) != len(instances):
        raise RuntimeError(
            f"{raw_path}: found {len(rows)} rows, expected {len(instances)}"
        )
    if any(row["solver_label"] != solver_label for row in rows):
        raise RuntimeError(f"{raw_path}: contains an unexpected solver_label")
    if any(row["cap_s"] != str(cap) for row in rows):
        raise RuntimeError(f"{raw_path}: contains a cap other than {cap}")
    raw_instances = [row["instance"] for row in rows]
    if len(set(raw_instances)) != len(raw_instances):
        raise RuntimeError(f"{raw_path}: contains duplicate instances")
    if set(raw_instances) != set(instances):
        raise RuntimeError(f"{raw_path}: instance set differs from {CORPUS_LIST}")


def validate_summary(summary_path, instances):
    with summary_path.open(encoding="utf-8", newline="") as stream:
        summary_rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(summary_rows) != len(instances):
        raise RuntimeError(
            f"{summary_path}: wrote {len(summary_rows)} rows, expected {len(instances)}"
        )
    if {row["instance"] for row in summary_rows} != set(instances):
        raise RuntimeError(f"{summary_path}: regenerated instance set is incomplete")
    return len(summary_rows)


def main() -> int:
    coverage = load_coverage_module()
    instances = coverage.read_instance_list(CORPUS_LIST)
    if len(instances) != EXPECTED_INSTANCES or len(set(instances)) != len(instances):
        raise RuntimeError(
            f"{CORPUS_LIST}: expected {EXPECTED_INSTANCES} unique instances, "
            f"found {len(instances)} rows and {len(set(instances))} unique IDs"
        )

    for cap in (120, 17):
        for epoch in (1, 2):
            for candidate in ("B", "S"):
                solver_label = f"{candidate}-cap{cap}-epoch{epoch}"
                raw_path = OPENING / f"{cap}s/epoch-{epoch}/{solver_label}.tsv"
                rows = coverage.load_output(raw_path)
                validate_raw(rows, raw_path, solver_label, instances, cap)
                summary_path = coverage.write_summary(
                    raw_path, solver_label, instances, rows, cap
                )
                count = validate_summary(summary_path, instances)
                print(f"{summary_path.relative_to(ROOT)}: {count} data rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
