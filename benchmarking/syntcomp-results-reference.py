#!/usr/bin/env python3
"""Convert official SYNTCOMP result CSV rows into panel-selection references."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib

from suite_paths import load_source_map


VERDICTS = {"0": "REALIZABLE", "1": "UNREALIZABLE"}


def parse_series(raw: str) -> tuple[str, str]:
    if "=" not in raw:
        raise argparse.ArgumentTypeError("series must be LABEL=SOLVER_ID")
    label, solver_id = raw.split("=", 1)
    if not label or not solver_id:
        raise argparse.ArgumentTypeError(
            "series must have a nonempty label and solver ID"
        )
    return label, solver_id


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    corpus_group = parser.add_mutually_exclusive_group(required=True)
    corpus_group.add_argument("--corpus", type=pathlib.Path)
    corpus_group.add_argument(
        "--source-map",
        type=pathlib.Path,
        help="suite sources.tsv for a shared/content-addressed corpus",
    )
    parser.add_argument("--tlsf-dir", required=True, type=pathlib.Path)
    parser.add_argument("--reference", required=True, type=pathlib.Path)
    parser.add_argument("--selection", required=True, type=pathlib.Path)
    parser.add_argument("--series", action="append", required=True, type=parse_series)
    parser.add_argument("--expected", required=True, type=int)
    parser.add_argument("--cap", type=float, default=17.0)
    parser.add_argument("--source-description", default="official SYNTCOMP results")
    args = parser.parse_args()

    logical_sources = load_source_map(args.source_map) if args.source_map else None

    rows: list[dict[str, str]] = []
    with args.results.open(encoding="utf-8", newline="") as stream:
        rows.extend(csv.DictReader(stream))
    names = sorted({row["inst"] for row in rows})
    if len(names) != args.expected:
        raise ValueError(f"expected {args.expected} official instances, found {len(names)}")
    missing_tlsf = [
        name for name in names if not (args.tlsf_dir / f"{name}.tlsf").is_file()
    ]
    if missing_tlsf:
        raise FileNotFoundError(f"official instances missing from release: {missing_tlsf[:10]}")

    args.selection.parent.mkdir(parents=True, exist_ok=True)
    args.selection.write_text(
        "# Exact TLSF selection extracted from official result instance names.\n"
        f"# Source: {args.source_description}; instances={len(names)}.\n"
        + "".join(f"{name}.tlsf\n" for name in names),
        encoding="utf-8",
    )

    args.reference.mkdir(parents=True, exist_ok=True)
    provenance: dict[str, object] = {
        "cap_seconds": args.cap,
        "instance_count": len(names),
        "results_csv": str(args.results.resolve()),
        "source_description": args.source_description,
        "series": {},
    }
    for label, solver_id in args.series:
        selected = {row["inst"]: row for row in rows if row["id"] == solver_id}
        output_path = args.reference / f"{label}.json"
        answered_count = 0
        with output_path.open("w", encoding="utf-8") as output:
            for name in names:
                source = selected.get(name)
                duration = args.cap
                result = "TIMEOUT"
                stdout = ""
                if source is not None:
                    try:
                        official_duration = float(source["timeSolveWall"])
                    except (TypeError, ValueError):
                        official_duration = args.cap
                    duration = min(official_duration, args.cap)
                    verdict = VERDICTS.get(source["resultSolve"])
                    answered = (
                        source["statusSolve"] == "ok"
                        and verdict is not None
                        and official_duration < args.cap
                    )
                    if answered:
                        answered_count += 1
                        result = "OK"
                        stdout = f"[official] {verdict}\n"
                if logical_sources is not None:
                    try:
                        ltl_path = logical_sources[f"{name}.ltl"]
                    except KeyError as error:
                        raise KeyError(
                            f"{args.source_map}: no source for {name}.ltl"
                        ) from error
                else:
                    ltl_path = args.corpus / f"{name}.ltl"
                materialized = {
                    "name": f"official/{label}/{name}.ltl",
                    "command": [
                        "official-reference",
                        "-F",
                        str(ltl_path.resolve()),
                    ],
                    "result": result,
                    "duration": duration,
                    "stdout": stdout,
                    "stderr": "",
                }
                output.write(json.dumps(materialized, sort_keys=True) + "\n")
        provenance["series"][label] = {
            "solver_id": solver_id,
            "official_rows": len(selected),
            "answered_before_cap": answered_count,
            "materialized_rows": len(names),
        }
    (args.reference / "provenance.json").write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
