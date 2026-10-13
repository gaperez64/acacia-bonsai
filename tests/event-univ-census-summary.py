#!/usr/bin/env python3
"""Summarize guarded frontend census rows and bind the diagnostic artifacts."""

import argparse
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SHIPPING = {"real-components", "formula-unreal", "automaton-unreal"}
FRAMES = SHIPPING | {"raw-positive", "raw-negative", "simplified-positive", "simplified-negative"}
FIELDS = ("source_row", "status", "frame", "component", "expected_components",
          "distinct_promises", "width", "guard_fires")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def read_input_status(path, source_inputs):
    statuses = {}
    with path.open() as source:
        for row in csv.reader(source, delimiter="\t"):
            if len(row) != 3:
                raise ValueError(f"invalid input status row: {row}")
            ordinal, status, seconds = int(row[0]), row[1], float(row[2])
            if (not 1 <= ordinal <= source_inputs or ordinal in statuses or
                    not re.fullmatch(r"ok|exhausted|error_[1-9][0-9]*", status) or
                    not math.isfinite(seconds) or seconds < 0):
                raise ValueError(f"invalid input status row: {row}")
            statuses[ordinal] = (status, seconds)
    return statuses


def summarize(ids, census, input_status=None, allow_incomplete=False):
    if not ids:
        raise ValueError("empty source map")
    statuses = None if input_status is None else read_input_status(input_status, len(ids))
    frames = {name: {"rows": 0, "inputs": {}} for name in FRAMES}
    partial_rows = 0
    interrupted = set()
    with census.open() as source:
        reader = csv.DictReader(source, delimiter="\t")
        if reader.fieldnames != list(FIELDS):
            raise ValueError("invalid census header")
        for row in reader:
            ordinal = int(row["source_row"])
            if not 1 <= ordinal <= len(ids):
                raise ValueError(f"invalid census row: {row}")
            if None in row or any(value in (None, "") for value in row.values()):
                if not allow_incomplete:
                    raise ValueError("truncated census row or missing expected component count")
                partial_rows += 1
                interrupted.add(ordinal)
                continue
            name = row["frame"]
            component = int(row["component"])
            expected = int(row["expected_components"])
            promises, width = int(row["distinct_promises"]), int(row["width"])
            if (name not in FRAMES or row["status"] != "ok" or
                    not 1 <= component <= expected or
                    (name not in SHIPPING and expected != 1) or
                    promises < 0 or width <= 0 or
                    row["guard_fires"] != str(int(promises > width))):
                raise ValueError(f"invalid census row: {row}")
            bucket = frames[name]
            entry = bucket["inputs"].setdefault(
                ordinal, {"expected": expected, "components": set(), "guard": False})
            if entry["expected"] != expected or component in entry["components"]:
                raise ValueError("inconsistent or duplicate census component")
            entry["components"].add(component)
            bucket["rows"] += 1
            entry["guard"] |= row["guard_fires"] == "1"
    ready = set(range(1, len(ids) + 1)) - interrupted
    for frame in frames.values():
        ready &= {ordinal for ordinal, entry in frame["inputs"].items()
                  if len(entry["components"]) == entry["expected"]}
    if statuses is not None:
        ready &= {ordinal for ordinal, (status, _) in statuses.items() if status == "ok"}
    complete = len(ready) == len(ids)
    if not complete and not allow_incomplete:
        raise ValueError("census did not complete every input, frame and component successfully")

    def counts(inputs):
        return {"inputs": len(inputs), "unchanged": sum(not guard for guard in inputs.values()),
                "guard_fires": sum(inputs.values()),
                "guard_input_ordinals": sorted(ordinal for ordinal, guard in inputs.items()
                                               if guard)}

    union = {ordinal: any(frames[name]["inputs"][ordinal]["guard"] for name in SHIPPING)
             for ordinal in sorted(ready)}
    summary = {"source_inputs": len(ids), "complete": complete,
               "truncated_rows": partial_rows, "unknown_inputs": len(ids) - len(ready),
               "unknown_shipping_inputs": len(ids) - len(union),
               "unknown_input_ordinals": sorted(set(range(1, len(ids) + 1)) - ready),
               "frontend_only": True,
               "reaches_live_translation": "not determined by the census",
               "frames": {name: {"translation_frames": frame["rows"],
                                 **counts({ordinal: frame["inputs"][ordinal]["guard"]
                                           for ordinal in ready})}
                          for name, frame in sorted(frames.items())},
               "shipping_union": counts(union)}
    if statuses is not None:
        summary["input_status_counts"] = dict(sorted(Counter(
            status for status, _ in statuses.values()).items()))
        summary["input_status_counts"]["missing"] = len(ids) - len(statuses)
        summary["input_status_seconds"] = sum(seconds for _, seconds in statuses.values())
        summary["exhausted_input_ordinals"] = sorted(
            ordinal for ordinal, (status, _) in statuses.items() if status == "exhausted")
    return summary, union


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_map", type=Path)
    parser.add_argument("census", type=Path)
    parser.add_argument("tool", type=Path)
    parser.add_argument("--input-status", type=Path,
                        help="headerless TSV: source-map ordinal, ok|exhausted|error_N, seconds")
    parser.add_argument("--allow-incomplete", action="store_true",
                        help="write explicitly partial counts after an interrupted census")
    args = parser.parse_args()
    with args.source_map.open() as source:
        ids = [row["instance"] for row in csv.DictReader(source, delimiter="\t")]
    summary, union = summarize(ids, args.census, args.input_status, args.allow_incomplete)
    output = args.census.parent
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (output / "guard-firing.list").write_text(
        ("# Complete census.\n" if summary["complete"] else
         "# INCOMPLETE census; membership is partial.\n") +
        "# Inputs with a prospective shipping translation guard.\n" +
        "".join(ids[ordinal - 1] + "\n" for ordinal, guard in union.items() if guard))
    build = args.tool.resolve().parents[1]
    identity = {"source_map": str(args.source_map.resolve()),
                "source_map_sha256": digest(args.source_map),
                "census_sha256": digest(args.census), "tool": str(args.tool.resolve()),
                "tool_sha256": digest(args.tool), "git_head": git("rev-parse", "HEAD"),
                "git_branch": git("rev-parse", "--abbrev-ref", "HEAD"),
                "git_status": git("status", "--porcelain"),
                "git_diff_sha256": hashlib.sha256(
                    subprocess.check_output(["git", "diff", "HEAD"], cwd=ROOT)).hexdigest(),
                "tlsf_tools_head": git("-C", "subprojects/tlsf-tools", "rev-parse", "HEAD"),
                "build_options": json.loads(
                    (build / "meson-info/intro-buildoptions.json").read_text()),
                "tool_sources_sha256": {name: digest(ROOT / name) for name in (
                    "tests/event_univ_census.cc", "tests/event-univ-census-summary.py",
                    "tests/event-univ-census-isolated.py", "src/solver/translator_options.hh",
                    "src/tlsf_frontend.cc", "src/tlsf_frontend.hh")},
                "linked_libraries": subprocess.check_output(["ldd", str(args.tool.resolve())],
                                                            text=True),
                "compiler": json.loads((build / "meson-info/intro-compilers.json").read_text())}
    if args.input_status is not None:
        identity["input_status"] = str(args.input_status.resolve())
        identity["input_status_sha256"] = digest(args.input_status)
    (output / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
