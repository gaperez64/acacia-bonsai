#!/usr/bin/env python3
"""Rank 4- and 5-arm virtual-best portfolios from six 60 s per-arm CSVs.

This is a selection aid. A real concurrent portfolio needs its own run.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import SOLVED, par2  # noqa: E402

ARMS = (
    "real:small:backward",
    "real:small:forward",
    "unreal:formula:spot-guarded-sparse",
    "unreal:automaton:forward",
    "real:gr1:oxidd",
    "unreal:gr1:oxidd",
)
CAP = 60.0


def read_ids(path: pathlib.Path) -> list[str]:
    ids = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
           if line.strip() and not line.startswith("#")]
    if len(ids) != 1524 or len(set(ids)) != len(ids):
        raise ValueError(f"{path}: expected 1,524 distinct original IDs")
    return ids


def read_leg(path: pathlib.Path, ids: list[str]) -> dict[str, tuple[str, float]]:
    rows: dict[str, tuple[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["instance", "result", "seconds", "exit"]:
            raise ValueError(f"{path}: expected export-cactus four-column CSV")
        for row in reader:
            name = row["instance"]
            if name in rows:
                raise ValueError(f"{path}: duplicate {name}")
            seconds = float(row["seconds"])
            if not math.isfinite(seconds) or seconds < 0 or seconds > CAP:
                raise ValueError(f"{path}: invalid 60 s time for {name}: {seconds}")
            if row["result"] not in (SOLVED | {"UNKNOWN", "TIMEOUT",
                                             "RESOURCE_LIMIT", "ERROR"}):
                raise ValueError(f"{path}: unexpected result for {name}: {row['result']}")
            rows[name] = (row["result"], seconds)
    if set(rows) != set(ids):
        raise ValueError(f"{path}: missing {len(set(ids) - set(rows))} and "
                         f"extra {len(set(rows) - set(ids))} IDs")
    sidecar = path.with_suffix(".raw.tsv")
    with sidecar.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["instance", "result", "exit_code", "seconds", "cap_s"]:
            raise ValueError(f"{sidecar}: expected export-cactus raw sidecar")
        seen = set()
        for row in reader:
            name = row["instance"]
            if name in seen or name not in rows or float(row["cap_s"]) != CAP:
                raise ValueError(f"{sidecar}: duplicate, extra, or non-60 s row {name}")
            seen.add(name)
            result, seconds = rows[name]
            expected = {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}.get(
                row["result"], row["result"])
            if expected != result or (result not in SOLVED and seconds != CAP):
                raise ValueError(f"{sidecar}: status mismatch for {name}")
            if result in SOLVED and float(row["seconds"]) != seconds:
                raise ValueError(f"{sidecar}: solved time mismatch for {name}")
        if seen != set(ids):
            raise ValueError(f"{sidecar}: missing {len(set(ids) - seen)} IDs")
    return rows


def rank(legs: dict[str, dict[str, tuple[str, float]]], ids: list[str]
         ) -> list[tuple[int, float, tuple[str, ...], tuple[int, ...]]]:
    for name in ids:
        verdicts = {legs[arm][name][0] for arm in ARMS if legs[arm][name][0] in SOLVED}
        if len(verdicts) > 1:
            raise ValueError(f"conflicting solved verdicts for {name}: {verdicts}")
    ranked = []
    for size in (4, 5):
        for subset in itertools.combinations(ARMS, size):
            solved = 0
            solved_seconds = 0.0
            unique = [0] * size
            for name in ids:
                successes = [(index, legs[arm][name][1]) for index, arm in enumerate(subset)
                             if legs[arm][name][0] in SOLVED]
                if successes:
                    solved += 1
                    solved_seconds += min(seconds for _, seconds in successes)
                    if len(successes) == 1:
                        unique[successes[0][0]] += 1
            ranked.append((solved, par2(solved_seconds, len(ids) - solved, CAP),
                           subset, tuple(unique)))
    return sorted(ranked, key=lambda item: (-item[0], item[1], item[2]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", type=pathlib.Path, required=True,
                        help="original-name SYNTCOMP26 all.list")
    parser.add_argument("--leg", action="append", required=True, metavar="ARM=CSV",
                        help="one export-cactus CSV per arm; repeat for all six arms")
    args = parser.parse_args()
    paths = {}
    for spec in args.leg:
        arm, separator, path = spec.partition("=")
        if not separator or arm not in ARMS or arm in paths or not path:
            parser.error(f"invalid or duplicate --leg {spec!r}")
        paths[arm] = pathlib.Path(path)
    if set(paths) != set(ARMS):
        parser.error(f"expected exactly these six arms: {', '.join(ARMS)}")
    try:
        ids = read_ids(args.list)
        legs = {arm: read_leg(paths[arm], ids) for arm in ARMS}
        rows = rank(legs, ids)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print("Selection aid only: virtual-best isolated-arm times; run the chosen "
          "concurrent portfolio for its measured score.")
    print("Rule: maximize solved, then minimize PAR-2 at 60 s. Unique counts are "
          "solves by only that arm within the subset.")
    all_arm_unique = {
        arm: sum(legs[arm][name][0] in SOLVED
                 and all(legs[other][name][0] not in SOLVED
                         for other in ARMS if other != arm)
                 for name in ids)
        for arm in ARMS
    }
    print("Unique contributions among all six: " + ", ".join(
        f"{arm}={all_arm_unique[arm]}" for arm in ARMS))
    for position, (solved, score, subset, unique) in enumerate(rows[:10], 1):
        print(f"{position:2}. solved={solved}/1524 PAR-2={score:.6f} s "
              f"arms={','.join(subset)}")
        print("    unique: " + ", ".join(f"{arm}={count}"
                                       for arm, count in zip(subset, unique)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
