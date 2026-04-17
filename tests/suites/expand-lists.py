#!/usr/bin/env python3
"""Flatten every `.list` file under tests/suites/{tests,benchmarks}/ into a
tab-separated manifest that meson can consume via run_command().

The list files are plain-text:
  - one filename per line
  - `#` introduces a line comment; blank lines are ignored
  - `@include other` (where `other` is a suite name in the same folder)
    inlines the contents of `<folder>/<other>.list`; inclusions are
    resolved recursively (acyclically).
  - suites whose stem starts with `!` are loaded but their entries are
    emitted with an `S` (skip) marker so meson can filter them out.

Output format (one record per line, tab-separated):

    KIND \\t FOLDER \\t SUITE \\t FILE

where KIND is one of:
    T  — tests() entry, suite is active
    B  — benchmark() entry, suite is active
    ST — tests() entry, suite is marked skipped (name starts with `!`)
    SB — benchmark() entry, suite is marked skipped

Running this script standalone prints the manifest to stdout.
"""
from __future__ import annotations

import pathlib
import sys
from typing import List, Set

ROOT = pathlib.Path(__file__).resolve().parent


def parse_list(path: pathlib.Path, seen: Set[pathlib.Path] = None) -> List[str]:
    """Read `path`, resolving `@include` directives relative to its folder."""
    if seen is None:
        seen = set()
    real = path.resolve()
    if real in seen:
        # Cycle: just return empty.
        return []
    seen = seen | {real}
    if not path.exists():
        raise FileNotFoundError(path)

    entries: List[str] = []
    folder = path.parent
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("@include "):
            target = line[len("@include ") :].strip()
            if not target.endswith(".list"):
                target += ".list"
            entries.extend(parse_list(folder / target, seen))
        else:
            entries.append(line)
    return entries


def main(out=sys.stdout):
    for kind_dir, kind_char in [("tests", "T"), ("benchmarks", "B")]:
        base = ROOT / kind_dir
        if not base.is_dir():
            continue
        for folder_dir in sorted(p for p in base.iterdir() if p.is_dir()):
            folder = folder_dir.name
            for list_file in sorted(folder_dir.glob("*.list")):
                suite = list_file.stem
                skipped = suite.startswith("!")
                active = ("S" + kind_char) if skipped else kind_char
                for entry in parse_list(list_file):
                    out.write(f"{active}\t{folder}\t{suite}\t{entry}\n")


if __name__ == "__main__":
    main()
