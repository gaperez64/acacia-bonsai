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

from benchlib import ROOT, read_part
from tlsf_pairs import convert_pair


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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("converted", type=pathlib.Path)
    parser.add_argument("--syfco", default="syfco")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--seed", type=int, default=20260804)
    parser.add_argument("--native-inspect", type=pathlib.Path)
    parser.add_argument("--ltlfilt", default="ltlfilt")
    parser.add_argument("--canonicalizer", type=pathlib.Path)
    parser.add_argument("--target", choices=("Mealy", "Moore"))
    parser.add_argument("--stage-timeout", type=float, default=120)
    parser.add_argument("--only", action="append", default=[], metavar="STEM")
    parser.add_argument("--debug-dir", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--status", type=pathlib.Path)
    args = parser.parse_args(argv)
    if args.stage_timeout <= 0:
        parser.error("--stage-timeout must be positive")
    if args.native_inspect and not args.canonicalizer:
        parser.error("--native-inspect requires --canonicalizer")
    # Acacia's inspector already adapted the source to Mealy. Ask SyFCo for
    # that same target; applying a signal delay to either formula again is wrong.
    target = args.target or ("Mealy" if args.native_inspect else None)
    if args.native_inspect and target != "Mealy":
        parser.error("native inspector's effective target is Mealy")
    sources = sorted(args.source.glob("*.tlsf"))
    if args.only:
        by_stem = {source.stem: source for source in sources}
        if any(stem not in by_stem for stem in args.only):
            parser.error("unknown --only instance")
        selected = [by_stem[stem] for stem in dict.fromkeys(args.only)]
    else:
        if args.count < 1 or not sources:
            parser.error("--count must be positive and source must contain TLSF files")
        selected = random.Random(args.seed).sample(sources, min(args.count, len(sources)))
    rows = []
    scratch = ROOT / "build_scratch" / "p0-gates"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="conversion-", dir=scratch) as raw_tmp:
        tmp = pathlib.Path(raw_tmp)
        for index, source in enumerate(sorted(selected), 1):
            row = {"instance": source.name, "outcome": "ERROR",
                   "comparison_target": target or "source target", "error": ""}
            actual_ltl = args.converted / f"{source.stem}.ltl"
            actual_part = args.converted / f"{source.stem}.part"
            expected_part = tmp / f"{source.stem}.part"
            try:
                if not actual_ltl.is_file() or not actual_part.is_file():
                    row.update(outcome="MISSING_CONVERSION", error="formula or partition absent")
                    continue
                expected_ltl = convert_pair(args.syfco, source, expected_part,
                                            args.stage_timeout, target, args.canonicalizer)
                row["syfco_pair_match"] = int(
                    actual_ltl.read_text() == expected_ltl
                    and actual_part.read_text() == expected_part.read_text())
                row["outcome"] = "PASS" if row["syfco_pair_match"] else "FAIL"
                if args.native_inspect:
                    native, inputs, outputs = inspect_native(
                        args.native_inspect, source, args.stage_timeout)
                    syfco_inputs, syfco_outputs = read_part(expected_part)
                    native_key, syfco_key = formula_keys(
                        args.ltlfilt, args.canonicalizer, native, expected_ltl, args.stage_timeout)
                    row.update(
                        formula_ast_match=int(native_key == syfco_key),
                        formula_bytes_match=int(native + "\n" == expected_ltl),
                        inputs_match=int(set(inputs.split(",")) == set(syfco_inputs.split(","))),
                        outputs_match=int(set(outputs.split(",")) == set(syfco_outputs.split(","))),
                        native_formula_key_sha256=hashlib.sha256(native_key).hexdigest(),
                        syfco_formula_key_sha256=hashlib.sha256(syfco_key).hexdigest(),
                        native_formula_bytes_sha256=hashlib.sha256(native.encode()).hexdigest(),
                        syfco_formula_bytes_sha256=hashlib.sha256(expected_ltl.encode()).hexdigest())
                    if not all(row[key] for key in
                               ("formula_ast_match", "inputs_match", "outputs_match")):
                        row["outcome"] = "FAIL"
                    if row["outcome"] == "FAIL" and args.debug_dir:
                        args.debug_dir.mkdir(parents=True, exist_ok=True)
                        (args.debug_dir / f"{source.stem}.native.ltl").write_text(native)
                        (args.debug_dir / f"{source.stem}.syfco.ltl").write_text(expected_ltl)
            except subprocess.TimeoutExpired as error:
                row.update(outcome="UNKNOWN", error=f"stage timeout: {error.cmd}")
            except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
                row.update(outcome="ERROR", error=str(error))
            finally:
                rows.append(row)
                print(f"{row['outcome']} {source.name} {row['error']}")
                if args.status:
                    args.status.parent.mkdir(parents=True, exist_ok=True)
                    args.status.write_text(f"RUNNING {index}/{len(selected)}\n")
    if args.report:
        fields = ["instance", "outcome", "comparison_target", "syfco_pair_match",
                  "formula_ast_match", "formula_bytes_match", "inputs_match", "outputs_match",
                  "native_formula_key_sha256", "syfco_formula_key_sha256",
                  "native_formula_bytes_sha256", "syfco_formula_bytes_sha256", "error"]
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fields, dialect="excel-tab")
            writer.writeheader()
            writer.writerows(rows)
    passed = all(row["outcome"] == "PASS" for row in rows)
    if args.status:
        args.status.write_text(f"COMPLETE {'PASS' if passed else 'FAIL'}\n")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
