#!/usr/bin/env python3
"""Adjudicate Acacia/TLSF status conflicts with an independent solver.

A conflict between Acacia and a TLSF ``//STATUS`` annotation cannot be resolved
by trusting either side of the conflict.  An independent solver is required to
distinguish a bad corpus annotation from a bad Acacia verdict.  If ltlsynt
agrees with Acacia, the annotation may be corrected through the evidence-bearing
exceptions table.  If ltlsynt agrees with the annotation, Acacia has produced a
wrong decisive verdict: that is a solver correctness failure, not a corpus
problem, and it must halt the sprint.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import pathlib
import re
import subprocess
import sys
import time
from dataclasses import dataclass


DECISIVE_VERDICTS = frozenset(("REALIZABLE", "UNREALIZABLE"))
CONFLICT_COLUMNS = [
    "solver_label",
    "instance",
    "tlsf_file",
    "cap_s",
    "expected",
    "expectation_source",
    "actual",
    "seconds",
]
OUTPUT_COLUMNS = [
    "tlsf_file",
    "instance",
    "annotated_status",
    "acacia_verdict",
    "ltlsynt_verdict",
    "classification",
    "ltlsynt_seconds",
    "evidence",
]
EXCEPTION_COLUMNS = [
    "instance",
    "annotated_status",
    "corrected_status",
    "evidence",
]
STATUS_RE = re.compile(
    r"^\s*//\s*STATUS\s*:\s*(?P<status>[A-Za-z]+)\s*$", re.IGNORECASE
)
VERDICT_RE = re.compile(r"\b(?:UNREALIZABLE|REALIZABLE)\b", re.IGNORECASE)
SCRIPT_DIR = pathlib.Path(__file__).parent


class AdjudicationError(Exception):
    """An actionable input or output error."""


@dataclass(frozen=True)
class Conflict:
    tlsf_file: str
    instance: str
    acacia_verdict: str


@dataclass(frozen=True)
class SolverResult:
    verdict: str
    seconds: float
    detail: str


def positive_seconds(value: str) -> float:
    try:
        seconds = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a positive number") from None
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("must be a positive number")
    return seconds


def one_line(value: str) -> str:
    """Collapse subprocess text so it remains safe in a one-row TSV field."""
    return " ".join(value.split())


def read_conflict_file(path: pathlib.Path) -> list[dict[str, str]]:
    try:
        stream = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise AdjudicationError(f"cannot read conflicts file {path}: {error}") from error

    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != CONFLICT_COLUMNS:
            raise AdjudicationError(
                f"conflicts file {path} has an unexpected header; expected "
                + "\t".join(CONFLICT_COLUMNS)
            )
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise AdjudicationError(
                    f"conflicts file {path}:{line_number} is malformed"
                )
            actual = row["actual"].strip().upper()
            expected = row["expected"].strip().upper()
            if (
                actual not in DECISIVE_VERDICTS
                or expected not in DECISIVE_VERDICTS
                or actual == expected
            ):
                raise AdjudicationError(
                    f"conflicts file {path}:{line_number} is not a decisive "
                    "verdict conflict"
                )
            if row["expectation_source"].strip() != "status":
                raise AdjudicationError(
                    f"conflicts file {path}:{line_number} is based on "
                    f"expectation_source={row['expectation_source']!r}, not the "
                    "TLSF //STATUS annotation"
                )
            try:
                int(row["cap_s"])
            except ValueError:
                raise AdjudicationError(
                    f"conflicts file {path}:{line_number} has invalid cap_s"
                ) from None

            normalized = dict(row)
            normalized["actual"] = actual
            normalized["expected"] = expected
            rows.append(normalized)
    return rows


def load_conflicts(paths: list[pathlib.Path]) -> list[Conflict]:
    """Load sidecars and deduplicate their rows by concrete TLSF filename."""
    by_tlsf: dict[str, Conflict] = {}
    for path in paths:
        for row in read_conflict_file(path):
            tlsf_file = row["tlsf_file"].strip()
            instance = row["instance"].strip()
            if (
                not tlsf_file
                or pathlib.PurePath(tlsf_file).name != tlsf_file
                or not tlsf_file.endswith(".tlsf")
            ):
                raise AdjudicationError(
                    f"conflicts file {path} has invalid flat TLSF filename "
                    f"{tlsf_file!r}"
                )
            if not instance:
                raise AdjudicationError(
                    f"conflicts file {path} has an empty instance for {tlsf_file}"
                )

            conflict = Conflict(tlsf_file, instance, row["actual"])
            previous = by_tlsf.get(tlsf_file)
            if previous is None:
                by_tlsf[tlsf_file] = conflict
            elif previous.acacia_verdict != conflict.acacia_verdict:
                raise AdjudicationError(
                    f"conflicting Acacia verdicts for {tlsf_file}: "
                    f"{previous.acacia_verdict} and {conflict.acacia_verdict}"
                )
    return list(by_tlsf.values())


def read_annotated_status(path: pathlib.Path) -> str:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise AdjudicationError(f"cannot read TLSF source {path}: {error}") from error

    for line in lines:
        match = STATUS_RE.match(line)
        if match is None:
            continue
        status = match.group("status").upper()
        if status not in DECISIVE_VERDICTS:
            raise AdjudicationError(
                f"TLSF source {path} has non-decisive //STATUS {status!r}"
            )
        return status
    raise AdjudicationError(f"TLSF source {path} has no //STATUS annotation")


def validate_conflicts(
    conflicts: list[Conflict], corpus: pathlib.Path
) -> list[tuple[Conflict, pathlib.Path, str]]:
    if not corpus.is_dir():
        raise AdjudicationError(f"TLSF corpus is not a directory: {corpus}")

    validated: list[tuple[Conflict, pathlib.Path, str]] = []
    for conflict in conflicts:
        tlsf_path = corpus / conflict.tlsf_file
        if not tlsf_path.is_file():
            raise AdjudicationError(f"TLSF source does not exist: {tlsf_path}")
        annotated = read_annotated_status(tlsf_path)
        if annotated == conflict.acacia_verdict:
            raise AdjudicationError(
                f"{conflict.tlsf_file} is not a live status conflict: both the "
                f"annotation and Acacia say {annotated}"
            )
        validated.append((conflict, tlsf_path, annotated))
    return validated


def parse_verdict(stdout: str, stderr: str) -> str | None:
    output = f"{stdout}\n{stderr}"
    verdicts = {match.group(0).upper() for match in VERDICT_RE.finditer(output)}
    if len(verdicts) == 1:
        return verdicts.pop()
    return None


def run_ltlsynt(executable: str, tlsf_path: pathlib.Path, timeout: float) -> SolverResult:
    command = [executable, "--tlsf", str(tlsf_path), "--realizability"]
    started = time.monotonic()
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return SolverResult("other", time.monotonic() - started, "timed out")
    except OSError as error:
        detail = f"could not be executed: {one_line(str(error))}"
        return SolverResult("other", time.monotonic() - started, detail)

    seconds = time.monotonic() - started
    verdict = parse_verdict(result.stdout, result.stderr)
    if verdict is not None:
        return SolverResult(verdict, seconds, "")
    return SolverResult(
        "other", seconds, f"returned no decisive verdict (exit {result.returncode})"
    )


def classify(
    acacia_verdict: str, annotated_status: str, solver: SolverResult
) -> tuple[str, str]:
    annotated_lower = annotated_status.lower()
    if solver.verdict == acacia_verdict:
        return (
            "annotation_wrong",
            f"ltlsynt agreed with Acacia ({acacia_verdict}), so the TLSF "
            f"annotation ({annotated_lower}) is wrong.",
        )
    if solver.verdict == annotated_status:
        return (
            "acacia_wrong",
            f"ltlsynt agreed with the TLSF annotation ({annotated_lower}), not "
            f"Acacia ({acacia_verdict}).",
        )
    return (
        "inconclusive",
        f"ltlsynt {solver.detail}; no independent agreement with Acacia "
        f"({acacia_verdict}) or the TLSF annotation ({annotated_lower}) could "
        "be established.",
    )


def atomic_write_tsv(
    path: pathlib.Path, columns: list[str], rows: list[dict[str, str]]
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream, fieldnames=columns, delimiter="\t", lineterminator="\n"
            )
            writer.writeheader()
            writer.writerows(rows)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except OSError as error:
        raise AdjudicationError(f"cannot write TSV {path}: {error}") from error
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def ltlsynt_version(executable: str, timeout: float) -> str | None:
    try:
        result = subprocess.run(
            [executable, "--version"],
            capture_output=True,
            text=True,
            timeout=min(timeout, 10.0),
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    version = one_line(result.stdout or result.stderr)
    return version or None


def read_existing_exceptions(path: pathlib.Path) -> dict[str, dict[str, str]]:
    try:
        stream = path.open(encoding="utf-8", newline="")
    except FileNotFoundError:
        return {}
    except OSError as error:
        raise AdjudicationError(f"cannot read exceptions TSV {path}: {error}") from error

    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != EXCEPTION_COLUMNS:
            raise AdjudicationError(
                f"exceptions TSV {path} has an unexpected header; expected "
                + "\t".join(EXCEPTION_COLUMNS)
            )
        rows: dict[str, dict[str, str]] = {}
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise AdjudicationError(
                    f"exceptions TSV {path}:{line_number} is malformed"
                )
            instance = row["instance"].strip()
            if not instance or instance in rows:
                raise AdjudicationError(
                    f"exceptions TSV {path}:{line_number} has an empty or duplicate "
                    "instance"
                )
            rows[instance] = dict(row)
    return rows


def append_exceptions(
    path: pathlib.Path,
    adjudication_rows: list[dict[str, str]],
    executable: str,
    timeout: float,
) -> int:
    existing = read_existing_exceptions(path)
    version = ltlsynt_version(executable, timeout)
    solver_name = version if version is not None else "ltlsynt (version unavailable)"
    additions: list[dict[str, str]] = []
    for row in adjudication_rows:
        instance = row["tlsf_file"]
        annotated = row["annotated_status"]
        corrected = row["acacia_verdict"].lower()
        previous = existing.get(instance)
        if previous is not None:
            if (
                previous["annotated_status"].strip().lower() != annotated
                or previous["corrected_status"].strip().lower() != corrected
            ):
                raise AdjudicationError(
                    f"existing exception for {instance} disagrees with this "
                    "adjudication"
                )
            continue
        additions.append(
            {
                "instance": instance,
                "annotated_status": annotated,
                "corrected_status": corrected,
                "evidence": (
                    f"{solver_name} independently returned "
                    f"{row['acacia_verdict']}, agreeing with Acacia and "
                    f"contradicting the TLSF //STATUS annotation "
                    f"{annotated.upper()}."
                ),
            }
        )

    if not additions:
        return 0
    path.parent.mkdir(parents=True, exist_ok=True)
    needs_header = not path.exists()
    try:
        with path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=EXCEPTION_COLUMNS,
                delimiter="\t",
                lineterminator="\n",
            )
            if needs_header:
                writer.writeheader()
            writer.writerows(additions)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as error:
        raise AdjudicationError(f"cannot append exceptions TSV {path}: {error}") from error
    return len(additions)


def print_correctness_banner(rows: list[dict[str, str]]) -> None:
    border = "!" * 78
    print(border, file=sys.stderr)
    print("SOLVER CORRECTNESS FAILURE — HALT THE SPRINT", file=sys.stderr)
    print(
        "ltlsynt agrees with the TLSF annotation and contradicts Acacia on:",
        file=sys.stderr,
    )
    for row in rows:
        print(
            f"  - {row['instance']} ({row['tlsf_file']}): annotation="
            f"{row['annotated_status']}, Acacia={row['acacia_verdict']}, "
            f"ltlsynt={row['ltlsynt_verdict']}",
            file=sys.stderr,
        )
    print(
        "This is a solver correctness failure, not a corpus problem. Do not add "
        "a status exception.",
        file=sys.stderr,
    )
    print(border, file=sys.stderr)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        "--conflicts",
        action="append",
        required=True,
        type=pathlib.Path,
        metavar="PATH",
        help="coverage *-conflicts.tsv sidecar (repeatable)",
    )
    parser.add_argument(
        "--tlsf-corpus", required=True, type=pathlib.Path, metavar="DIR"
    )
    parser.add_argument(
        "--ltlsynt",
        default="ltlsynt",
        metavar="PATH",
        help="independent ltlsynt executable",
    )
    parser.add_argument(
        "--timeout",
        default=120,
        type=positive_seconds,
        metavar="SECONDS",
        help="per-instance ltlsynt timeout",
    )
    parser.add_argument(
        "--output",
        default=SCRIPT_DIR / "syntcomp26-status-adjudication.tsv",
        type=pathlib.Path,
        metavar="PATH",
        help="adjudication report",
    )
    parser.add_argument(
        "--exceptions-out",
        default=SCRIPT_DIR / "syntcomp26-status-exceptions.tsv",
        type=pathlib.Path,
        metavar="PATH",
        help="evidence-bearing status exceptions TSV",
    )
    parser.add_argument(
        "--append-exceptions",
        action="store_true",
        help="append exceptions only if every conflict is annotation_wrong",
    )
    return parser


def run(args: argparse.Namespace) -> int:
    conflicts = load_conflicts(args.conflicts)
    validated = validate_conflicts(conflicts, args.tlsf_corpus)
    rows: list[dict[str, str]] = []
    for conflict, tlsf_path, annotated in validated:
        solver = run_ltlsynt(args.ltlsynt, tlsf_path, args.timeout)
        classification, evidence = classify(
            conflict.acacia_verdict, annotated, solver
        )
        rows.append(
            {
                "tlsf_file": conflict.tlsf_file,
                "instance": conflict.instance,
                "annotated_status": annotated.lower(),
                "acacia_verdict": conflict.acacia_verdict,
                "ltlsynt_verdict": solver.verdict,
                "classification": classification,
                "ltlsynt_seconds": f"{solver.seconds:.6f}",
                "evidence": evidence,
            }
        )

    atomic_write_tsv(args.output, OUTPUT_COLUMNS, rows)
    print(f"wrote {args.output} ({len(rows)} distinct TLSF conflict(s))")

    acacia_wrong = [row for row in rows if row["classification"] == "acacia_wrong"]
    if acacia_wrong:
        print_correctness_banner(acacia_wrong)
        return 1

    inconclusive = [row for row in rows if row["classification"] == "inconclusive"]
    if inconclusive:
        print(
            f"INCONCLUSIVE: {len(inconclusive)} conflict(s) lacked an independent "
            "decisive verdict; no exceptions were appended.",
            file=sys.stderr,
        )
        return 2

    if args.append_exceptions:
        added = append_exceptions(
            args.exceptions_out, rows, args.ltlsynt, args.timeout
        )
        print(f"appended {added} new exception row(s) to {args.exceptions_out}")
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return run(args)
    except AdjudicationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
