#!/usr/bin/env python3
"""Derive mp-class inputs from existing native trusted_prepare phase records."""

import argparse
import csv
import json
from pathlib import Path


def derive(screen: Path, label: str) -> tuple[list[str], list[dict]]:
    selected, evidence = [], []
    with (screen / f"{label}.tsv").open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    for row in rows:
        directory = screen / "phases" / label / row["cap_s"] / (
            f"{row['instance']}-{row['run_index']}"
        )
        matches = []
        for file in sorted(directory.glob("*.jsonl")):
            for line_number, line in enumerate(file.read_text().splitlines(), 1):
                record = json.loads(line)
                if (record.get("event") == "decline"
                        and record.get("route") == "trusted_prepare"
                        and record.get("stage") == "mp-class"
                        and record.get("reason") == "mp-class"):
                    matches.append(f"{file}:{line_number}")
        if matches:
            selected.append(row["instance"])
            evidence.append({"instance": row["instance"], "records": matches})
    if len(selected) != len(set(selected)):
        raise ValueError("duplicate logical inputs in screen")
    return selected, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screen", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--label", default="G-off")
    args = parser.parse_args()
    selected, evidence = derive(args.screen, args.label)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(f"{item}\n" for item in selected))
    args.output.with_suffix(".evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"{len(selected)} inputs -> {args.output}")


if __name__ == "__main__":
    main()
