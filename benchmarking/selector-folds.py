#!/usr/bin/env python3
"""Build deterministic family and source-directory folds without reading labels.

Groups are ordered by decreasing size, then SHA-256 of their UTF-8 key. Each
whole group goes to the smallest fold, breaking load ties by fold index.
Output rows are sorted by instance, independently of either input's row order.
"""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import pathlib

from family_metadata import read_list


COLUMNS = ["instance", "family_key", "fold_family", "directory_key", "fold_directory"]


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--family-table", required=True, type=pathlib.Path)
    parser.add_argument("--folds", default=5, type=positive_int)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser


def read_families(path: pathlib.Path) -> dict[str, dict[str, str]]:
    required = {"logical_instance", "family_key", "origin", "origin_kind"}
    families = {}
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if not required <= set(reader.fieldnames or []):
            raise ValueError(f"{path}: requires columns {', '.join(sorted(required))}")
        for line, row in enumerate(reader, 2):
            if None in row or any(not row[key] for key in required):
                raise ValueError(f"{path}:{line}: malformed family row")
            instance = row["logical_instance"]
            if instance in families:
                raise ValueError(f"{path}:{line}: duplicate family row for {instance}")
            families[instance] = row
    return families


def directory_key(row: dict[str, str]) -> str:
    if row["origin_kind"] != "direct":
        return row["family_key"]
    prefix, separator, source = row["origin"].partition(":")
    if prefix != "direct" or not separator or not source:
        raise ValueError(f"invalid direct origin: {row['origin']!r}")
    return "direct:" + str(pathlib.PurePosixPath(source).parent)


def assign_groups(keys: list[str], folds: int) -> dict[str, int]:
    if folds < 1:
        raise ValueError("fold count must be positive")
    counts = collections.Counter(keys)
    ordered = sorted(
        counts, key=lambda key: (-counts[key], hashlib.sha256(key.encode("utf-8")).digest())
    )
    loads = [0] * folds
    assignment = {}
    for key in ordered:
        fold = min(range(folds), key=lambda index: (loads[index], index))
        assignment[key] = fold
        loads[fold] += counts[key]
    return assignment


def build(list_path: pathlib.Path, family_path: pathlib.Path, folds: int) -> list[dict]:
    instances = read_list(list_path)
    if len(instances) != len(set(instances)):
        raise ValueError(f"{list_path}: duplicate listed instance")
    families = read_families(family_path)
    rows = []
    for instance in sorted(instances):
        if instance not in families:
            raise ValueError(f"listed instance {instance!r} missing from family table {family_path}")
        family = families[instance]
        rows.append({"instance": instance, "family_key": family["family_key"],
                     "directory_key": directory_key(family)})
    for group, column in (("family_key", "fold_family"), ("directory_key", "fold_directory")):
        assignment = assign_groups([row[group] for row in rows], folds)
        for row in rows:
            row[column] = assignment[row[group]]
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argument_parser()
    args = parser.parse_args(argv)
    try:
        rows = build(args.list, args.family_table, args.folds)
        with args.output.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=COLUMNS, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
