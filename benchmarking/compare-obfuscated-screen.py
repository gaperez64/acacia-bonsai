#!/usr/bin/env python3
"""Compare uniform-cap plain and obfuscated runner TSVs on the accepted panel."""

from __future__ import annotations

import argparse
import math
import pathlib
import sys

from benchlib import load_tool, par2

obfuscate = load_tool("benchmarking/obfuscate-corpus.py")
coverage = obfuscate.coverage
OUTPUT_COLUMNS = [
    "instance", "obfuscation", "exclusion_reason", "plain_result", "obfuscated_result",
    "plain_seconds", "obfuscated_seconds", "delta_s", "time_change", "coverage_change",
    "verdict_conflict",
]


def read_screen(path):
    rows = coverage.load_output(path)
    indexed = {}
    for row in rows:
        name = row["instance"]
        if not name or name in indexed:
            raise coverage.CoverageError(f"{path}: empty or duplicate instance {name!r}")
        try:
            seconds = float(row["seconds"])
            if not math.isfinite(seconds) or seconds < 0:
                raise ValueError
            if row["result"] in coverage.DECISIVE_RESULTS:
                expected_exit = "0" if row["result"] == "REALIZABLE" else "1"
                if row["exit_code"] != expected_exit:
                    raise ValueError
        except ValueError:
            raise coverage.CoverageError(f"{path}: invalid seconds or exit_code for {name}") from None
        indexed[name] = row
    if len({row["cap_s"] for row in rows}) > 1 or len({row["solver_label"] for row in rows}) > 1:
        raise coverage.CoverageError(f"{path}: screen must have one cap and one solver label")
    return indexed


def nonnegative_seconds(value):
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError("must be finite and nonnegative")
    return number


def compare(args):
    manifest = obfuscate.read_manifest(args.manifest)
    plain = read_screen(args.plain)
    obfuscated = read_screen(args.obfuscated)
    included = {name for name, row in manifest.items() if row["exit_status"] == "0"}
    if not manifest or not set(manifest) <= plain.keys() or set(obfuscated) != included:
        raise coverage.CoverageError(
            "instance panels disagree: plain must cover the manifest; "
            "obfuscated must cover exactly its successful instances",
        )
    cap = int(next(iter(plain.values()))["cap_s"])
    if any(int(row["cap_s"]) != cap for row in obfuscated.values()):
        raise coverage.CoverageError("plain and obfuscated screens must have the same cap")
    rows = []
    conflicts = plain_only = obfuscated_only = 0
    solved = [0, 0]
    solved_seconds = [0.0, 0.0]
    for name, entry in manifest.items():
        left = plain[name]
        row = dict.fromkeys(OUTPUT_COLUMNS, "")
        row.update(instance=name, plain_result=left["result"], plain_seconds=left["seconds"],
                   obfuscation="excluded" if name not in included else "included",
                   exclusion_reason=entry["reason"])
        if name in included:
            right = obfuscated[name]
            answers = [item["result"] in coverage.DECISIVE_RESULTS for item in (left, right)]
            for index, (item, answered) in enumerate(zip((left, right), answers)):
                solved[index] += answered
                if answered:
                    solved_seconds[index] += float(item["seconds"])
            conflict = all(answers) and left["result"] != right["result"]
            conflicts += conflict
            flip = "none"
            if answers == [True, False]:
                flip = "plain-only"
                plain_only += 1
            elif answers == [False, True]:
                flip = "obfuscated-only"
                obfuscated_only += 1
            delta = float(right["seconds"]) - float(left["seconds"])
            change = "within-noise" if abs(delta) <= args.noise_s else (
                "slower" if delta > 0 else "faster")
            row.update(obfuscated_result=right["result"], obfuscated_seconds=right["seconds"],
                       delta_s=f"{delta:.6f}", time_change=change, coverage_change=flip,
                       verdict_conflict=str(conflict).lower())
        rows.append(row)
    coverage.atomic_write_tsv(args.output, OUTPUT_COLUMNS, rows)
    scores = [par2(seconds, len(included) - count, cap)
              for seconds, count in zip(solved_seconds, solved)]
    print(f"paired inputs: {len(included)}; excluded by obfuscation: {len(manifest) - len(included)}")
    print(f"solved: plain={solved[0]} obfuscated={solved[1]}")
    print(f"PAR-2 total (s), cap={cap}: plain={scores[0]:.6f} obfuscated={scores[1]:.6f}")
    print(f"coverage flips: plain-only={plain_only} obfuscated-only={obfuscated_only}")
    print(f"verdict conflicts: {conflicts}; per-instance results: {args.output}")
    return 3 if conflicts else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plain", required=True, type=pathlib.Path)
    parser.add_argument("--obfuscated", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--noise-s", type=nonnegative_seconds, default=0,
                        help="absolute per-instance time delta treated as noise (default: 0)")
    args = parser.parse_args(argv)
    try:
        return compare(args)
    except (coverage.CoverageError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
