#!/usr/bin/env python3
"""Spot-check generated .ltl/.part pairs by rerunning SyFCo exactly."""

from __future__ import annotations

import argparse
import csv
import hashlib
import pathlib
import random
import subprocess
import tempfile

from benchlib import read_part


def canonicalize_formula(
    canonicalizer: pathlib.Path, formula: str, timeout: float
) -> bytes:
    """Return a deterministic AST key modulo commutative Boolean ordering."""
    canonical = subprocess.run(
        [str(canonicalizer)],
        input=formula,
        check=True,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return canonical.stdout.rstrip().encode()


def normalize_formula(
    ltlfilt: str, canonicalizer: pathlib.Path, formula: str, timeout: float
) -> bytes:
    """Canonicalize after Spot removes and simplifies derived operators."""
    result = subprocess.run(
        [
            ltlfilt,
            "--unabbreviate=RWM",
            "--simplify",
            "--format=%f",
        ],
        input=formula,
        check=True,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return canonicalize_formula(canonicalizer, result.stdout, timeout)


def formula_keys(
    ltlfilt: str,
    canonicalizer: pathlib.Path,
    native: str,
    syfco: str,
    timeout: float,
) -> tuple[bytes, bytes]:
    """Use the cheap exact AST check before the more expensive Spot fallback."""
    native_key = canonicalize_formula(canonicalizer, native, timeout)
    syfco_key = canonicalize_formula(canonicalizer, syfco, timeout)
    if native_key == syfco_key:
        return native_key, syfco_key
    return (
        normalize_formula(ltlfilt, canonicalizer, native, timeout),
        normalize_formula(ltlfilt, canonicalizer, syfco, timeout),
    )


def inspect_native(
    executable: pathlib.Path, source: pathlib.Path, timeout: float
) -> tuple[str, str, str]:
    result = subprocess.run(
        [str(executable), str(source)],
        check=True,
        capture_output=True,
        timeout=timeout,
    )
    fields = result.stdout.split(b"\0")
    if len(fields) != 7 or fields[-1]:
        raise RuntimeError(f"malformed native inspector output for {source.name}")
    formula, inputs, outputs = (field.decode() for field in fields[:3])
    return formula, inputs, outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("converted", type=pathlib.Path)
    parser.add_argument("--syfco", default="syfco")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--seed", type=int, default=20260804)
    parser.add_argument(
        "--native-inspect",
        type=pathlib.Path,
        help="compare native formula semantics, literal serialization, and I/O lists",
    )
    parser.add_argument("--ltlfilt", default="ltlfilt")
    parser.add_argument(
        "--canonicalizer",
        type=pathlib.Path,
        help="Spot-linked helper that canonicalizes commutative Boolean operands",
    )
    parser.add_argument(
        "--stage-timeout",
        type=float,
        default=120,
        help="timeout in seconds for each SyFCo, native, and ltlfilt invocation",
    )
    parser.add_argument(
        "--only",
        action="append",
        default=[],
        metavar="STEM",
        help="check this instance stem instead of taking a random sample (repeatable)",
    )
    parser.add_argument(
        "--debug-dir",
        type=pathlib.Path,
        help="write native/SyFCo values for mismatching instances",
    )
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--status", type=pathlib.Path)
    args = parser.parse_args()

    sources = sorted(
        source
        for source in args.source.glob("*.tlsf")
        if (args.converted / f"{source.stem}.ltl").is_file()
    )
    if args.stage_timeout <= 0:
        parser.error("--stage-timeout must be positive")
    if args.native_inspect and not args.canonicalizer:
        parser.error("--native-inspect requires --canonicalizer")
    if args.only:
        by_stem = {source.stem: source for source in sources}
        missing = [stem for stem in args.only if stem not in by_stem]
        if missing:
            parser.error(f"unknown --only instance(s): {','.join(missing)}")
        selected = [by_stem[stem] for stem in dict.fromkeys(args.only)]
    else:
        if args.count < 1 or args.count > len(sources):
            raise SystemExit(f"--count must be between 1 and {len(sources)}")
        selected = random.Random(args.seed).sample(sources, args.count)
    if args.status:
        args.status.parent.mkdir(parents=True, exist_ok=True)
        args.status.write_text(f"RUNNING 0/{len(selected)}\n")

    if args.report and not args.native_inspect:
        parser.error("--report requires --native-inspect")

    report_handle = None
    report_writer = None
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report_handle = args.report.open("w", newline="")
        report_writer = csv.DictWriter(
            report_handle,
            fieldnames=[
                "instance",
                "syfco_pair_match",
                "formula_ast_match",
                "formula_bytes_match",
                "inputs_match",
                "outputs_match",
                "native_formula_key_sha256",
                "syfco_formula_key_sha256",
                "native_formula_bytes_sha256",
                "syfco_formula_bytes_sha256",
                "error",
            ],
            dialect="excel-tab",
        )
        report_writer.writeheader()
        report_handle.flush()

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="acacia-syfco-check-") as raw_tmp:
        tmp = pathlib.Path(raw_tmp)
        for index, source in enumerate(sorted(selected), start=1):
            expected_part = tmp / f"{source.stem}.part"
            try:
                result = subprocess.run(
                    [
                        args.syfco,
                        "--format",
                        "ltlxba",
                        "--mode",
                        "fully",
                        "--part-file",
                        str(expected_part),
                        str(source),
                    ],
                    check=True,
                    capture_output=True,
                    text=True,
                    timeout=args.stage_timeout,
                )
            except (subprocess.SubprocessError, OSError) as error:
                failures.append(f"SyFCo error {source.name}: {error}")
                if report_writer and report_handle:
                    report_writer.writerow(
                        {
                            "instance": source.name,
                            "syfco_pair_match": 0,
                            "formula_ast_match": 0,
                            "formula_bytes_match": 0,
                            "inputs_match": 0,
                            "outputs_match": 0,
                            "native_formula_key_sha256": "",
                            "syfco_formula_key_sha256": "",
                            "native_formula_bytes_sha256": "",
                            "syfco_formula_bytes_sha256": "",
                            "error": f"syfco: {error}",
                        }
                    )
                    report_handle.flush()
                if args.status:
                    args.status.write_text(f"RUNNING {index}/{len(selected)}\n")
                continue
            expected_ltl = result.stdout.rstrip() + "\n"
            actual_ltl = (args.converted / f"{source.stem}.ltl").read_text()
            actual_part = (args.converted / f"{source.stem}.part").read_text()
            expected_part_text = expected_part.read_text()
            syfco_pair_match = (
                actual_ltl == expected_ltl and actual_part == expected_part_text
            )
            if not syfco_pair_match:
                failures.append(f"SyFCo pair changed: {source.name}")

            if args.native_inspect:
                try:
                    native_formula, native_inputs, native_outputs = inspect_native(
                        args.native_inspect, source, args.stage_timeout
                    )
                    syfco_inputs, syfco_outputs = read_part(expected_part)
                    native_normal, syfco_normal = formula_keys(
                        args.ltlfilt,
                        args.canonicalizer,
                        native_formula,
                        expected_ltl,
                        args.stage_timeout,
                    )
                except (subprocess.SubprocessError, OSError, RuntimeError) as error:
                    failures.append(f"native comparison error {source.name}: {error}")
                    if report_writer and report_handle:
                        report_writer.writerow(
                            {
                                "instance": source.name,
                                "syfco_pair_match": int(syfco_pair_match),
                                "formula_ast_match": 0,
                                "formula_bytes_match": 0,
                                "inputs_match": 0,
                                "outputs_match": 0,
                                "native_formula_key_sha256": "",
                                "syfco_formula_key_sha256": "",
                                "native_formula_bytes_sha256": "",
                                "syfco_formula_bytes_sha256": "",
                                "error": f"native comparison: {error}",
                            }
                        )
                        report_handle.flush()
                    if args.status:
                        args.status.write_text(f"RUNNING {index}/{len(selected)}\n")
                    continue
                native_formula_bytes = (native_formula + "\n").encode()
                syfco_formula_bytes = expected_ltl.encode()
                row = {
                    "instance": source.name,
                    "syfco_pair_match": int(syfco_pair_match),
                    "formula_ast_match": int(native_normal == syfco_normal),
                    "formula_bytes_match": int(
                        native_formula_bytes == syfco_formula_bytes
                    ),
                    "inputs_match": int(native_inputs == syfco_inputs),
                    "outputs_match": int(native_outputs == syfco_outputs),
                    "native_formula_key_sha256": hashlib.sha256(native_normal).hexdigest(),
                    "syfco_formula_key_sha256": hashlib.sha256(syfco_normal).hexdigest(),
                    "native_formula_bytes_sha256": hashlib.sha256(
                        native_formula_bytes
                    ).hexdigest(),
                    "syfco_formula_bytes_sha256": hashlib.sha256(
                        syfco_formula_bytes
                    ).hexdigest(),
                    "error": "",
                }
                if report_writer and report_handle:
                    report_writer.writerow(row)
                    report_handle.flush()
                mismatched = [
                    key
                    for key in ("formula_ast_match", "inputs_match", "outputs_match")
                    if not row[key]
                ]
                if mismatched:
                    failures.append(f"native mismatch {source.name}: {','.join(mismatched)}")
                    if args.debug_dir:
                        args.debug_dir.mkdir(parents=True, exist_ok=True)
                        prefix = args.debug_dir / source.stem
                        prefix.with_suffix(".native.ltl").write_text(native_formula)
                        prefix.with_suffix(".syfco.ltl").write_text(expected_ltl)
                        prefix.with_suffix(".native.inputs").write_text(native_inputs + "\n")
                        prefix.with_suffix(".syfco.inputs").write_text(syfco_inputs + "\n")
                        prefix.with_suffix(".native.outputs").write_text(native_outputs + "\n")
                        prefix.with_suffix(".syfco.outputs").write_text(syfco_outputs + "\n")
                print(
                    f"{'OK' if not mismatched and syfco_pair_match else 'FAIL'} "
                    f"{source.name} native=formula-ast:{row['formula_ast_match']} "
                    f"formula-bytes:{row['formula_bytes_match']} "
                    f"inputs:{row['inputs_match']} outputs:{row['outputs_match']}"
                )
            else:
                print(f"{'OK' if syfco_pair_match else 'FAIL'} {source.name}")
            if args.status:
                args.status.write_text(f"RUNNING {index}/{len(selected)}\n")

    if report_handle:
        report_handle.close()

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        if args.status:
            args.status.write_text("COMPLETE FAIL\n")
        return 1

    suffix = " and native frontend formula/I/O compatibility" if args.native_inspect else ""
    print(f"verified {len(selected)} deterministic SyFCo conversions{suffix}")
    if args.status:
        args.status.write_text("COMPLETE PASS\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
