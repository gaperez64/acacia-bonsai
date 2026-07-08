#!/usr/bin/env python3
"""Aggregate Meson benchmark slice logs into one JSON-lines file per config."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from collections import defaultdict

from benchlib import load_meson_jsonl


SLICE_RE = re.compile(r"^(?P<config>.+)-slice-(?P<slice>\d+)-of-(?P<total>\d+)\.json$")


def summarize(rows: list[dict]) -> tuple[int, int, int, float]:
    timeouts = sum(1 for row in rows if row.get("result") == "TIMEOUT")
    failures = sum(1 for row in rows if row.get("result") not in ("OK", "TIMEOUT", "SKIP"))
    duration = sum(float(row.get("duration", 0) or 0) for row in rows)
    return len(rows), timeouts, failures, duration


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "logs_dir",
        nargs="?",
        default="_bm-logs",
        help="directory containing <config>-slice-N-of-M.json files",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="output directory; default is <logs_dir>/aggregated",
    )
    parser.add_argument(
        "--config",
        action="append",
        default=[],
        help="aggregate only this config; may be passed multiple times",
    )
    parser.add_argument(
        "--min-slices",
        type=int,
        default=1,
        help="skip otherwise complete groups with fewer than this many slices",
    )
    args = parser.parse_args()

    logs_dir = pathlib.Path(args.logs_dir)
    out_dir = pathlib.Path(args.out) if args.out else logs_dir / "aggregated"
    wanted = set(args.config)

    groups: dict[tuple[str, int], dict[int, pathlib.Path]] = defaultdict(dict)
    for path in sorted(logs_dir.glob("*.json")):
        match = SLICE_RE.match(path.name)
        if not match:
            continue
        config = match.group("config")
        if wanted and config not in wanted:
            continue
        slice_no = int(match.group("slice"))
        total = int(match.group("total"))
        groups[(config, total)][slice_no] = path

    complete: dict[str, tuple[int, dict[int, pathlib.Path]]] = {}
    for (config, total), slices in groups.items():
        if total < args.min_slices:
            print(f"skip {config} {total} slices (< --min-slices {args.min_slices})", file=sys.stderr)
            continue
        missing = [i for i in range(1, total + 1) if i not in slices]
        if missing:
            print(
                f"skip {config} {len(slices)}/{total} slices "
                f"(missing {','.join(map(str, missing[:8]))}"
                f"{'...' if len(missing) > 8 else ''})",
                file=sys.stderr,
            )
            continue
        previous = complete.get(config)
        if previous is None or total > previous[0]:
            complete[config] = (total, slices)

    if not complete:
        print("no complete slice groups found", file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    for config, (total, slices) in sorted(complete.items()):
        rows: list[dict] = []
        seen: set[str] = set()
        duplicates: list[str] = []
        for slice_no in range(1, total + 1):
            for row in load_meson_jsonl(slices[slice_no]):
                name = row.get("name", "")
                if name in seen:
                    duplicates.append(name)
                seen.add(name)
                rows.append(row)
        if duplicates:
            print(f"warning: {config} has {len(duplicates)} duplicate test names", file=sys.stderr)

        out_path = out_dir / f"{config}.json"
        with out_path.open("w") as handle:
            for row in sorted(rows, key=lambda item: item.get("name", "")):
                handle.write(f"{json.dumps(row)}\n")

        count, timeouts, failures, duration = summarize(rows)
        print(
            f"{config}: slices={total} rows={count} "
            f"timeouts={timeouts} failures={failures} duration={duration:.2f}s"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
