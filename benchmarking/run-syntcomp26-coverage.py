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

The <output>-summary.tsv sidecar reports, per instance, the answer from the
*earliest* cap that decided it: decisive_result and decisive_seconds say what
was decided and how long it took, and smallest_cap_solved says under which cap
that happened.  A consumer that reads coverage at one particular cap must
therefore filter on smallest_cap_solved -- otherwise a campaign run with
--caps 1,5,17,60 contributes its 60-second answers to a 17-second total.
still_unsolved_at_max_cap, failure_kind_at_max_cap and max_cap_s describe the
largest cap of this campaign, whatever it was, and not any fixed cap.

Saved uniform-cap observations can be exported without running a solver:
  run-syntcomp26-coverage.py export-cactus --summary run-summary.tsv \
      --list tests/suites/benchmarks/syntcomp26/all.list --cap 17 --output run.csv
This reads run.tsv and its conflicts sidecar, and writes run.csv plus run.raw.tsv.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import math
import os
import pathlib
import re
import shlex
import subprocess
import sys

from benchlib import (
    CACTUS_NON_SOLVED_RESULTS,
    TOOL_EXIT_CODES,
    RunResult,
    campaign_scope_guard,
    classify_run,
    run_systemd_scope,
)


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
    "flags",
    "cpu_seconds",
    "max_process_rss_bytes",
    "scope_memory_peak_bytes",
    "memory_max",
    "memory_swap_max",
    "allowed_cpus",
    "cpu_quota",
    "collect_rusage",
    "worker_records_dir",
    "scope_unit",
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
    "still_unsolved_at_max_cap",
    "failure_kind_at_max_cap",
    "max_cap_s",
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
    path: pathlib.Path, corpus: pathlib.Path, required: bool = False
) -> dict[str, str | None]:
    """Load status corrections and verify them against the current corpus.

    `required` says the caller named this path rather than inheriting the
    default.  A named file that is not there is a misconfiguration, and a silent
    empty table is the worst way to report it: every catalogued wrong //STATUS
    then reads as a fresh verdict conflict, which under the default policy
    aborts the run at whichever instance it reaches first.  That looks exactly
    like a soundness bug in the solver and is not one.
    """
    try:
        stream = path.open(encoding="utf-8", newline="")
    except FileNotFoundError as error:
        if required:
            raise CoverageError(
                f"status exceptions {path} does not exist; without it every "
                f"adjudicated wrong //STATUS annotation is reported as a verdict "
                f"conflict"
            ) from error
        print(
            f"WARNING: no status exceptions at {path}; adjudicated wrong "
            f"//STATUS annotations will be reported as verdict conflicts",
            file=sys.stderr,
        )
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


def load_output(
    path: pathlib.Path, *, allow_missing_memory: bool = False,
) -> list[dict[str, str]]:
    try:
        stream = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise CoverageError(f"cannot read resume output {path}: {error}") from error
    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        legacy_columns = [column for column in OUTPUT_COLUMNS if column != "scope_unit"]
        optional = {"max_process_rss_bytes", "scope_memory_peak_bytes"} if allow_missing_memory else set()
        header = [column for column in (reader.fieldnames or []) if column not in optional]
        expected = [[column for column in columns if column not in optional]
                    for columns in (OUTPUT_COLUMNS, legacy_columns)]
        if header not in expected or len(set(reader.fieldnames or [])) != len(reader.fieldnames or []):
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
            row.setdefault("scope_unit", "")
            for column in optional:
                row.setdefault(column, "")
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
    if result == "RESOURCE_LIMIT":
        return "MEMOUT", "memory"
    if run.returncode < 0:
        return "CRASH", f"signal:{-run.returncode}"
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
                "still_unsolved_at_max_cap": str(not solved_by_largest).lower(),
                "failure_kind_at_max_cap": largest_row["result"] if largest_row else "",
                "max_cap_s": str(largest_cap),
            }
        )

    summary_path = output.with_name(f"{output.stem}-summary.tsv")
    atomic_write_tsv(summary_path, SUMMARY_COLUMNS, summary_rows)
    return summary_path


def load_uniform_observations(
    summary: pathlib.Path,
    runs: pathlib.Path,
    instance_list: pathlib.Path,
    expected_cap: float,
    *,
    allow_missing_memory: bool = False,
) -> dict[str, dict[str, str]]:
    """Read validated, complete, uniform-cap observations without running solvers.

    Summary fields select the outcome; raw runs supply actual exits and times.
    No solver output is parsed and no repetition or staged cap is selected.
    A nonempty collect-policy sidecar blocks export: it has no adjudication field.
    """
    if not math.isfinite(expected_cap) or expected_cap <= 0:
        raise CoverageError("expected cap must be finite and greater than zero")
    expected = read_instance_list(instance_list)
    if not expected or len(set(expected)) != len(expected):
        raise CoverageError(f"{instance_list}: empty list or duplicate instances")

    def index_rows(rows, path):
        indexed = {}
        for row in rows:
            instance = row["instance"]
            if not instance or instance in indexed:
                raise CoverageError(f"{path}: missing or duplicate instance {instance!r}")
            indexed[instance] = row
        missing, extra = set(expected) - indexed.keys(), indexed.keys() - set(expected)
        if missing or extra:
            raise CoverageError(
                f"{path}: instance sets differ; missing: {sorted(missing)}; extra: {sorted(extra)}"
            )
        return indexed

    def number(value, context):
        try:
            result = float(value)
        except ValueError:
            raise CoverageError(f"{context}: invalid number {value!r}") from None
        if not math.isfinite(result) or result < 0:
            raise CoverageError(f"{context}: must be finite and non-negative, got {value!r}")
        return result

    def check_cap(value, context):
        if number(value, context) != expected_cap:
            raise CoverageError(f"{context}: expected uniform cap {expected_cap:g}, got {value!r}")

    with summary.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != SUMMARY_COLUMNS:
            raise CoverageError(f"{summary}: unexpected summary header")
        summary_rows = list(reader)
    if any(None in row or None in row.values() for row in summary_rows):
        raise CoverageError(f"{summary}: malformed summary row")
    summaries = index_rows(summary_rows, summary)
    raw_rows = load_output(runs, allow_missing_memory=allow_missing_memory)
    # Inspect every recorded cap, including rows a staged summary would hide.
    for row in raw_rows:
        check_cap(row["cap_s"], f"{runs}: {row['instance']} cap_s")
    observations = index_rows(raw_rows, runs)
    conflicts = runs.with_name(f"{runs.stem}-conflicts.tsv")
    if conflicts.exists() and load_conflicts(conflicts):
        raise CoverageError(f"{conflicts}: unresolved verdict conflicts block reporting")
    for field in (
        "solver_label", "acacia_sha", "binary_sha256", "preset", "flags",
        "memory_max", "memory_swap_max", "allowed_cpus", "cpu_quota", "collect_rusage",
    ):
        if len({row[field] for row in raw_rows}) != 1:
            raise CoverageError(f"{runs}: mixed provenance field {field}")

    for instance, row in summaries.items():
        context = f"{summary}: {instance}"
        observed = observations[instance]
        if not row["solver_label"] or row["solver_label"] != observed["solver_label"]:
            raise CoverageError(f"{context}: solver_label disagrees with raw run")
        check_cap(row["max_cap_s"], f"{context} max_cap_s")
        seconds = number(observed["seconds"], f"{runs}: {instance} seconds")
        try:
            exit_code = int(observed["exit_code"])
        except ValueError:
            raise CoverageError(f"{runs}: {instance}: invalid exit_code") from None
        if row["still_unsolved_at_max_cap"] not in {"true", "false"}:
            raise CoverageError(f"{context}: invalid still_unsolved_at_max_cap")
        solved = row["still_unsolved_at_max_cap"] == "false"
        status = row["decisive_result"] if solved else row["failure_kind_at_max_cap"]
        result = {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}.get(status, status)
        if solved:
            if status not in DECISIVE_RESULTS:
                raise CoverageError(f"{context}: solved row needs a REALIZABLE/UNREALIZABLE verdict")
            if exit_code != TOOL_EXIT_CODES["acacia"][status]:
                raise CoverageError(f"{context}: solved verdict disagrees with exit_code {exit_code}")
            check_cap(row["smallest_cap_solved"], f"{context} smallest_cap_solved")
            if number(row["decisive_seconds"], f"{context} decisive_seconds") != seconds:
                raise CoverageError(f"{context}: decisive_seconds disagrees with raw run")
        else:
            if result not in CACTUS_NON_SOLVED_RESULTS:
                raise CoverageError(f"{context}: unsupported status mapping {status!r}")
            if any(row[field] for field in (
                "decisive_result", "decisive_seconds", "smallest_cap_solved",
            )):
                raise CoverageError(f"{context}: unsolved row contains decisive fields")
        if status != observed["result"] or row["failure_kind_at_max_cap"] != status:
            raise CoverageError(f"{context}: summary status disagrees with raw run")
    return {instance: observations[instance] for instance in summaries}


def export_cactus_csv(
    summary: pathlib.Path,
    runs: pathlib.Path,
    instance_list: pathlib.Path,
    expected_cap: float,
    output: pathlib.Path,
) -> pathlib.Path:
    """Validate one uniform-cap observation per ID, then export CSV + raw TSV."""
    observations = load_uniform_observations(summary, runs, instance_list, expected_cap)
    csv_rows, sidecar_rows = [], []
    for instance, observed in observations.items():
        status = observed["result"]
        result = {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}.get(status, status)
        seconds = float(observed["seconds"]) if status in DECISIVE_RESULTS else expected_cap
        csv_rows.append((instance, result, seconds, int(observed["exit_code"])))
        sidecar_rows.append({
            "instance": instance, "result": status, "exit_code": observed["exit_code"],
            "seconds": observed["seconds"], "cap_s": observed["cap_s"],
        })

    raw_output = output.with_suffix(".raw.tsv")
    conflicts = runs.with_name(f"{runs.stem}-conflicts.tsv")
    inputs = {path.resolve() for path in (summary, runs, instance_list, conflicts)}
    if output.resolve() == raw_output.resolve() or inputs & {
        output.resolve(), raw_output.resolve(),
    }:
        raise CoverageError("export paths must be distinct from each other and the inputs")
    # All checks precede either write, including when replacing existing exports.
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("instance", "result", "seconds", "exit"))
        writer.writerows(csv_rows)
    atomic_write_tsv(raw_output, ["instance", "result", "exit_code", "seconds", "cap_s"],
                     sidecar_rows)
    return raw_output


def export_cactus_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=export_cactus_csv.__doc__)
    parser.add_argument("--summary", required=True, type=pathlib.Path)
    parser.add_argument("--runs", type=pathlib.Path,
                        help="raw TSV (default: SUMMARY with -summary.tsv replaced by .tsv)")
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--cap", required=True, type=float)
    parser.add_argument("--output", required=True, type=pathlib.Path,
                        help="cactus CSV; also writes OUTPUT with suffix .raw.tsv")
    args = parser.parse_args(argv)
    if args.runs is None:
        if not args.summary.name.endswith("-summary.tsv"):
            parser.error("--runs is required unless --summary ends in -summary.tsv")
        args.runs = args.summary.with_name(args.summary.name.removesuffix("-summary.tsv") + ".tsv")
    try:
        sidecar = export_cactus_csv(args.summary, args.runs, args.list, args.cap, args.output)
    except (CoverageError, OSError) as error:
        parser.error(str(error))
    print(f"wrote {args.output}")
    print(f"wrote {sidecar}")
    return 0


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
        default=None,
        metavar="PATH",
        help="evidence-bearing TLSF status corrections; defaults to the table "
             "beside this script, and a path given here must exist",
    )
    parser.add_argument(
        "--caps", required=True, type=parse_caps, metavar="1,5,17,60"
    )
    parser.add_argument("--allowed-cpus", metavar="0-3",
                        help="pin the whole solver invocation to these CPUs (systemd "
                             "AllowedCPUs); refused unless cpuset is delegated to the user "
                             "manager, since systemd otherwise ignores it silently")
    parser.add_argument("--cpu-quota", metavar="200%",
                        help="cap total CPU for the whole solver invocation (systemd CPUQuota)")
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
    parser.add_argument("--collect-rusage", action="store_true",
                        help="wrap the solver with GNU time inside the existing scope")
    parser.add_argument("--worker-records-dir", type=pathlib.Path,
                        help="diagnostic run: capture transformed workers below this directory")
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
    # A named path must exist; the default may legitimately be absent, and a
    # copy of this script run from outside the repo will find it absent.
    named = args.status_exceptions is not None
    exceptions_path = (
        args.status_exceptions if named
        else pathlib.Path(__file__).with_name("syntcomp26-status-exceptions.tsv")
    )
    exceptions = read_status_exceptions(exceptions_path, corpus, required=named)
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
    records_root = getattr(args, "worker_records_dir", None)
    if records_root is not None:
        records_root = records_root.resolve()
    run_metadata = {
        "acacia_sha": acacia_sha, "binary_sha256": binary_sha256,
        "preset": args.preset, "flags": args.flags,
        "memory_max": args.memory_max, "memory_swap_max": args.memory_swap_max,
        # In the resume guard because a run continued under a different CPU
        # budget is a different measurement, exactly as for the memory cap.
        "allowed_cpus": args.allowed_cpus or "", "cpu_quota": args.cpu_quota or "",
        "collect_rusage": str(getattr(args, "collect_rusage", False)).lower(),
        "worker_records_dir": str(records_root) if records_root else "",
    }
    output = pathlib.Path(args.output)
    if args.resume and output.exists():
        rows = load_output(output)
        for row in rows:
            if row["solver_label"] == args.solver_label and any(
                row.get(key) != value for key, value in run_metadata.items()
            ):
                raise CoverageError("resume configuration or binary differs from recorded campaign")
        # Upgrade the older header only after validating the recorded treatment.
        # Empty scope IDs preserve the distinction from a measured identifier.
        atomic_write_tsv(output, OUTPUT_COLUMNS, rows)
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
                if getattr(args, "collect_rusage", False):
                    cmd = ["/usr/bin/time", "-q", "-f", "ACACIA_RUSAGE %U %S %M", *cmd]
                run_env = None
                if records_root is not None:
                    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "_", args.solver_label)
                    directory = records_root / safe_label / str(cap) / pathlib.Path(instance).name
                    directory.mkdir(parents=True, exist_ok=True)
                    run_env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(directory),
                                   ACACIA_DIAG_INSTANCE=instance)
                solver_run = run_systemd_scope(
                    cmd,
                    timeout=cap,
                    memory_max=args.memory_max,
                    memory_swap_max=args.memory_swap_max,
                    allowed_cpus=args.allowed_cpus,
                    cpu_quota=args.cpu_quota,
                    unit_prefix="acacia-syntcomp26-coverage",
                    env=run_env,
                )
                result, resource_reason = normalize_result(solver_run)
                usage = re.search(r"^ACACIA_RUSAGE ([0-9.]+) ([0-9.]+) ([0-9]+)$",
                                  solver_run.stderr, re.M)
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
                    "flags": args.flags,
                    "cpu_seconds": format(float(usage[1]) + float(usage[2]), ".6f") if usage else "",
                    "max_process_rss_bytes": str(int(usage[3]) * 1024) if usage else "",
                    "scope_memory_peak_bytes": (str(solver_run.memory_peak_bytes)
                                                if solver_run.memory_peak_bytes is not None else ""),
                    "scope_unit": solver_run.scope_unit,
                    **run_metadata,
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


@campaign_scope_guard("run-syntcomp26-coverage")
def campaign_main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return run(args)
    except CoverageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


def main() -> int:
    # Reporting must never enter the campaign guard (which inspects/cleans scopes).
    if sys.argv[1:2] == ["export-cactus"]:
        return export_cactus_main(sys.argv[2:])
    return campaign_main()


if __name__ == "__main__":
    raise SystemExit(main())
