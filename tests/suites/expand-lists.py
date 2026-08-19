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

An optional ``sources.tsv`` beside the list files maps logical filenames to a
shared corpus path relative to ``tests/ltl``.  Its tab-separated header is
``instance\tsource``.  When present, every listed instance must be mapped.

Output format (one record per line, tab-separated):

    KIND \\t FOLDER \\t SUITE \\t FILE \\t SOURCE

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
from typing import Dict, List, Set

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


def parse_sources(path: pathlib.Path) -> Dict[str, str]:
    """Read a logical-instance to ``tests/ltl``-relative source map."""
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "instance\tsource":
        raise ValueError(f"{path}: expected 'instance\\tsource' header")
    sources: Dict[str, str] = {}
    for line_number, raw in enumerate(lines[1:], 2):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 2:
            raise ValueError(f"{path}:{line_number}: expected two tab-separated fields")
        instance, source = fields
        if pathlib.PurePosixPath(instance).name != instance or not instance.endswith(".ltl"):
            raise ValueError(f"{path}:{line_number}: invalid instance {instance!r}")
        source_path = pathlib.PurePosixPath(source)
        if source_path.is_absolute() or ".." in source_path.parts or not source.endswith(".ltl"):
            raise ValueError(f"{path}:{line_number}: invalid source {source!r}")
        if instance in sources:
            raise ValueError(f"{path}:{line_number}: duplicate instance {instance!r}")
        sources[instance] = source
    return sources


def main(out=sys.stdout):
    for kind_dir, kind_char in [("tests", "T"), ("benchmarks", "B")]:
        base = ROOT / kind_dir
        if not base.is_dir():
            continue
        for folder_dir in sorted(p for p in base.iterdir() if p.is_dir()):
            folder = folder_dir.name
            sources_path = folder_dir / "sources.tsv"
            sources = parse_sources(sources_path) if sources_path.is_file() else None
            for list_file in sorted(folder_dir.glob("*.list")):
                suite = list_file.stem
                skipped = suite.startswith("!")
                active = ("S" + kind_char) if skipped else kind_char
                for entry in parse_list(list_file):
                    if sources is None:
                        source = f"{folder}/{entry}"
                    else:
                        try:
                            source = sources[entry]
                        except KeyError as error:
                            raise KeyError(
                                f"{sources_path}: no source for {entry!r} "
                                f"referenced by {list_file.name}"
                            ) from error
                    out.write(f"{active}\t{folder}\t{suite}\t{entry}\t{source}\n")


if __name__ == "__main__":
    main()
