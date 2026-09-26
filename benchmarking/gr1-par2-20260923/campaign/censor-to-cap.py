#!/usr/bin/env python3
"""Censor one uniform-cap coverage TSV or cactus CSV to a lower cap.

This transforms archived observations; it never runs a solver. Coverage TSVs
need their matching summary and an instance list for the existing coverage
validator. Four-column cactus CSVs lack a cap field, so their path must carry
the high cap (``cap120`` or ``120s``), and TIMEOUT durations must corroborate it.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import CACTUS_NON_SOLVED_RESULTS, CACTUS_SOLVED_RESULTS  # noqa: E402


DECISIVE = CACTUS_SOLVED_RESULTS
CSV_RESULTS = DECISIVE | frozenset(CACTUS_NON_SOLVED_RESULTS)
PROVENANCE_NOTE = "derived by censoring a C_high s observation; not a C_low s run"


def coverage_module():
    spec = importlib.util.spec_from_file_location(
        "syntcomp26_coverage", ROOT / "benchmarking/run-syntcomp26-coverage.py"
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


coverage = coverage_module()


def read_csv(path: pathlib.Path, high: int) -> list[dict[str, str]]:
    # The four-column format does not record a cap per row. Require the archive
    # path to identify the cap and reject any TIMEOUT observed at another cap.
    cap_markers = {int(match.group(1) or match.group(2))
                   for part in path.parts
                   for match in re.finditer(r"(?:^|\D)cap(\d+)(?=\D|$)|^(\d+)s$", part)}
    if cap_markers != {high}:
        raise ValueError(f"{path}: CSV path does not identify only the {high} s source cap")
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["instance", "result", "seconds", "exit"]:
            raise ValueError(f"{path}: expected four-column cactus CSV")
        rows = list(reader)
    seen = set()
    for number, row in enumerate(rows, 2):
        instance = row.get("instance")
        if not instance or instance in seen or None in row or None in row.values():
            raise ValueError(f"{path}:{number}: missing, duplicate, or malformed instance")
        seen.add(instance)
        if row["result"] not in CSV_RESULTS:
            raise ValueError(f"{path}:{number}: unsupported result {row['result']!r}")
        try:
            seconds = float(row["seconds"])
            int(row["exit"])
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid seconds or exit") from exc
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError(f"{path}:{number}: seconds must be finite and non-negative")
        if row["result"] == "TIMEOUT" and not high <= seconds <= high + 1:
            raise ValueError(f"{path}:{number}: TIMEOUT is inconsistent with {high} s cap")
        if row["result"] in DECISIVE and seconds > high:
            raise ValueError(f"{path}:{number}: decisive time exceeds {high} s cap")
        if row["result"] != "TIMEOUT" and seconds > high + 1:
            raise ValueError(f"{path}:{number}: row time exceeds {high} s cap")
    if not rows:
        raise ValueError(f"{path}: empty dataset")
    return rows


def censor(rows: list[dict[str, str]], low: int, *, coverage_tsv: bool):
    derived = []
    provenance = []
    for row in rows:
        original = row.copy()
        seconds = float(row["seconds"])
        decisive = row["result"] in DECISIVE and seconds <= low
        next_row = row.copy()
        if not decisive:
            next_row["result"] = "TIMEOUT"
            next_row["seconds"] = str(low)
            if coverage_tsv:
                next_row.update(exit_code="124", timed_out="true", resource_reason="timeout",
                                cpu_seconds="", max_process_rss_bytes="",
                                scope_memory_peak_bytes="", scope_unit="")
            else:
                next_row["exit"] = "124"
        if coverage_tsv:
            next_row["cap_s"] = str(low)
            next_row["solver_label"] = re.sub(
                r"cap\d+", f"cap{low}", row["solver_label"], count=1
            ) + "-derived"
        derived.append(next_row)
        provenance.append({
            "instance": row["instance"], "original_result": original["result"],
            "original_seconds": original["seconds"],
            "original_exit": original["exit_code" if coverage_tsv else "exit"],
        })
    return derived, provenance


def write_rows(path: pathlib.Path, fields: list[str], rows: list[dict[str, str]],
               delimiter: str) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter=delimiter)
        writer.writeheader()
        writer.writerows(rows)


def derive(source: pathlib.Path, output: pathlib.Path, high: int, low: int,
           instance_list: pathlib.Path | None = None) -> None:
    if high <= 0 or low <= 0 or low >= high:
        raise ValueError("require 0 < C_low < C_high")
    if source.suffix not in {".tsv", ".csv"} or output.suffix != source.suffix:
        raise ValueError("source and output must have the same .tsv or .csv format")
    if source.resolve() == output.resolve():
        raise ValueError("output must differ from source")
    summary = source.with_name(f"{source.stem}-summary.tsv")
    if source.suffix == ".tsv":
        if instance_list is None:
            raise ValueError("coverage TSV requires --list")
        observations = coverage.load_uniform_observations(summary, source, instance_list, high)
        rows = list(observations.values())
        fields = list(rows[0])
    else:
        rows = read_csv(source, high)
        fields = ["instance", "result", "seconds", "exit"]
    sidecar = output.with_name(f"{output.stem}-provenance.tsv")
    metadata = output.with_name(f"{output.stem}-provenance.json")
    output_summary = output.with_name(f"{output.stem}-summary.tsv")
    inputs = {source.resolve(), summary.resolve(), instance_list.resolve()
              if instance_list else source.resolve()}
    outputs = {output.resolve(), sidecar.resolve(), metadata.resolve()}
    if source.suffix == ".tsv":
        outputs.add(output_summary.resolve())
    if inputs & outputs or len(outputs) != (4 if source.suffix == ".tsv" else 3):
        raise ValueError("output and provenance paths must differ from inputs")
    derived, provenance = censor(rows, low, coverage_tsv=source.suffix == ".tsv")
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    metadata_row = {
        "statement": PROVENANCE_NOTE,
        "source_file": str(source.resolve()),
        "source_file_sha256": source_hash,
        "C_high_s": high,
        "C_low_s": low,
        "rows": len(rows),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    write_rows(output, fields, derived, "\t" if source.suffix == ".tsv" else ",")
    if source.suffix == ".tsv":
        coverage.write_summary(output, derived[0]["solver_label"],
                               [row["instance"] for row in derived], derived, low)
    write_rows(sidecar, ["instance", "original_result", "original_seconds", "original_exit"],
               provenance, "\t")
    metadata.write_text(json.dumps(metadata_row, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output} ({len(rows)} rows)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--high-cap", required=True, type=int)
    parser.add_argument("--low-cap", required=True, type=int)
    parser.add_argument("--list", type=pathlib.Path, help="required for coverage TSV")
    args = parser.parse_args()
    try:
        derive(args.input, args.output, args.high_cap, args.low_cap, args.list)
    except (OSError, ValueError, coverage.CoverageError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
