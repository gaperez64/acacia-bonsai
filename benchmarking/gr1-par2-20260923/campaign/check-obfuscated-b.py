#!/usr/bin/env python3
"""Select 30 renamed TLSF inputs and compare B to archived original-name B."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
import random

HERE = pathlib.Path(__file__).resolve().parent


def read(path: pathlib.Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    result = {row["instance"]: row for row in rows}
    if len(rows) != len(result):
        raise ValueError(f"duplicate instance in {path}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("choose", "compare"))
    parser.add_argument("--seed", type=int, default=20260924)
    parser.add_argument("--list", type=pathlib.Path,
                        default=HERE / "generic-selection/all-obfuscated.list")
    parser.add_argument("--sample", type=pathlib.Path,
                        default=HERE / "generic-selection/B-obfuscated-30.list")
    parser.add_argument("--mapping", type=pathlib.Path,
                        default=HERE / "generic-selection/sealed-mapping.jsonl")
    parser.add_argument("--renamed-csv", type=pathlib.Path)
    parser.add_argument("--original-csv", type=pathlib.Path)
    args = parser.parse_args()
    if args.phase == "choose":
        ids = [line.strip() for line in args.list.read_text().splitlines() if line.strip()]
        if len(ids) != 1524 or len(set(ids)) != 1524:
            raise ValueError("expected 1,524 distinct obfuscated IDs")
        chosen = random.Random(args.seed).sample(ids, 30)
        args.sample.write_text("".join(f"{item}\n" for item in chosen), encoding="utf-8")
        print(f"selected 30 IDs with seed {args.seed}")
        return
    if args.renamed_csv is None or args.original_csv is None:
        parser.error("compare needs --renamed-csv and --original-csv")
    digest = hashlib.sha256(args.mapping.read_bytes()).hexdigest()
    if digest != args.mapping.with_suffix(".sha256").read_text().split()[0]:
        raise ValueError("sealed mapping hash mismatch")
    mapping = {row["obfuscated_id"]: row["original_id"] for row in
               map(json.loads, args.mapping.read_text().splitlines())}
    sample = [line.strip() for line in args.sample.read_text().splitlines() if line.strip()]
    renamed, original = read(args.renamed_csv), read(args.original_csv)
    if set(renamed) != set(sample) or len(sample) != 30 or len(set(sample)) != 30:
        raise ValueError("renamed B observations differ from the 30-ID sample")
    mismatches = [(obf, mapping[obf], renamed[obf]["result"], original[mapping[obf]]["result"])
                  for obf in sample if renamed[obf]["result"] != original[mapping[obf]]["result"]]
    print(f"B verdict matches: {30 - len(mismatches)}/30")
    for item in mismatches:
        print("MISMATCH", *item)
    if mismatches:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
