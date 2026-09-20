#!/usr/bin/env python3
"""Freeze exact CPre captures into a hashed dual-frontier campaign manifest."""

from __future__ import annotations

import argparse
import csv
import hashlib
import os
from pathlib import Path
import sys
import tempfile


FIELDS = [
    "instance",
    "family",
    "source_hash",
    "automaton_hash",
    "event_hash",
    "worker",
    "actual_backend",
    "K",
    "bool_threshold",
    "dimensions",
    "loop",
    "bound_epoch",
    "capture_kind",
    "before_count",
    "action_count",
    "transition_count",
    "event_path",
    "complete_capture",
    "skip_reason",
    "capture_binary_sha256",
    "binary_sha256",
    "config_hash",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--capture",
        action="append",
        required=True,
        metavar="FAMILY,INSTANCE,SOURCE,DIR",
        help="repeat for each captured automaton directory",
    )
    parser.add_argument("--solver", required=True, type=Path)
    parser.add_argument("--replay", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def metadata(directory: Path) -> dict[str, str]:
    with (directory / "meta.tsv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 1:
        raise ValueError(f"{directory}/meta.tsv does not have exactly one row")
    return rows[0]


def event_number(path: Path) -> int:
    return int(path.stem.removeprefix("cpre-"))


def header_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.removeprefix("#").strip().split():
        if "=" in token:
            name, value = token.split("=", 1)
            fields[name] = value
    required = {"schema_version", "loop", "k", "actions", "before"}
    if fields.get("schema_version") != "2" or not required <= fields.keys():
        raise ValueError("invalid CPre event header")
    return fields


def inspect_event(path: Path) -> tuple[dict[str, str], int, bool, str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        return {}, 0, False, "empty event"
    try:
        header = header_fields(lines[0])
    except ValueError as error:
        return {}, 0, False, str(error)

    try:
        before_marker = lines.index("[before]")
        actions_marker = lines.index("[actions]")
        after_marker = next(i for i, line in enumerate(lines) if line.startswith("[after]\t"))
        declared_after = int(lines[after_marker].split("\t", 1)[1])
    except (ValueError, StopIteration, IndexError):
        return header, 0, False, "truncated or misordered sections"

    transitions = sum(
        1
        for line in lines[actions_marker + 1 : after_marker]
        if line and not line.startswith("action\t")
    )
    before_rows = actions_marker - before_marker - 1
    after_rows = len(lines) - after_marker - 1
    if before_rows != int(header["before"]):
        return header, transitions, False, "before count mismatch"
    if after_rows != declared_after:
        return header, transitions, False, "after count mismatch or truncated event"
    return header, transitions, True, ""


def automaton_hash(directory: Path) -> str:
    hoa = directory / "automaton.hoa"
    if hoa.is_file():
        return sha256(hoa)
    skipped = directory / "automaton.hoa.skipped"
    return f"unavailable:{sha256(skipped)}" if skipped.is_file() else "unavailable"


def parse_capture(text: str) -> tuple[str, str, Path, Path]:
    parts = text.split(",", 3)
    if len(parts) != 4 or not parts[0] or not parts[1]:
        raise ValueError("--capture must be FAMILY,INSTANCE,SOURCE,DIR")
    family, instance, source, directory = parts
    source_path = Path(source).resolve()
    directory_path = Path(directory).resolve()
    if not source_path.is_file():
        raise ValueError(f"capture source is not a file: {source_path}")
    if not directory_path.is_dir():
        raise ValueError(f"capture directory is not a directory: {directory_path}")
    return family, instance, source_path, directory_path


def atomic_write(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent, text=True)
    try:
        with os.fdopen(descriptor, "w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, FIELDS, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def main() -> int:
    args = parse_args()
    capture_binary_hash = sha256(args.solver.resolve())
    replay_binary_hash = sha256(args.replay.resolve())
    config_hash = sha256(args.config.resolve())
    rows: list[dict[str, str]] = []
    for capture_text in args.capture:
        family, instance, source, directory = parse_capture(capture_text)
        meta = metadata(directory)
        epoch = -1
        previous_k = None
        for event_path in sorted(directory.glob("cpre-*.tsv"), key=event_number):
            header, transitions, complete, reason = inspect_event(event_path)
            k = header.get("k", "")
            if k != previous_k:
                epoch += 1
                previous_k = k
            before = header.get("before", "")
            capture_kind = "large-cpre" if before and int(before) >= 1024 else "control-cpre"
            rows.append(
                {
                    "instance": instance,
                    "family": family,
                    "source_hash": sha256(source),
                    "automaton_hash": automaton_hash(directory),
                    "event_hash": sha256(event_path),
                    "worker": meta.get("worker", "unknown"),
                    "actual_backend": "backward",
                    "K": k,
                    "bool_threshold": meta.get("bool_threshold", ""),
                    "dimensions": meta.get("states", ""),
                    "loop": header.get("loop", str(event_number(event_path))),
                    "bound_epoch": str(epoch),
                    "capture_kind": capture_kind,
                    "before_count": before,
                    "action_count": header.get("actions", ""),
                    "transition_count": str(transitions),
                    "event_path": str(event_path),
                    "complete_capture": "yes" if complete else "no",
                    "skip_reason": reason,
                    "capture_binary_sha256": capture_binary_hash,
                    "binary_sha256": replay_binary_hash,
                    "config_hash": config_hash,
                }
            )
    rows.sort(key=lambda row: (row["family"], row["instance"], int(row["loop"])))
    atomic_write(args.output, rows)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"freeze-dual-frontier-manifest: {error}", file=sys.stderr)
        raise SystemExit(2) from error
