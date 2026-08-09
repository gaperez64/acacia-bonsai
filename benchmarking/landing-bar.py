#!/usr/bin/env python3
"""Compare baseline and candidate benchmark CSVs using Acacia's landing bar."""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys
from collections import Counter
from dataclasses import dataclass


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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=17.0)
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
            failures.append(
                f"lost {key}: {before.verdict} -> {after.kind.upper()}"
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
