#!/usr/bin/env python3
"""Gate native and SyFCo-converted TLSF routes on Acacia verdict parity."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
import pathlib
import subprocess
import sys

from benchlib import campaign_scope_guard, classify_acacia_run, read_part, run_systemd_scope


SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def keep_verdict_line(line: str) -> bool:
    """Retain only output that can affect verdict classification."""
    upper = line.upper()
    return (
        "REALIZABLE" in upper or "UNKNOWN" in upper or "TIMEOUT" in upper
    )


def classify(run) -> str:
    return classify_acacia_run(run)


def write_rows(path: pathlib.Path, rows: list[dict[str, object]]) -> None:
    fields = [
        "instance",
        "native_result",
        "native_seconds",
        "native_exit",
        "converted_result",
        "converted_seconds",
        "converted_exit",
        "comparison",
    ]
    temporary = path.with_name(f".{path.name}.tmp")
    path.parent.mkdir(parents=True, exist_ok=True)
    with temporary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
        handle.flush()
    temporary.replace(path)


@campaign_scope_guard("tlsf-verdict-parity")
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--converted", required=True, type=pathlib.Path)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=17.0)
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--expect-count", type=int, default=1579)
    parser.add_argument("--csv", required=True, type=pathlib.Path)
    parser.add_argument("--summary", required=True, type=pathlib.Path)
    parser.add_argument("--status", required=True, type=pathlib.Path)
    parser.add_argument(
        "--resume",
        action="store_true",
        help="validate and continue an existing checkpoint CSV",
    )
    parser.add_argument("--prepare", action="store_true",
                        help="convert and spot-check, then run every available pair even on failure")
    parser.add_argument("--syfco", default="syfco")
    parser.add_argument("--native-inspect", type=pathlib.Path)
    parser.add_argument("--canonicalizer", type=pathlib.Path)
    args = parser.parse_args()
    preparation_exit = spot_check_exit = None
    spot_outcomes = "not run"
    if args.prepare:
        if not args.native_inspect or not args.canonicalizer:
            parser.error("--prepare needs --native-inspect and --canonicalizer")
        scripts = pathlib.Path(__file__).resolve().parent
        preparation_exit = subprocess.run(
            [sys.executable, str(scripts / "convert-tlsf-corpus.py"),
             str(args.source), str(args.converted), "--syfco", args.syfco,
             "--target", "Mealy", "--canonicalizer", str(args.canonicalizer)], check=False).returncode
        spot_check_exit = subprocess.run(
            [sys.executable, str(scripts / "check-tlsf-conversion.py"),
             str(args.source), str(args.converted), "--syfco", args.syfco,
             "--native-inspect", str(args.native_inspect),
             "--canonicalizer", str(args.canonicalizer),
             "--report", str(args.converted / "spot-check.tsv")], check=False).returncode
        try:
            with (args.converted / "spot-check.tsv").open(newline="") as stream:
                spot_outcomes = str(dict(Counter(row["outcome"] for row in
                                                csv.DictReader(stream, delimiter="\t"))))
        except (OSError, KeyError):
            spot_outcomes = "unavailable: spot-check report missing or malformed"

    names = [
        line.strip()
        for line in args.list.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if not names or len(set(names)) != len(names):
        parser.error("parity list must be nonempty and unique")
    if len(names) != args.expect_count:
        parser.error(f"expected {args.expect_count} commons, found {len(names)}")
    if not args.bin.is_file():
        parser.error(f"missing binary: {args.bin}")

    rows: list[dict[str, object]] = []
    if args.resume and args.csv.is_file():
        with args.csv.open(newline="") as handle:
            rows = list(csv.DictReader(handle))
        if len(rows) > len(names):
            parser.error("checkpoint has more rows than the instance list")
        for index, row in enumerate(rows):
            expected = pathlib.Path(names[index]).stem + ".tlsf"
            if row.get("instance") != expected:
                parser.error(
                    f"checkpoint row {index + 1} is {row.get('instance')!r}, "
                    f"expected {expected!r}"
                )

    args.status.parent.mkdir(parents=True, exist_ok=True)
    args.status.write_text(f"RUNNING {len(rows)}/{len(names)}\n")
    missing_conversions = 0
    opposite_verdicts = 0
    errors = 0
    native_only = 0
    converted_only = 0
    native_resource_limits = 0
    converted_resource_limits = 0
    for row in rows:
        comparison = row["comparison"]
        missing_conversions += comparison == "MISSING_CONVERSION"
        opposite_verdicts += comparison == "OPPOSITE_VERDICT"
        errors += comparison == "ERROR"
        native_only += comparison == "NATIVE_ONLY"
        converted_only += comparison == "CONVERTED_ONLY"
        native_resource_limits += row["native_result"] == "RESOURCE_LIMIT"
        converted_resource_limits += row["converted_result"] == "RESOURCE_LIMIT"

    for index, ltl_name in enumerate(names[len(rows) :], start=len(rows) + 1):
        stem = pathlib.Path(ltl_name).stem
        source = args.source / f"{stem}.tlsf"
        formula = args.converted / ltl_name
        part = args.converted / f"{stem}.part"
        if not source.is_file() or not formula.is_file() or not part.is_file():
            missing_conversions += 1
            rows.append({"instance": source.name, "comparison": "MISSING_CONVERSION",
                         "native_result": "NOT_RUN", "converted_result": "NOT_RUN"})
            write_rows(args.csv, rows)
            args.status.write_text(f"RUNNING {index}/{len(names)}\n")
            continue
        try:
            inputs, outputs = read_part(part)
        except (OSError, ValueError) as error:
            errors += 1
            rows.append({"instance": source.name, "comparison": "ERROR",
                         "native_result": "NOT_RUN", "converted_result": "ERROR"})
            print(f"preparation error {stem}: {error}")
            write_rows(args.csv, rows)
            args.status.write_text(f"RUNNING {index}/{len(names)}\n")
            continue

        native = run_systemd_scope(
            [str(args.bin), "-T", str(source)],
            args.timeout,
            args.memory_max,
            args.memory_swap_max,
            unit_prefix="acacia-tlsf-native",
            capture_filter=keep_verdict_line,
        )
        converted = run_systemd_scope(
            [
                str(args.bin),
                "-F",
                str(formula),
                "-i",
                inputs,
                "-o",
                outputs,
            ],
            args.timeout,
            args.memory_max,
            args.memory_swap_max,
            unit_prefix="acacia-tlsf-converted",
            capture_filter=keep_verdict_line,
        )
        native_result = classify(native)
        converted_result = classify(converted)
        native_resource_limits += native_result == "RESOURCE_LIMIT"
        converted_resource_limits += converted_result == "RESOURCE_LIMIT"
        comparison = "MATCH"
        if native_result == "ERROR" or converted_result == "ERROR":
            comparison = "ERROR"
            errors += 1
        elif native_result in SOLVED and converted_result in SOLVED:
            if native_result != converted_result:
                comparison = "OPPOSITE_VERDICT"
                opposite_verdicts += 1
        elif native_result in SOLVED:
            comparison = "NATIVE_ONLY"
            native_only += 1
        elif converted_result in SOLVED:
            comparison = "CONVERTED_ONLY"
            converted_only += 1
        elif native_result != converted_result:
            comparison = "NONSOLVED_DIFFERENCE"

        rows.append(
            {
                "instance": source.name,
                "native_result": native_result,
                "native_seconds": f"{native.seconds:.3f}",
                "native_exit": native.returncode,
                "converted_result": converted_result,
                "converted_seconds": f"{converted.seconds:.3f}",
                "converted_exit": converted.returncode,
                "comparison": comparison,
            }
        )
        write_rows(args.csv, rows)
        args.status.write_text(f"RUNNING {index}/{len(names)}\n")

    gate_pass = (opposite_verdicts == 0 and errors == 0 and missing_conversions == 0
                 and preparation_exit in (None, 0) and spot_check_exit in (None, 0))
    summary = (
        f"instances: {len(rows)}\n"
        f"missing conversions: {missing_conversions}\n"
        f"preparation exit: {preparation_exit if preparation_exit is not None else 'not run'}\n"
        f"spot-check outcomes: {spot_outcomes}\n"
        f"spot-check exit: {spot_check_exit if spot_check_exit is not None else 'not run'}\n"
        f"opposite verdicts: {opposite_verdicts}\n"
        f"frontend errors: {errors}\n"
        f"native-only answers: {native_only}\n"
        f"converted-only answers: {converted_only}\n"
        f"native resource limits: {native_resource_limits}\n"
        f"converted resource limits: {converted_resource_limits}\n"
        f"per-solver memory max: {args.memory_max}\n"
        f"per-solver swap max: {args.memory_swap_max}\n"
        f"GATE {'PASS' if gate_pass else 'FAIL'}\n"
    )
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.summary.write_text(summary)
    args.status.write_text(f"COMPLETE {'PASS' if gate_pass else 'FAIL'}\n")
    print(summary, end="")
    return 0 if gate_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
