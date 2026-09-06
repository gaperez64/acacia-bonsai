#!/usr/bin/env python3
"""Run resumable staged-cap coverage over the SYNTCOMP 2026 TLSF corpus.

Short caps cheaply discharge easy instances so that later, more expensive caps
are spent only on the remaining coverage gap.  With the default ``stop``
conflict policy, a verdict conflict is fatal as soon as it is durably recorded.
The ``collect`` policy instead records every conflict in a resumable sidecar,
finishes the campaign, and exits nonzero; it defers the conflict check for later
adjudication, never waives it, so collected results must not be used beforehand.
Known bad annotations are handled by a per-instance, evidence-bearing exceptions
table; exceptions correct specific expectations rather than providing a way to
switch off conflict checking.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import os
import pathlib
import re
import shlex
import subprocess
import sys

from benchlib import RunResult, classify_run, run_systemd_scope


ROOT = pathlib.Path(__file__).resolve().parents[1]
DECISIVE_RESULTS = {"REALIZABLE", "UNREALIZABLE"}
NORMALIZED_RESULTS = {
    "REALIZABLE",
    "UNREALIZABLE",
    "UNKNOWN",
    "TIMEOUT",
    "MEMOUT",
    "CRASH",
    "ERROR",
}
OUTPUT_COLUMNS = [
    "solver_label",
    "instance",
    "tlsf_file",
    "cap_s",
    "result",
    "seconds",
    "exit_code",
    "timed_out",
    "resource_reason",
    "expectation_source",
    "stdout_bytes",
    "stderr_bytes",
    "run_index",
    "acacia_sha",
    "binary_sha256",
    "preset",
    "timestamp_utc",
]
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
SUMMARY_COLUMNS = [
    "solver_label",
    "instance",
    "smallest_cap_solved",
    "decisive_result",
    "decisive_seconds",
    "still_unsolved_at_60",
    "failure_kind_at_60",
]
STATUS_RE = re.compile(
    r"^\s*//\s*STATUS\s*:\s*(?P<status>[A-Za-z]+)\s*$", re.IGNORECASE
)


class CoverageError(Exception):
    """An actionable input or resume-file error."""


def parse_caps(value: str) -> list[int]:
    """Parse, validate, and numerically order the staged time caps."""
    pieces = value.split(",")
    if not pieces or any(not piece.strip() for piece in pieces):
        raise argparse.ArgumentTypeError("caps must be a comma-separated list")
    try:
        caps = [int(piece.strip()) for piece in pieces]
    except ValueError:
        raise argparse.ArgumentTypeError("caps must be positive integers") from None
    if any(cap <= 0 for cap in caps):
        raise argparse.ArgumentTypeError("caps must be positive integers")
    return sorted(set(caps))


def nonnegative_int(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a nonnegative integer") from None
    if number < 0:
        raise argparse.ArgumentTypeError("must be a nonnegative integer")
    return number


def read_instance_list(path: pathlib.Path) -> list[str]:
    """Read logical instance names, ignoring blanks and comments."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CoverageError(f"cannot read instance list {path}: {error}") from error
    return [line for raw in lines if (line := raw.strip()) and not line.startswith("#")]


def select_instances(
    instances: list[str], start_after: str | None, limit: int | None
) -> list[str]:
    if start_after is not None:
        try:
            start = instances.index(start_after) + 1
        except ValueError:
            raise CoverageError(
                f"--start-after instance not found in list: {start_after}"
            ) from None
        instances = instances[start:]
    if limit is not None:
        instances = instances[:limit]
    return instances


def read_tlsf_map(path: pathlib.Path) -> dict[str, str]:
    """Load the required logical-instance to flat-TLSF map."""
    try:
        stream = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise CoverageError(f"cannot read TLSF map {path}: {error}") from error
    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        fields = set(reader.fieldnames or [])
        missing = {"instance", "tlsf"} - fields
        if missing:
            names = ", ".join(sorted(missing))
            raise CoverageError(f"TLSF map {path} is missing column(s): {names}")
        mapping: dict[str, str] = {}
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise CoverageError(f"TLSF map {path}:{line_number} is malformed")
            instance = row["instance"].strip()
            tlsf = row["tlsf"].strip()
            if not instance or not tlsf:
                raise CoverageError(
                    f"TLSF map {path}:{line_number} has an empty instance or tlsf"
                )
            if instance in mapping:
                raise CoverageError(
                    f"TLSF map {path}:{line_number} repeats instance {instance}"
                )
            mapping[instance] = tlsf
    return mapping


def resolve_targets(
    instances: list[str], mapping: dict[str, str], corpus: pathlib.Path
) -> dict[str, tuple[str, pathlib.Path]]:
    if not corpus.is_dir():
        raise CoverageError(f"TLSF corpus is not a directory: {corpus}")
    targets: dict[str, tuple[str, pathlib.Path]] = {}
    for instance in instances:
        if instance not in mapping:
            raise CoverageError(f"instance missing from TLSF map: {instance}")
        tlsf_file = mapping[instance]
        if pathlib.PurePath(tlsf_file).name != tlsf_file:
            raise CoverageError(
                f"TLSF map entry for {instance} is not a flat filename: {tlsf_file}"
            )
        tlsf_path = corpus / tlsf_file
        if not tlsf_path.is_file():
            raise CoverageError(
                f"TLSF file for instance {instance} does not exist: {tlsf_path}"
            )
        targets[instance] = tlsf_file, tlsf_path
    return targets


def expected_verdict(path: pathlib.Path) -> str | None:
    """Return the exact decisive expectation carried by a TLSF STATUS line."""
    status = tlsf_status(path)
    if status == "realizable":
        return "REALIZABLE"
    if status == "unrealizable":
        return "UNREALIZABLE"
    if status in {"unknown", "uknown", "unknon"}:
        # The two misspellings are real corpus data, not defensive programming.
        return None
    return None


def tlsf_status(path: pathlib.Path) -> str | None:
    """Return the normalized text of the first TLSF STATUS annotation."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CoverageError(f"cannot read TLSF source {path}: {error}") from error
    for line in lines:
        match = STATUS_RE.match(line)
        if match is None:
            continue
        return match.group("status").lower()
    return None


def read_status_exceptions(
    path: pathlib.Path, corpus: pathlib.Path
) -> dict[str, str | None]:
    """Load status corrections and verify them against the current corpus."""
    try:
        stream = path.open(encoding="utf-8", newline="")
    except FileNotFoundError:
        return {}
    except OSError as error:
        raise CoverageError(
            f"cannot read status exceptions {path}: {error}"
        ) from error

    columns = ["instance", "annotated_status", "corrected_status", "evidence"]
    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != columns:
            raise CoverageError(
                f"status exceptions {path} has an unexpected header; expected "
                + "\t".join(columns)
            )
        exceptions: dict[str, str | None] = {}
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise CoverageError(
                    f"status exceptions {path}:{line_number} is malformed"
                )
            instance = row["instance"].strip()
            annotated = row["annotated_status"].strip().lower()
            corrected = row["corrected_status"].strip().lower()
            evidence = row["evidence"].strip()
            if (
                not instance
                or pathlib.PurePath(instance).name != instance
                or not instance.endswith(".tlsf")
            ):
                raise CoverageError(
                    f"status exceptions {path}:{line_number} has an invalid flat "
                    f"TLSF filename: {instance!r}"
                )
            if instance in exceptions:
                raise CoverageError(
                    f"status exceptions {path}:{line_number} repeats {instance}"
                )
            if not annotated:
                raise CoverageError(
                    f"status exceptions {path}:{line_number} has empty annotated_status"
                )
            if not evidence:
                raise CoverageError(
                    f"status exceptions {path}:{line_number} has empty evidence"
                )
            if corrected not in {"", "none", "realizable", "unrealizable"}:
                raise CoverageError(
                    f"status exceptions {path}:{line_number} has invalid "
                    f"corrected_status {row['corrected_status']!r}"
                )

            tlsf_path = corpus / instance
            actual = tlsf_status(tlsf_path)
            if annotated != actual:
                actual_display = actual if actual is not None else "<none>"
                raise CoverageError(
                    f"status exception for {instance} says annotated_status="
                    f"{annotated!r}, but the TLSF //STATUS is {actual_display!r}; "
                    "the corpus changed and this exception must be re-justified"
                )
            exceptions[instance] = (
                corrected.upper() if corrected not in {"", "none"} else None
            )
    return exceptions


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise CoverageError(f"cannot hash binary {path}: {error}") from error
    return digest.hexdigest()


def git_head() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def timestamp_utc() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat().replace(
        "+00:00", "Z"
    )


def atomic_write_tsv(
    path: pathlib.Path, columns: list[str], rows: list[dict[str, str]]
) -> None:
    """Atomically replace a complete TSV file."""
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
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def load_output(path: pathlib.Path) -> list[dict[str, str]]:
    try:
        stream = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise CoverageError(f"cannot read resume output {path}: {error}") from error
    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != OUTPUT_COLUMNS:
            raise CoverageError(
                f"resume output {path} has an unexpected header; expected "
                + "\t".join(OUTPUT_COLUMNS)
            )
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise CoverageError(f"resume output {path}:{line_number} is malformed")
            try:
                int(row["cap_s"])
            except ValueError:
                raise CoverageError(
                    f"resume output {path}:{line_number} has invalid cap_s"
                ) from None
            if row["result"] not in NORMALIZED_RESULTS:
                raise CoverageError(
                    f"resume output {path}:{line_number} has invalid result "
                    f"{row['result']!r}"
                )
            if row["expectation_source"] not in {"status", "exception", "none"}:
                raise CoverageError(
                    f"resume output {path}:{line_number} has invalid "
                    f"expectation_source {row['expectation_source']!r}"
                )
            rows.append(dict(row))
    return rows


def load_conflicts(path: pathlib.Path) -> list[dict[str, str]]:
    try:
        stream = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise CoverageError(f"cannot read resume conflicts {path}: {error}") from error
    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != CONFLICT_COLUMNS:
            raise CoverageError(
                f"resume conflicts {path} has an unexpected header; expected "
                + "\t".join(CONFLICT_COLUMNS)
            )
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise CoverageError(
                    f"resume conflicts {path}:{line_number} is malformed"
                )
            try:
                int(row["cap_s"])
            except ValueError:
                raise CoverageError(
                    f"resume conflicts {path}:{line_number} has invalid cap_s"
                ) from None
            if (
                row["expected"] not in DECISIVE_RESULTS
                or row["actual"] not in DECISIVE_RESULTS
                or row["expected"] == row["actual"]
            ):
                raise CoverageError(
                    f"resume conflicts {path}:{line_number} is not a verdict conflict"
                )
            if row["expectation_source"] not in {"status", "exception"}:
                raise CoverageError(
                    f"resume conflicts {path}:{line_number} has invalid "
                    f"expectation_source {row['expectation_source']!r}"
                )
            rows.append(dict(row))
    return rows


def append_tsv_row(
    path: pathlib.Path, columns: list[str], row: dict[str, str]
) -> None:
    """Append and durably record one row in an initialized TSV file."""
    with path.open("a", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=columns, delimiter="\t", lineterminator="\n"
        )
        writer.writerow(row)
        stream.flush()
        os.fsync(stream.fileno())


def conflict_row(
    row: dict[str, str], expectations: dict[str, tuple[str | None, str]]
) -> dict[str, str] | None:
    expectation_entry = expectations.get(row["instance"])
    if expectation_entry is None:
        return None
    expected, expectation_source = expectation_entry
    if (
        expected is None
        or row["result"] not in DECISIVE_RESULTS
        or row["result"] == expected
    ):
        return None
    return {
        "solver_label": row["solver_label"],
        "instance": row["instance"],
        "tlsf_file": row["tlsf_file"],
        "cap_s": row["cap_s"],
        "expected": expected,
        "expectation_source": expectation_source,
        "actual": row["result"],
        "seconds": row["seconds"],
    }


def normalize_result(run: RunResult) -> tuple[str, str]:
    """Map benchlib classifications to the coverage file's exact vocabulary."""
    result = classify_run(run, tool="acacia")
    if result == "TIMEOUT":
        return "TIMEOUT", "timeout"
    if run.returncode < 0:
        return "CRASH", f"signal:{-run.returncode}"
    if result == "RESOURCE_LIMIT":
        return "MEMOUT", "memory"
    return result, ""


def unique_in_order(instances: list[str]) -> list[str]:
    return list(dict.fromkeys(instances))


def write_summary(
    output: pathlib.Path,
    solver_label: str,
    instances: list[str],
    rows: list[dict[str, str]],
    largest_cap: int,
) -> pathlib.Path:
    relevant: dict[str, list[dict[str, str]]] = {
        instance: [] for instance in unique_in_order(instances)
    }
    for row in rows:
        if row["solver_label"] == solver_label and row["instance"] in relevant:
            relevant[row["instance"]].append(row)

    summary_rows: list[dict[str, str]] = []
    for instance in unique_in_order(instances):
        instance_rows = relevant[instance]
        decisive = [
            row for row in instance_rows if row["result"] in DECISIVE_RESULTS
        ]
        earliest = min(decisive, key=lambda row: int(row["cap_s"])) if decisive else None
        at_largest = [
            row for row in instance_rows if int(row["cap_s"]) == largest_cap
        ]
        largest_row = at_largest[-1] if at_largest else None
        solved_by_largest = any(
            row["result"] in DECISIVE_RESULTS
            and int(row["cap_s"]) <= largest_cap
            for row in instance_rows
        )
        summary_rows.append(
            {
                "solver_label": solver_label,
                "instance": instance,
                "smallest_cap_solved": earliest["cap_s"] if earliest else "",
                "decisive_result": earliest["result"] if earliest else "",
                "decisive_seconds": earliest["seconds"] if earliest else "",
                "still_unsolved_at_60": str(not solved_by_largest).lower(),
                "failure_kind_at_60": largest_row["result"] if largest_row else "",
            }
        )

    summary_path = output.with_name(f"{output.stem}-summary.tsv")
    atomic_write_tsv(summary_path, SUMMARY_COLUMNS, summary_rows)
    return summary_path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True, metavar="PATH")
    parser.add_argument("--solver-label", required=True, metavar="LABEL")
    parser.add_argument("--list", required=True, metavar="PATH")
    parser.add_argument("--tlsf-map", required=True, metavar="PATH")
    parser.add_argument("--tlsf-corpus", required=True, metavar="DIR")
    parser.add_argument(
        "--status-exceptions",
        type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("syntcomp26-status-exceptions.tsv"),
        metavar="PATH",
        help="evidence-bearing TLSF status corrections (missing means none)",
    )
    parser.add_argument(
        "--caps", required=True, type=parse_caps, metavar="1,5,17,60"
    )
    parser.add_argument("--memory-max", required=True, metavar="8G")
    parser.add_argument("--memory-swap-max", required=True, metavar="0")
    parser.add_argument("--output", required=True, metavar="TSV")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument(
        "--conflict-policy",
        choices=("stop", "collect"),
        default="stop",
        help="stop at the first verdict conflict (default), or collect all conflicts",
    )
    parser.add_argument("--limit", type=nonnegative_int, metavar="N")
    parser.add_argument("--start-after", metavar="INSTANCE")
    parser.add_argument("--flags", default="", help="extra flags parsed with shlex.split")
    parser.add_argument(
        "--acacia-sha",
        metavar="S",
        help="revision recorded in output (default: git rev-parse HEAD)",
    )
    parser.add_argument("--preset", default="", metavar="S")
    return parser


def run(args: argparse.Namespace) -> int:
    binary = pathlib.Path(args.bin).resolve()
    if not binary.is_file():
        raise CoverageError(f"binary does not exist: {binary}")
    if not os.access(binary, os.X_OK):
        raise CoverageError(f"binary is not executable: {binary}")

    instances = select_instances(
        read_instance_list(pathlib.Path(args.list)), args.start_after, args.limit
    )
    tlsf_map = read_tlsf_map(pathlib.Path(args.tlsf_map))
    corpus = pathlib.Path(args.tlsf_corpus)
    targets = resolve_targets(instances, tlsf_map, corpus)
    exceptions = read_status_exceptions(args.status_exceptions, corpus)
    expectations: dict[str, tuple[str | None, str]] = {}
    for instance, (tlsf_file, tlsf_path) in targets.items():
        if tlsf_file in exceptions:
            expectation = exceptions[tlsf_file]
            source = "exception" if expectation is not None else "none"
        else:
            expectation = expected_verdict(tlsf_path)
            source = "status" if expectation is not None else "none"
        expectations[instance] = expectation, source
    try:
        flags = shlex.split(args.flags)
    except ValueError as error:
        raise CoverageError(f"invalid --flags value: {error}") from error

    binary_sha256 = sha256_file(binary)
    acacia_sha = args.acacia_sha if args.acacia_sha is not None else git_head()
    output = pathlib.Path(args.output)
    if args.resume and output.exists():
        rows = load_output(output)
    else:
        rows = []
        atomic_write_tsv(output, OUTPUT_COLUMNS, rows)

    conflicts_path = output.with_name(f"{output.stem}-conflicts.tsv")
    conflict_keys: set[tuple[str, str, int]] = set()
    collected_conflict_keys: set[tuple[str, str, int]] = set()
    if args.conflict_policy == "collect":
        if args.resume and conflicts_path.exists():
            conflict_rows = load_conflicts(conflicts_path)
        else:
            conflict_rows = []
            atomic_write_tsv(conflicts_path, CONFLICT_COLUMNS, conflict_rows)
        conflict_keys = {
            (row["solver_label"], row["instance"], int(row["cap_s"]))
            for row in conflict_rows
        }

    for row in rows:
        if row["solver_label"] != args.solver_label:
            continue
        conflict = conflict_row(row, expectations)
        if conflict is not None and args.conflict_policy == "stop":
            print(
                "verdict conflict: "
                f"instance={row['instance']} expected={conflict['expected']} "
                f"actual={row['result']}",
                file=sys.stderr,
            )
            return 1
        if conflict is not None:
            key = (row["solver_label"], row["instance"], int(row["cap_s"]))
            collected_conflict_keys.add(key)
            if key not in conflict_keys:
                append_tsv_row(conflicts_path, CONFLICT_COLUMNS, conflict)
                conflict_keys.add(key)

    completed_keys = {
        (row["solver_label"], row["instance"], int(row["cap_s"])) for row in rows
    }
    decisive_caps: dict[tuple[str, str], set[int]] = {}
    for row in rows:
        if row["result"] in DECISIVE_RESULTS:
            key = (row["solver_label"], row["instance"])
            decisive_caps.setdefault(key, set()).add(int(row["cap_s"]))

    run_index = 0
    with output.open("a", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=OUTPUT_COLUMNS, delimiter="\t", lineterminator="\n"
        )
        for cap in args.caps:
            selected = [
                instance
                for instance in instances
                if not any(
                    solved_cap < cap
                    for solved_cap in decisive_caps.get(
                        (args.solver_label, instance), set()
                    )
                )
            ]
            for instance in selected:
                completed_key = (args.solver_label, instance, cap)
                if completed_key in completed_keys:
                    continue
                tlsf_file, tlsf_path = targets[instance]
                cmd = [str(binary), *flags, "-T", str(tlsf_path)]
                solver_run = run_systemd_scope(
                    cmd,
                    timeout=cap,
                    memory_max=args.memory_max,
                    memory_swap_max=args.memory_swap_max,
                    unit_prefix="syntcomp26-coverage",
                )
                result, resource_reason = normalize_result(solver_run)
                row = {
                    "solver_label": args.solver_label,
                    "instance": instance,
                    "tlsf_file": tlsf_file,
                    "cap_s": str(cap),
                    "result": result,
                    "seconds": str(solver_run.seconds),
                    "exit_code": str(solver_run.returncode),
                    "timed_out": str(solver_run.timed_out).lower(),
                    "resource_reason": resource_reason,
                    "expectation_source": expectations[instance][1],
                    "stdout_bytes": str(solver_run.stdout_bytes),
                    "stderr_bytes": str(solver_run.stderr_bytes),
                    "run_index": str(run_index),
                    "acacia_sha": acacia_sha,
                    "binary_sha256": binary_sha256,
                    "preset": args.preset,
                    "timestamp_utc": timestamp_utc(),
                }
                writer.writerow(row)
                stream.flush()
                os.fsync(stream.fileno())
                rows.append(row)
                completed_keys.add(completed_key)
                if result in DECISIVE_RESULTS:
                    decisive_caps.setdefault(
                        (args.solver_label, instance), set()
                    ).add(cap)
                print(
                    f"cap={cap}s instance={instance} result={result} "
                    f"seconds={solver_run.seconds:.3f}"
                )
                run_index += 1

                conflict = conflict_row(row, expectations)
                if conflict is not None and args.conflict_policy == "stop":
                    print(
                        "verdict conflict: "
                        f"instance={instance} expected={conflict['expected']} "
                        f"actual={result}",
                        file=sys.stderr,
                    )
                    return 1
                if conflict is not None:
                    key = (args.solver_label, instance, cap)
                    collected_conflict_keys.add(key)
                    if key not in conflict_keys:
                        append_tsv_row(conflicts_path, CONFLICT_COLUMNS, conflict)
                        conflict_keys.add(key)

    summary_path = write_summary(
        output, args.solver_label, instances, rows, max(args.caps)
    )
    print(f"wrote {output}")
    print(f"wrote {summary_path}")
    if args.conflict_policy == "collect":
        conflict_count = len(collected_conflict_keys)
        # Only warn when there is something to adjudicate.  Printing the banner
        # on a clean campaign teaches the reader to skip it, which is the one
        # thing this message cannot afford.
        if conflict_count:
            print(
                f"CONFLICTS COLLECTED: {conflict_count} (see {conflicts_path}) "
                "-- results are NOT usable until adjudicated",
                file=sys.stderr,
            )
        if conflict_count > 0:
            return 3
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return run(args)
    except CoverageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
