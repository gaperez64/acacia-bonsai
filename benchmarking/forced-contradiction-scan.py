#!/usr/bin/env python3
"""Run the forced-output contradiction checker over a TLSF corpus.

The checker is a formula-level decision procedure, so asking whether it fires on
every instance of the official selection costs seconds rather than the hours a
solve campaign would take.  That is the whole point of scanning separately: the
"zero false matches over 1,524" requirement of the sprint handoff is checkable
long before any binary is raced.

Each file is scanned in its own process.  The checker allocates BuDDy variables
per call and never frees them, which is harmless for a solver child that runs
once but would accumulate across 1,586 files in a single process.

Usage:
    forced-contradiction-scan.py --scan-bin BIN --corpus DIR [--list FILE]
                                 [--tlsf-map FILE] [--output TSV]

--list and --tlsf-map together restrict the scan to the TLSF files named by a
benchmark list, so the count can be compared against coverage runs directly.
"""

from __future__ import annotations

import argparse
import collections
import csv
import pathlib
import subprocess
import sys


def selection_files(list_path: pathlib.Path, map_path: pathlib.Path) -> list[str]:
    """Return the TLSF basenames used by the instances named in a .list file."""
    wanted = set()
    for line in list_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            wanted.add(line)

    files: list[str] = []
    with map_path.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if row["instance"] in wanted:
                files.append(row["tlsf"])
    missing = len(wanted) - len(files)
    if missing:
        raise SystemExit(
            f"{missing} of {len(wanted)} instances in {list_path} have no TLSF source"
        )
    return sorted(set(files))


def family_of(name: str) -> str:
    """Strip the parameter suffix so results group by benchmark family."""
    stem = name.removesuffix(".tlsf")
    marker = stem.find("_pb_")
    return stem[:marker] if marker != -1 else stem


def scan_one(scan_bin: pathlib.Path, path: pathlib.Path) -> tuple[str, str]:
    """Return (verdict, detail) for one TLSF file.

    A crash is reported as its own verdict rather than being allowed to look
    like a decline: the difference matters, and the sprint rule is that a
    checker which cannot decide says so.
    """
    try:
        done = subprocess.run(
            [str(scan_bin), str(path)],
            capture_output=True,
            text=True,
            timeout=120,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", ""
    if done.returncode < 0:
        return "CRASH", f"signal {-done.returncode}"
    if done.returncode != 0:
        return "ERROR", done.stderr.strip().splitlines()[-1] if done.stderr else ""
    head, _, detail = done.stdout.strip().partition(" ")
    return head or "EMPTY", detail


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scan-bin", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--list", type=pathlib.Path)
    parser.add_argument("--tlsf-map", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--expect-matches", type=int)
    args = parser.parse_args()

    if bool(args.list) != bool(args.tlsf_map):
        parser.error("--list and --tlsf-map must be given together")

    if args.list:
        names = selection_files(args.list, args.tlsf_map)
    else:
        names = sorted(p.name for p in args.corpus.glob("*.tlsf"))
    if not names:
        raise SystemExit(f"no TLSF files found under {args.corpus}")

    rows = []
    tally: collections.Counter[str] = collections.Counter()
    matched_families: collections.Counter[str] = collections.Counter()
    for name in names:
        path = args.corpus / name
        if not path.exists():
            raise SystemExit(f"missing TLSF file {path}")
        verdict, detail = scan_one(args.scan_bin, path)
        tally[verdict] += 1
        if verdict == "MATCH":
            matched_families[family_of(name)] += 1
        rows.append({"tlsf": name, "verdict": verdict, "detail": detail})

    if args.output:
        with args.output.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(
                handle, fieldnames=["tlsf", "verdict", "detail"], delimiter="\t"
            )
            writer.writeheader()
            writer.writerows(rows)

    print(f"scanned {len(names)} files")
    for verdict, count in sorted(tally.items()):
        print(f"  {verdict:<8} {count}")
    if matched_families:
        print("matches by family:")
        for family, count in sorted(matched_families.items()):
            print(f"  {family:<28} {count}")

    failed = tally["CRASH"] + tally["ERROR"] + tally["TIMEOUT"] + tally["EMPTY"]
    if failed:
        print(f"FAIL: {failed} files did not produce a clean verdict", file=sys.stderr)
        return 1
    if args.expect_matches is not None and tally["MATCH"] != args.expect_matches:
        print(
            f"FAIL: expected {args.expect_matches} matches, got {tally['MATCH']}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
