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
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
from benchlib import campaign_scope_guard
from benchlib import read_part, run_process_group, run_systemd_scope
from suite_paths import load_source_map, read_tlsf_source_entries
import datetime
from types import SimpleNamespace
import tempfile
from scope_memory import append_memory_sidecar, memory_fields, write_memory_sidecar
from benchlib import (
    classify_run,
    write_csv,
)
from suite_paths import (
    TLSF_SOURCE_MAP_HEADER,
)
import math
from scope_memory import (
    MEMORY_COLUMNS, load_memory_sidecar,
)
import time
import uuid
from benchlib import dispatch_mode
from benchlib import (
    CACTUS_NON_SOLVED_RESULTS,
    TOOL_EXIT_CODES,
    RunResult,
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
    # Only run-subset.py's converted-pair legs (ltlsynt, Acacia 1.x) write
    # this: the untimed SyFCo conversion failed, so no solver ran.
    "SYFCO-FAIL",
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
ROUTE_COLUMNS = ["winner", "lift_elapsed", "fallback_start"]
# A derived series (tools/recycle-cap.py merge) keeps the observation columns
# and states, per row, whether it was measured at the series cap or recycled
# from a shorter-cap run, which run it came from, and that run's file digest.
DERIVED_COLUMNS = ["provenance", "source_run", "source_run_sha256", "source_cap_s"]
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
            writer.writerows({key: value for key, value in row.items()
                              if key not in MEMORY_COLUMNS or key in columns} for row in rows)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def load_output(
    path: pathlib.Path,
    *,
    allow_missing_memory: bool = False,
    expect_route_columns: bool | None = None,
) -> list[dict[str, str]]:
    try:
        stream = path.open(encoding="utf-8", newline="")
    except OSError as error:
        raise CoverageError(f"cannot read resume output {path}: {error}") from error
    with stream:
        reader = csv.DictReader(stream, delimiter="\t")
        legacy_columns = [column for column in OUTPUT_COLUMNS if column not in {"scope_unit", *MEMORY_COLUMNS}]
        optional = set(MEMORY_COLUMNS)
        if allow_missing_memory:
            optional |= {"max_process_rss_bytes", "scope_memory_peak_bytes"}
        header = [column for column in (reader.fieldnames or []) if column not in optional]
        expected = [[column for column in columns if column not in optional]
                    for columns in (
                        OUTPUT_COLUMNS,
                        legacy_columns,
                        OUTPUT_COLUMNS + ROUTE_COLUMNS,
                        OUTPUT_COLUMNS + DERIVED_COLUMNS,
                        OUTPUT_COLUMNS + ROUTE_COLUMNS + DERIVED_COLUMNS,
                    )]
        if header not in expected or len(set(reader.fieldnames or [])) != len(reader.fieldnames or []):
            raise CoverageError(
                f"resume output {path} has an unexpected header; expected "
                + "\t".join(OUTPUT_COLUMNS)
            )
        has_route_columns = all(column in header for column in ROUTE_COLUMNS)
        has_derived_columns = all(column in header for column in DERIVED_COLUMNS)
        if expect_route_columns is not None and has_route_columns != expect_route_columns:
            state = "with" if expect_route_columns else "without"
            raise CoverageError(f"resume output {path} must be {state} route columns")
        rows: list[dict[str, str]] = []
        for line_number, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise CoverageError(f"resume output {path}:{line_number} is malformed")
            try:
                if int(row["cap_s"]) <= 0:
                    raise ValueError
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
            if has_derived_columns:
                try:
                    int(row["source_cap_s"])
                except ValueError:
                    raise CoverageError(
                        f"resume output {path}:{line_number} has invalid source_cap_s"
                    ) from None
                if not row["provenance"] or not row["source_run"] or not re.fullmatch(
                        r"[0-9a-f]{64}", row["source_run_sha256"]):
                    raise CoverageError(
                        f"resume output {path}:{line_number} has incomplete provenance"
                    )
            row.setdefault("scope_unit", "")
            for column in MEMORY_COLUMNS:
                row.setdefault(column, "missing legacy observation/provenance"
                               if column.endswith("missing_reason") else "")
            if has_route_columns:
                for column in ROUTE_COLUMNS:
                    row.setdefault(column, "")
            for column in optional:
                row.setdefault(column, "")
            rows.append(dict(row))
    try:
        load_memory_sidecar(path, rows)
    except ValueError as error:
        raise CoverageError(str(error)) from error
    return rows


def route_record_fields(path: pathlib.Path) -> dict[str, str]:
    """Read the small, stable route attribution projected into the raw TSV."""
    if not path.exists():
        return dict.fromkeys(ROUTE_COLUMNS, "")
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CoverageError(f"invalid route record {path}: {error}") from error
    if not isinstance(payload, dict):
        raise CoverageError(f"invalid route record {path}: expected a JSON object")
    winner = payload.get("winner")
    if winner not in {"lifting", "fallback-pending"}:
        raise CoverageError(f"invalid route record {path}: unsupported winner {winner!r}")

    def number(name: str) -> str:
        value = payload.get(name)
        if value is None:
            return ""
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise CoverageError(f"invalid route record {path}: {name} is not numeric")
        if not math.isfinite(value) or value < 0:
            raise CoverageError(f"invalid route record {path}: {name} is invalid")
        return str(value)

    return {
        "winner": winner,
        "lift_elapsed": number("lift_elapsed"),
        "fallback_start": number("fallback_start"),
    }


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
            if row["expectation_source"] not in {"status", "exception", "observation"}:
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
        writer.writerow({key: value for key, value in row.items()
                         if key not in MEMORY_COLUMNS or key in columns})
        stream.flush()
        os.fsync(stream.fileno())


def conflict_row(
    row: dict[str, str], expectations: dict[str, tuple[str | None, str]],
    previous_rows=(),
) -> dict[str, str] | None:
    if row["result"] not in DECISIVE_RESULTS:
        return None
    expected, expectation_source = expectations.get(row["instance"], (None, "none"))
    if expected is None or row["result"] == expected:
        previous = next((old for old in previous_rows
                         if old["solver_label"] == row["solver_label"]
                         and old["instance"] == row["instance"]
                         and old["result"] in DECISIVE_RESULTS
                         and old["result"] != row["result"]), None)
        if previous is None:
            return None
        expected, expectation_source = previous["result"], "observation"
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


def normalize_result(run: RunResult, tool: str = "acacia") -> tuple[str, str]:
    """Map benchlib classifications to the coverage file's exact vocabulary."""
    result = classify_run(run, tool=tool)
    if result == "TIMEOUT":
        return "TIMEOUT", "timeout"
    if result == "RESOURCE_LIMIT":
        return "MEMOUT", "memory"
    if run.returncode < 0:
        return "CRASH", f"signal:{-run.returncode}"
    return result, ""


def quarantine_interrupted_records(directory: pathlib.Path) -> pathlib.Path | None:
    """Move aside records left by an attempt that never wrote its TSV row.

    The row is written only after the solver exits, so a directory that
    already holds files belongs to an interrupted attempt.  Mixing its records
    with the new attempt's would misattribute decisions; deleting them would
    lose evidence.  They are renamed, not removed.
    """
    if not directory.is_dir() or not any(directory.iterdir()):
        return None
    target = directory.with_name(f"{directory.name}.interrupted-{uuid.uuid4().hex[:12]}")
    directory.rename(target)
    print(f"moved interrupted phase records {directory} to {target}", file=sys.stderr)
    return target


def unique_in_order(instances: list[str]) -> list[str]:
    return list(dict.fromkeys(instances))


def summary_rows(
    solver_label: str,
    rows: list[dict[str, str]],
    *,
    instances: list[str] | None = None,
    largest_cap: int = 0,
    conflicts: list[dict[str, str]] | tuple = (),
) -> list[dict[str, str]]:
    """Summarize one series, independently of execution selection.

    The overall cap is the maximum of the requested cap and all observations
    (including conflicts) for this label. An explicit panel preserves its order
    and missing IDs; the default is the sorted union of observed IDs.
    """
    observed = [row for row in rows if row["solver_label"] == solver_label]
    disputed = [row for row in conflicts if row["solver_label"] == solver_label]
    for field in (
        "acacia_sha", "binary_sha256", "preset", "flags", "memory_max",
        "memory_swap_max", "allowed_cpus", "cpu_quota", "collect_rusage",
        "worker_records_dir",
    ):
        if len({row.get(field, "") for row in observed}) > 1:
            raise CoverageError(f"{solver_label}: mixed provenance field {field}")
    keys = [(row["instance"], int(row["cap_s"])) for row in observed]
    if len(keys) != len(set(keys)):
        raise CoverageError(f"{solver_label}: duplicate instance/cap observations")
    largest_cap = max([largest_cap, *(int(row["cap_s"]) for row in observed + disputed)])
    if largest_cap <= 0:
        raise CoverageError("summary needs observations or a positive --cap")
    if instances is None:
        instances = sorted({row["instance"] for row in observed + disputed})
    elif len(instances) != len(set(instances)) or any(not name for name in instances):
        raise CoverageError("summary panel has empty or duplicate instances")
    relevant: dict[str, list[dict[str, str]]] = {
        instance: [] for instance in instances
    }
    for row in observed:
        if row["instance"] in relevant:
            relevant[row["instance"]].append(row)

    summary_rows: list[dict[str, str]] = []
    for instance in instances:
        instance_rows = relevant[instance]
        decisive = [
            row for row in instance_rows if row["result"] in DECISIVE_RESULTS
        ]
        if len({row["result"] for row in decisive}) > 1 and not any(
            row["instance"] == instance for row in disputed
        ):
            raise CoverageError(f"{solver_label}: {instance}: opposing verdicts without a conflict sidecar")
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

    return summary_rows


def requested_caps_path(output: pathlib.Path) -> pathlib.Path:
    return output.with_name(f"{output.stem}-requested-caps.tsv")


def load_requested_caps(output: pathlib.Path) -> dict[str, set[int]]:
    """Keep unexecuted campaign caps independently of mutable summaries."""
    path = requested_caps_path(output)
    caps: dict[str, set[int]] = {}
    if not path.exists():
        return caps  # Campaigns predating this sidecar retain their observed floor.
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["solver_label", "cap_s"]:
            raise CoverageError(f"{path}: invalid requested caps header")
        for row in reader:
            try:
                cap = int(row["cap_s"])
                if cap <= 0 or not row["solver_label"] or None in row:
                    raise ValueError
            except (TypeError, ValueError) as error:
                raise CoverageError(f"{path}: invalid requested cap") from error
            caps.setdefault(row["solver_label"], set()).add(cap)
    return caps


def write_summary(
    output: pathlib.Path,
    solver_label: str,
    instances: list[str] | None,
    rows: list[dict[str, str]],
    largest_cap: int = 0,
) -> pathlib.Path:
    conflicts_path = output.with_name(f"{output.stem}-conflicts.tsv")
    conflicts = load_conflicts(conflicts_path) if conflicts_path.exists() else []
    summary_path = output.with_name(f"{output.stem}-summary.tsv")
    largest_cap = max(largest_cap, *load_requested_caps(output).get(solver_label, {0}))
    atomic_write_tsv(summary_path, SUMMARY_COLUMNS, summary_rows(
        solver_label, rows, instances=instances, largest_cap=largest_cap, conflicts=conflicts,
    ))
    return summary_path


def summarize_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Rebuild a summary from saved raw observations.")
    parser.add_argument("--runs", required=True, type=pathlib.Path)
    parser.add_argument("--solver-label", help="default: summarize every label independently")
    parser.add_argument("--list", type=pathlib.Path, help="explicit panel, including missing IDs")
    parser.add_argument("--cap", type=nonnegative_int, default=0,
                        help="overall cap floor; never lowers an observed cap")
    parser.add_argument("--output", type=pathlib.Path, help="default: RUNS-summary.tsv")
    args = parser.parse_args(argv)
    try:
        rows = load_output(args.runs)
        requested = load_requested_caps(args.runs)
        conflicts_path = args.runs.with_name(f"{args.runs.stem}-conflicts.tsv")
        conflicts = load_conflicts(conflicts_path) if conflicts_path.exists() else []
        labels = ([args.solver_label] if args.solver_label else
                  sorted({row["solver_label"] for row in rows + conflicts} | requested.keys()))
        instances = read_instance_list(args.list) if args.list else None
        output = args.output or args.runs.with_name(f"{args.runs.stem}-summary.tsv")
        if output.resolve() in {args.runs.resolve(), conflicts_path.resolve(),
                                requested_caps_path(args.runs).resolve(),
                                *( [args.list.resolve()] if args.list else [])}:
            raise CoverageError("summary output must be distinct from its inputs")
        combined = [row for label in labels for row in summary_rows(
            label, rows, instances=instances,
            largest_cap=max(args.cap, *requested.get(label, {0})), conflicts=conflicts)]
        atomic_write_tsv(output, SUMMARY_COLUMNS, combined)
    except (CoverageError, OSError) as error:
        parser.error(str(error))
    print(f"wrote {output}")
    if any(row["solver_label"] in labels for row in conflicts):
        print(f"unresolved verdict conflicts: {conflicts_path}", file=sys.stderr)
        return 3
    return 0


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
    parser.add_argument("--binary-identity", type=pathlib.Path, metavar="PATH",
                        help="underlying executable when --bin is a frozen-runtime wrapper; "
                             "hash this path in the observation")
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
    parser.add_argument("--collect-rusage", action=argparse.BooleanOptionalAction,
                        default=sys.platform == "linux",
                        help="collect CPU time (Linux wait4, GNU time elsewhere; default: Linux only); "
                             "--no-collect-rusage disables CPU time only (RSS is still recorded on Linux)")
    parser.add_argument("--worker-records-dir", type=pathlib.Path,
                        help="diagnostic run: capture transformed workers below this directory")
    parser.add_argument("--phase-records-dir", type=pathlib.Path,
                        help="diagnostic run: save opt-in per-process phase JSONL by instance")
    parser.add_argument(
        "--route-records",
        type=pathlib.Path,
        metavar="DIR",
        help=("enable wrapper attribution: put unique route JSON records below DIR "
              "and append winner/lift timing fields to the raw TSV"),
    )
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

    identity_binary = getattr(args, "binary_identity", None)
    if identity_binary is not None and not identity_binary.is_file():
        raise CoverageError(f"binary identity does not exist: {identity_binary}")
    binary_sha256 = sha256_file(identity_binary or binary)
    acacia_sha = args.acacia_sha if args.acacia_sha is not None else git_head()
    records_root = getattr(args, "worker_records_dir", None)
    if records_root is not None:
        records_root = records_root.resolve()
    phase_records_root = getattr(args, "phase_records_dir", None)
    if phase_records_root is not None:
        phase_records_root = phase_records_root.resolve()
    route_records_root = getattr(args, "route_records", None)
    if route_records_root is not None:
        route_records_root = route_records_root.resolve()
        route_records_root.mkdir(parents=True, exist_ok=True)
    output_columns = (
        OUTPUT_COLUMNS + ROUTE_COLUMNS if route_records_root is not None else OUTPUT_COLUMNS
    )
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
        rows = load_output(
            output, expect_route_columns=route_records_root is not None
        )
        if rows and "provenance" in rows[0]:
            raise CoverageError(f"resume output {output} is a derived series, not a run")
        for row in rows:
            if row["solver_label"] == args.solver_label and any(
                row.get(key) != value for key, value in run_metadata.items()
            ):
                raise CoverageError("resume configuration or binary differs from recorded campaign")
        # Upgrade the older header only after validating the recorded treatment.
        # Empty scope IDs preserve the distinction from a measured identifier.
        with output.open(newline="") as stream:
            previous_columns = next(csv.reader(stream, delimiter="\t"))
        if previous_columns != output_columns:
            original = output.with_name(f"{output.stem}-legacy.tsv")
            if original.exists():
                raise CoverageError(f"refusing to overwrite original observations {original}")
            original.write_bytes(output.read_bytes())
        atomic_write_tsv(output, output_columns, rows)
    else:
        rows = []
        atomic_write_tsv(output, output_columns, rows)

    requested = load_requested_caps(output) if args.resume else {}
    requested.setdefault(args.solver_label, set()).update(args.caps)
    atomic_write_tsv(requested_caps_path(output), ["solver_label", "cap_s"], [
        {"solver_label": label, "cap_s": str(cap)}
        for label in sorted(requested) for cap in sorted(requested[label])
    ])
    write_memory_sidecar(output, rows)

    conflicts_path = output.with_name(f"{output.stem}-conflicts.tsv")
    conflict_keys: set[tuple[str, str, int]] = set()
    collected_conflict_keys: set[tuple[str, str, int]] = set()
    if args.resume and conflicts_path.exists():
        conflict_rows = load_conflicts(conflicts_path)
    else:
        conflict_rows = []
        # Clear a stale sidecar when starting a new run, regardless of policy.
        atomic_write_tsv(conflicts_path, CONFLICT_COLUMNS, conflict_rows)
    if conflict_rows:
        conflict_keys = {
            (row["solver_label"], row["instance"], int(row["cap_s"]))
            for row in conflict_rows
        }
        collected_conflict_keys = {key for key in conflict_keys if key[0] == args.solver_label}
        if collected_conflict_keys and args.conflict_policy == "stop":
            write_summary(output, args.solver_label, None, rows, max(args.caps))
            print(f"unresolved verdict conflicts: {conflicts_path}", file=sys.stderr)
            return 1

    for row in rows:
        if row["solver_label"] != args.solver_label:
            continue
        conflict = conflict_row(row, expectations, rows)
        if conflict is not None and args.conflict_policy == "stop":
            key = (row["solver_label"], row["instance"], int(row["cap_s"]))
            if key not in conflict_keys:
                append_tsv_row(conflicts_path, CONFLICT_COLUMNS, conflict)
            write_summary(output, args.solver_label, None, rows, max(args.caps))
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

    # run_index counts invocations recorded in this file, including those
    # loaded on resume, so a resumed run never reuses an earlier run's
    # phase-record directory name.
    run_index = len(rows)
    with output.open("a", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=output_columns, delimiter="\t", lineterminator="\n"
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
                # Linux collects wait4 counters in the lifecycle owner, so
                # its child PID is the solver rather than a time wrapper.
                if getattr(args, "collect_rusage", False) and sys.platform != "linux":
                    cmd = ["/usr/bin/time", "-q", "-f", "ACACIA_RUSAGE %U %S %M", *cmd]
                run_env = None
                child_env_overrides: dict[str, str] = {}
                if records_root is not None:
                    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "_", args.solver_label)
                    directory = records_root / safe_label / str(cap) / pathlib.Path(instance).name
                    directory.mkdir(parents=True, exist_ok=True)
                    run_env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=str(directory),
                                   ACACIA_DIAG_INSTANCE=instance)
                    child_env_overrides.update({
                        "ACACIA_SPOT_CAPTURE_DIR": str(directory),
                        "ACACIA_DIAG_INSTANCE": instance,
                    })
                if phase_records_root is not None:
                    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "_", args.solver_label)
                    safe_instance = re.sub(r"[^A-Za-z0-9_.-]", "_", instance)
                    directory = (phase_records_root / safe_label / str(cap) /
                                 f"{safe_instance}-{run_index}")
                    quarantine_interrupted_records(directory)
                    directory.mkdir(parents=True, exist_ok=True)
                    run_env = dict(os.environ if run_env is None else run_env,
                                   ACACIA_PHASE_RECORDS=str(directory))
                    child_env_overrides["ACACIA_PHASE_RECORDS"] = str(directory)
                route_path = None
                if route_records_root is None:
                    # Keep the historical launch call exactly unchanged when
                    # route attribution is disabled.
                    solver_run = run_systemd_scope(
                        cmd,
                        timeout=cap,
                        memory_max=args.memory_max,
                        memory_swap_max=args.memory_swap_max,
                        allowed_cpus=args.allowed_cpus,
                        cpu_quota=args.cpu_quota,
                        unit_prefix="acacia-syntcomp26-coverage",
                        env=run_env,
                        **({"scope_env": child_env_overrides}
                           if phase_records_root is not None else {}),
                    )
                else:
                    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "_", args.solver_label)
                    safe_instance = re.sub(r"[^A-Za-z0-9_.-]", "_", instance)
                    route_directory = route_records_root / safe_label / str(cap)
                    route_directory.mkdir(parents=True, exist_ok=True)
                    route_path = route_directory / f"{safe_instance}.{uuid.uuid4().hex}.json"
                    # This is deliberately the last clock read before launch:
                    # directory creation and command assembly are charged, but
                    # systemd-run startup cannot escape the absolute deadline.
                    outer_deadline = time.monotonic() + cap
                    child_env_overrides.update({
                        "ACACIA_OUTER_DEADLINE_MONOTONIC": repr(outer_deadline),
                        "ACACIA_ROUTE_RECORD": str(route_path),
                    })
                    run_env = dict(os.environ if run_env is None else run_env)
                    run_env.update(child_env_overrides)
                    solver_run = run_systemd_scope(
                        cmd,
                        timeout=cap,
                        memory_max=args.memory_max,
                        memory_swap_max=args.memory_swap_max,
                        allowed_cpus=args.allowed_cpus,
                        cpu_quota=args.cpu_quota,
                        unit_prefix="acacia-syntcomp26-coverage",
                        env=run_env,
                        scope_env=child_env_overrides,
                    )
                result, resource_reason = normalize_result(solver_run)
                cpu_seconds = (solver_run.cpu_seconds
                               if getattr(args, "collect_rusage", False) else None)
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
                    "cpu_seconds": (format(float(usage[1]) + float(usage[2]), ".6f") if usage else
                                    format(cpu_seconds, ".6f") if cpu_seconds is not None else ""),
                    "max_process_rss_bytes": str(int(usage[3]) * 1024) if usage else "",
                    "scope_memory_peak_bytes": (str(solver_run.memory_peak_bytes)
                                                if solver_run.memory_peak_bytes is not None else ""),
                    "scope_unit": solver_run.scope_unit,
                    **run_metadata,
                    **memory_fields(solver_run),
                }
                if usage:
                    row.update(max_process_rss_bytes=str(int(usage[3]) * 1024),
                               max_process_rss_source="/usr/bin/time.ru_maxrss (not scope peak)",
                               max_process_rss_missing_reason="")
                if route_path is not None:
                    row.update(route_record_fields(route_path))
                writer.writerow({column: row[column] for column in output_columns})
                stream.flush()
                os.fsync(stream.fileno())
                append_memory_sidecar(output, row)
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

                conflict = conflict_row(row, expectations, rows)
                if conflict is not None and args.conflict_policy == "stop":
                    append_tsv_row(conflicts_path, CONFLICT_COLUMNS, conflict)
                    write_summary(output, args.solver_label, None, rows, max(args.caps))
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
        output, args.solver_label, None, rows, max(args.caps)
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
def campaign_main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except CoverageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


def main(argv=None) -> int:
    # Reporting never enters the campaign guard, which inspects scopes.
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments[:1] == ["export-cactus"]:
        return export_cactus_main(arguments[1:])
    if arguments[:1] == ["summarize"]:
        return summarize_main(arguments[1:])
    dispatched = dispatch_mode(MODES, arguments)
    return dispatched if dispatched is not None else campaign_main(arguments)


# benchmarking/run-subset.py: retained policy and helpers.
subset_ROOT = pathlib.Path(__file__).resolve().parents[1]


subset_DEFAULT_SOURCE_MAP = subset_ROOT / "tests/suites/benchmarks/syntcomp24/sources.tsv"


def subset_read_ltl_partition(inst_ltl):
    return read_part(os.path.splitext(inst_ltl)[0] + ".part")


def subset_read_instance_list(path):
    """Read a benchmark list, ignoring blank lines and manifest comments."""
    return [
        line
        for raw in open(path)
        if (line := raw.strip()) and not line.startswith("#")
    ]


def subset_read_semantics_map(path):
    """Frozen SyFCo machine model for pre-converted ltlsynt inputs."""
    with open(path, newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["instance", "semantics"]:
            raise ValueError(f"{path}: expected instance and semantics columns")
        mapping = {}
        for row in reader:
            name, model = row["instance"], row["semantics"]
            if not name or name in mapping or model not in {"Mealy", "Moore"}:
                raise ValueError(f"{path}: invalid or duplicate semantics for {name!r}")
            mapping[name] = model
    return mapping


def subset_read_adhoc_tlsf_map(path, tlsf_corpus=None):
    """Read a headerless ad-hoc TLSF map."""
    # One-off campaign maps intentionally retain their historical, unvalidated
    # format; maintained suite maps use read_tlsf_source_entries instead.
    tlsf_map = {}
    for raw in pathlib.Path(path).read_text().splitlines():
        if not raw.strip():
            continue
        name, tlsf = raw.split("\t")
        if tlsf_corpus is not None:
            tlsf = str(pathlib.Path(tlsf_corpus) / tlsf)
        tlsf_map[name] = tlsf
    return tlsf_map


def subset_read_tlsf_map(path, tlsf_corpus=None):
    """Read a validated suite map or an explicitly headerless ad-hoc map."""
    path = pathlib.Path(path)
    lines = path.read_text().splitlines()
    if not lines or lines[0] != TLSF_SOURCE_MAP_HEADER:
        return subset_read_adhoc_tlsf_map(path, tlsf_corpus)

    tlsf_map = read_tlsf_source_entries(path)
    if tlsf_corpus is not None:
        return {
            name: str(pathlib.Path(tlsf_corpus) / source)
            for name, source in tlsf_map.items()
        }
    return tlsf_map


def subset_build_command(tool, binary, ltl, ins, outs, semantics=None):
    """Return the argv for one tool on one .ltl/.part pair."""
    # This is the single place where each tool's CLI shape is defined.
    if tool == "acacia":
        return [binary, "-F", ltl, "-i", ins, "-o", outs]
    if tool == "acacia1x":
        return [
            binary,
            "-c",
            "BOTH",
            "-F",
            ltl,
            "--ins",
            ins,
            "--outs",
            outs,
        ]
    if tool == "ltlsynt":
        cmd = [
            binary,
            "--realizability",
            "-F",
            ltl,
            f"--ins={ins}",
            f"--outs={outs}",
        ]
        if semantics is not None:
            cmd.append(f"--semantics={semantics}")
        return cmd
    raise ValueError(f"unknown tool: {tool}")


def subset__parse_tlsf_semantics(semantics):
    parts = [part.strip() for part in semantics.split(",")]
    machine_models = [part for part in parts if part in ("Mealy", "Moore")]
    if len(machine_models) != 1:
        return None
    if any(part not in ("Mealy", "Moore", "Strict") for part in parts):
        return None
    return machine_models[0], "Strict" in parts


def subset_convert_tlsf(syfco, tlsf, output_dir):
    tlsf = pathlib.Path(tlsf)
    ltl = output_dir / f"{tlsf.stem}.ltl"
    part = output_dir / f"{tlsf.stem}.part"
    try:
        semantics_run = subprocess.run(
            [syfco, "--print-semantics", str(tlsf)],
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    if semantics_run.returncode != 0:
        return None

    semantics = subset__parse_tlsf_semantics(semantics_run.stdout.strip())
    if semantics is None:
        return None
    machine_model, _is_strict = semantics
    # IMPORTANT: TLSF Strict semantics has no ltlsynt counterpart.  syfco's
    # ltlxba printer emits the plain assumption-implies-guarantee reading, not
    # the strict one, so ltlsynt solves a genuinely different specification
    # from Acacia on Strict instances.
    if ltl.exists() and part.exists():
        return ltl, machine_model

    ltl.unlink(missing_ok=True)
    part.unlink(missing_ok=True)

    cmd = [
        syfco,
        "--format",
        "ltlxba",
        "--mode",
        "fully",
        "--part-file",
        str(part),
        str(tlsf),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except OSError:
        return None
    if result.returncode != 0 or not part.exists():
        ltl.unlink(missing_ok=True)
        part.unlink(missing_ok=True)
        return None
    ltl.write_text(result.stdout.rstrip() + "\n")
    return ltl, machine_model


def subset_coverage_module():
    """The coverage runner, for its observation schema and TSV helpers."""
    path = pathlib.Path(__file__).with_name("run-syntcomp26-coverage.py")
    spec = importlib.util.spec_from_file_location("syntcomp26_coverage_schema", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class subset_RawObservations:
    """Append-only, resumable observations in the coverage runner's schema.

    A resumed file must have been written by the same tool, binary bytes,
    flags, revision label, cap and memory regime; anything else is a
    different measurement and is refused.
    """

    IDENTITY = ("acacia_sha", "binary_sha256", "preset", "flags", "cap_s",
                "memory_max", "memory_swap_max")

    def __init__(self, path, *, label, tool, binary, revision, flags, cap,
                 memory_max, memory_swap_max, resume):
        self.coverage = subset_coverage_module()
        self.path = pathlib.Path(path)
        self.label = label
        digest = hashlib.sha256()
        with pathlib.Path(binary).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        self.identity = {
            "acacia_sha": revision, "binary_sha256": digest.hexdigest(),
            "preset": tool, "flags": flags, "cap_s": str(cap),
            "memory_max": memory_max, "memory_swap_max": memory_swap_max,
        }
        self.tool = tool
        if resume and self.path.exists():
            rows = self.coverage.load_output(self.path)
            for row in rows:
                if row["solver_label"] == label and any(
                        row[key] != self.identity[key] for key in self.IDENTITY):
                    raise SystemExit(f"{self.path}: resume identity differs from the recorded leg")
        else:
            if self.path.exists():
                raise SystemExit(f"{self.path} exists; pass --resume to continue it")
            rows = []
            self.coverage.atomic_write_tsv(self.path, self.coverage.OUTPUT_COLUMNS, [])
        if resume and self.path.exists():
            with self.path.open(newline="") as stream:
                columns = next(csv.reader(stream, delimiter="\t"))
            if columns != self.coverage.OUTPUT_COLUMNS:
                original = self.path.with_name(f"{self.path.stem}-legacy.tsv")
                if original.exists():
                    raise SystemExit(f"refusing to overwrite original observations {original}")
                original.write_bytes(self.path.read_bytes())
                self.coverage.atomic_write_tsv(self.path, self.coverage.OUTPUT_COLUMNS, rows)
        write_memory_sidecar(self.path, rows)
        self.rows = rows
        self.done = {row["instance"] for row in rows if row["solver_label"] == label}

    def _base(self, instance, source_file):
        return {
            **dict.fromkeys(self.coverage.OUTPUT_COLUMNS, ""),
            **self.identity,
            "solver_label": self.label, "instance": instance,
            "tlsf_file": pathlib.Path(source_file).name if source_file else "",
            "expectation_source": "none", "run_index": str(len(self.rows)),
            "timestamp_utc": datetime.datetime.now(datetime.timezone.utc)
            .isoformat().replace("+00:00", "Z"),
            "collect_rusage": "false",
        }

    def _append(self, row):
        self.coverage.append_tsv_row(self.path, self.coverage.OUTPUT_COLUMNS, row)
        append_memory_sidecar(self.path, row)
        self.rows.append(row)
        self.done.add(row["instance"])

    def record_run(self, instance, source_file, run):
        result, reason = self.coverage.normalize_result(run, self.tool)
        row = self._base(instance, source_file)
        row.update({
            "result": result, "seconds": str(run.seconds), "exit_code": str(run.returncode),
            "timed_out": str(run.timed_out).lower(), "resource_reason": reason,
            "stdout_bytes": str(run.stdout_bytes), "stderr_bytes": str(run.stderr_bytes),
            "scope_memory_peak_bytes": ("" if run.memory_peak_bytes is None
                                        else str(run.memory_peak_bytes)),
            "scope_unit": run.scope_unit,
            **memory_fields(run),
        })
        self._append(row)

    def record_syfco_failure(self, instance, source_file):
        row = self._base(instance, source_file)
        row.update({"result": "SYFCO-FAIL", "seconds": "0", "exit_code": "-1",
                    "timed_out": "false", "resource_reason": "syfco",
                    "stdout_bytes": "0", "stderr_bytes": "0"})
        row.update(scope_memory_peak_missing_reason="not executed: SyFCo conversion failed",
                   scope_memory_events_missing_reason="not executed: SyFCo conversion failed",
                   max_process_rss_missing_reason="not executed: SyFCo conversion failed")
        self._append(row)


def subset_main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True)
    p.add_argument("--binary-identity", type=pathlib.Path,
                   help="underlying executable when --bin is a frozen-runtime wrapper")
    p.add_argument(
        "--tool", choices=("acacia", "acacia1x", "ltlsynt"), default="acacia"
    )
    p.add_argument("--instances-dir",
                   help="flat corpus override (disables the default source map)")
    p.add_argument(
        "--source-map",
        default=str(subset_DEFAULT_SOURCE_MAP),
        help="suite sources.tsv used when --instances-dir is omitted",
    )
    p.add_argument("--from-csv", help="loss-set CSV to pick instances from")
    p.add_argument("--category", action="append", default=[],
                   help="filter: keep these categories (repeatable)")
    p.add_argument("--real", action="append", default=[],
                   help="filter: keep these realizability values (real/unreal)")
    p.add_argument("--list", help="alternatively, a file of instance basenames")
    p.add_argument("--flags", default="", help="extra tool flags, e.g. '-u automaton'")
    p.add_argument("--runner-prefix", default="",
                   help="optional external wrapper, e.g. systemd-run/cgexec/timeout")
    p.add_argument("--systemd-scope", action="store_true",
                   help="run each solver in a named, memory-limited user scope")
    p.add_argument("--memory-max", default="8G")
    p.add_argument("--memory-swap-max", default="0")
    p.add_argument("--timeout", type=float, default=25.0)
    p.add_argument("--csv", default=None)
    p.add_argument("--limit", type=int, default=0, help="cap number of instances (0=all)")
    p.add_argument(
        "--tlsf-map",
        help="TSV of 'instance<TAB>path.tlsf'.  Both suite tlsf-sources.tsv "
             "files with an 'instance<TAB>tlsf' header and headerless ad-hoc "
             "maps are accepted.  Relative paths are resolved from the "
             "current working directory unless --tlsf-corpus names the "
             "corpus directory produced by benchmarking/syntcomp-corpus.py "
             "materialize.  With --tool acacia, feed the "
             "TLSF source with -T instead of the converted .ltl/.part pair; only "
             "this native TLSF route carries TLSF's indexed-family metadata, "
             "which the equivariant solver consumes as symmetry hints, so the "
             "two Acacia routes are not equivalent inputs.  With --tool "
             "ltlsynt, feed syfco's unadapted formula from a cached or "
             "temporary .ltl/.part pair together with ltlsynt's explicit "
             "--semantics flag.  This is both entrant-realistic and stronger "
             "than adapting the formula.",
    )
    p.add_argument(
        "--tlsf-corpus",
        metavar="DIR",
        help="resolve --tlsf-map paths relative to the corpus directory produced "
             "by benchmarking/syntcomp-corpus.py materialize",
    )
    p.add_argument("--syfco", default="syfco")
    p.add_argument("--syfco-cache", metavar="DIR",
                   help="directory for cached syfco-derived .ltl/.part pairs")
    p.add_argument("--raw-tsv", metavar="PATH",
                   help="append coverage-schema observations here as they finish "
                        "(requires --systemd-scope, --solver-label and an integral --timeout)")
    p.add_argument("--solver-label", help="series label recorded with --raw-tsv")
    p.add_argument("--revision", default="",
                   help="tool revision/version label recorded with --raw-tsv")
    p.add_argument("--resume", action="store_true",
                   help="with --raw-tsv: skip instances already recorded for this label")
    p.add_argument("--syfco-failures", metavar="PATH",
                   help="with --instances-dir: instances whose converted pair is known to be "
                        "absent because SyFCo failed; they are recorded as SYFCO-FAIL")
    p.add_argument("--semantics-map", metavar="PATH",
                   help="with pre-converted ltlsynt pairs: frozen instance to Mealy/Moore map")
    args = p.parse_args()
    if args.systemd_scope and args.runner_prefix:
        p.error("--systemd-scope and --runner-prefix are mutually exclusive")
    if args.raw_tsv and (not args.systemd_scope or not args.solver_label
                         or args.timeout != int(args.timeout)):
        p.error("--raw-tsv needs --systemd-scope, --solver-label and an integral --timeout")
    if args.resume and not args.raw_tsv:
        p.error("--resume applies to --raw-tsv")
    if args.resume and args.csv:
        # The CSV holds only this invocation's rows; beside a resumed raw file
        # it would silently present a partial leg as complete.
        p.error("--csv cannot be combined with --resume; export the raw TSV instead")
    if args.tool == "acacia1x" and args.tlsf_map:
        p.error(
            "Acacia v1 predates the TLSF frontend and must be fed converted "
            ".ltl/.part pairs"
        )
    if args.semantics_map and (args.tool != "ltlsynt" or not args.instances_dir):
        p.error("--semantics-map requires ltlsynt with --instances-dir")

    insts = []
    if args.from_csv:
        for row in csv.DictReader(open(args.from_csv)):
            if args.category and row["category"] not in args.category:
                continue
            if args.real and row["real"] not in args.real:
                continue
            insts.append(row["instance"])
    elif args.list:
        insts = subset_read_instance_list(args.list)
    else:
        sys.exit("need --from-csv or --list")
    if args.limit:
        insts = insts[:args.limit]

    extra = shlex.split(args.flags)
    runner_prefix = shlex.split(args.runner_prefix)
    source_map = None if args.instances_dir else load_source_map(pathlib.Path(args.source_map))
    tlsf_map = {}
    if args.tlsf_map:
        tlsf_map = subset_read_tlsf_map(args.tlsf_map, args.tlsf_corpus)
    temporary_cache = None
    syfco_cache = None
    if args.tool == "ltlsynt" and tlsf_map:
        if args.syfco_cache:
            syfco_cache = pathlib.Path(args.syfco_cache)
            syfco_cache.mkdir(parents=True, exist_ok=True)
        else:
            temporary_cache = tempfile.TemporaryDirectory()
            syfco_cache = pathlib.Path(temporary_cache.name)
    rows = []
    solved = 0
    tot_time = 0.0
    known_syfco_failures = set()
    if args.syfco_failures:
        if not args.instances_dir:
            p.error("--syfco-failures applies to --instances-dir")
        known_syfco_failures = set(subset_read_instance_list(args.syfco_failures))
    semantics_map = subset_read_semantics_map(args.semantics_map) if args.semantics_map else {}
    if args.semantics_map and set(insts) - known_syfco_failures - set(semantics_map):
        p.error("--semantics-map lacks a listed converted input")
    memory_rows = []
    raw = None
    if args.raw_tsv:
        raw = subset_RawObservations(
            args.raw_tsv, label=args.solver_label, tool=args.tool,
            binary=args.binary_identity or args.bin,
            revision=args.revision, flags=args.flags, cap=int(args.timeout),
            memory_max=args.memory_max, memory_swap_max=args.memory_swap_max,
            resume=args.resume)
    print(f"# bin={args.bin}\n# flags={args.flags!r}  timeout={args.timeout}s  n={len(insts)}")
    for base in insts:
        if raw is not None and base in raw.done:
            continue
        if tlsf_map:
            tlsf = tlsf_map.get(base)
            if tlsf is None or not pathlib.Path(tlsf).exists():
                print(f"  {base:44s} MISSING-TLSF")
                if raw is not None:
                    sys.exit(f"{base}: TLSF source missing; a campaign leg cannot skip it")
                continue
            if args.tool == "acacia":
                # Acacia's native TLSF route does not use an .ltl/.part pair.
                cmd = [args.bin, "-T", tlsf]
            else:
                converted = subset_convert_tlsf(args.syfco, tlsf, syfco_cache)
                if converted is None:
                    print(f"  {base:44s} SYFCO-FAIL")
                    memory_rows.append({"instance": base, **memory_fields(SimpleNamespace(
                        scope_memory_peak_missing_reason="not executed: SyFCo conversion failed",
                        scope_memory_events_missing_reason="not executed: SyFCo conversion failed",
                        max_process_rss_missing_reason="not executed: SyFCo conversion failed"))})
                    rows.append({"instance": base, "result": "SYFCO-FAIL",
                                 "seconds": 0.0, "exit": -1})
                    if raw is not None:
                        raw.record_syfco_failure(base, tlsf)
                    continue
                ltl_path, machine_model = converted
                ltl = str(ltl_path)
                ins, outs = subset_read_ltl_partition(ltl)
                cmd = subset_build_command(
                    args.tool, args.bin, ltl, ins, outs, machine_model
                )
            source_file = tlsf
        else:
            ltl_path = (pathlib.Path(args.instances_dir) / base
                        if args.instances_dir else source_map.get(base))
            if ltl_path is None or not ltl_path.exists():
                if base in known_syfco_failures:
                    print(f"  {base:44s} SYFCO-FAIL")
                    memory_rows.append({"instance": base, **memory_fields(SimpleNamespace(
                        scope_memory_peak_missing_reason="not executed: SyFCo conversion failed",
                        scope_memory_events_missing_reason="not executed: SyFCo conversion failed",
                        max_process_rss_missing_reason="not executed: SyFCo conversion failed"))})
                    rows.append({"instance": base, "result": "SYFCO-FAIL",
                                 "seconds": 0.0, "exit": -1})
                    if raw is not None:
                        raw.record_syfco_failure(base, base)
                    continue
                print(f"  {base:44s} MISSING")
                if raw is not None:
                    sys.exit(f"{base}: converted pair missing and not a declared SyFCo failure")
                continue
            if base in known_syfco_failures:
                sys.exit(f"{base}: declared a SyFCo failure but its pair exists")
            source_file = ltl_path
            ltl = str(ltl_path)
            ins, outs = subset_read_ltl_partition(ltl)
            # Plain .ltl inputs carry no semantics, so only the TLSF route adds
            # ltlsynt's --semantics flag.
            cmd = subset_build_command(args.tool, args.bin, ltl, ins, outs,
                                semantics_map.get(base))
        cmd = runner_prefix + cmd + extra
        if args.systemd_scope:
            run = run_systemd_scope(
                cmd,
                args.timeout,
                args.memory_max,
                args.memory_swap_max,
                unit_prefix="acacia-subset",
            )
        else:
            run = run_process_group(cmd, args.timeout)
        observation = {"instance": base, **memory_fields(run)}
        memory_rows.append(observation)
        print(f"# memory {base}: {observation}")
        res = classify_run(run, args.tool)
        if raw is not None:
            raw.record_run(base, source_file, run)
        ok = res in ("REALIZABLE", "UNREALIZABLE")
        solved += ok
        tot_time += run.seconds
        rows.append({"instance": base, "result": res, "seconds": round(run.seconds, 3),
                     "exit": run.returncode})
        print(f"  {base:44s} {res:13s} {run.seconds:7.2f}s")

    print(f"\nsolved {solved}/{len(rows)}   total {tot_time:.1f}s")
    if args.csv:
        write_csv(args.csv, rows, ["instance", "result", "seconds", "exit"])
        print(f"wrote {args.csv}")
        if memory_rows:
            memory_path = str(args.csv) + ".memory.tsv"
            with open(memory_path, "w", newline="") as stream:
                writer = csv.DictWriter(stream, list(memory_rows[0]), delimiter="\t")
                writer.writeheader()
                writer.writerows(memory_rows)
            print(f"wrote {memory_path}")
    if temporary_cache is not None:
        temporary_cache.cleanup()


plan_ROOT = pathlib.Path(__file__).resolve().parents[1]


sys.path.insert(0, str(plan_ROOT / "benchmarking"))


plan_NATIVE = plan_ROOT / "benchmarking/run-syntcomp26-coverage.py"


plan_SUBSET = plan_NATIVE


plan_SAFE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\Z")


plan_HEX = re.compile(r"[0-9a-f]{64}\Z")


def plan_load_coverage():
    from benchlib import load_tool
    return load_tool("benchmarking/run-syntcomp26-coverage.py")


plan_coverage = plan_load_coverage()


class plan_CampaignError(ValueError):
    pass


def plan_native_flags(entry):
    """Return the flags actually passed to a native series."""
    arms = "" if entry["use_default_arms"] else shlex.join(
        ["--arms", ",".join(entry["arms"])])
    return " ".join(part for part in (arms, entry["flags"]) if part)


def plan_sha256_file(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def plan_directory_digest(path):
    digest = hashlib.sha256()
    for item in sorted(path.rglob("*")):
        if not item.is_file():
            continue
        digest.update(json.dumps([str(item.relative_to(path)), plan_sha256_file(item)],
                                 separators=(",", ":")).encode() + b"\n")
    return digest.hexdigest()


def plan_preserved_runtime_identity(runtime, sums_file):
    """Verify the soname files against the preserved 2.15.1.dev goodset."""
    if runtime is None or not runtime.is_dir():
        raise plan_CampaignError("pre-2.16 series needs an existing runtime_lib_dir")
    if sums_file is None or not sums_file.is_file():
        raise plan_CampaignError("pre-2.16 series needs an existing runtime_sha256s")
    expected = {}
    for line in sums_file.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        expected[pathlib.Path(name).name] = digest
    libraries = {}
    for soname in ("libspot.so.0", "libbddx.so.0"):
        path = runtime / soname
        if not path.is_file():
            raise plan_CampaignError(f"preserved runtime missing {path}")
        target = path.resolve()
        digest = plan_sha256_file(path)
        if digest != expected.get(target.name):
            raise plan_CampaignError(f"preserved runtime SHA-256 mismatch: {path}")
        libraries[soname] = digest
    return {"sha256s_path": str(sums_file),
            "sha256s_sha256": plan_sha256_file(sums_file), "libraries": libraries,
            "verification": ("soname bytes match the preserved SHA256SUMS; "
                             "checksum-file authority is unverified")}


def plan_resolve(base, value):
    path = pathlib.Path(value)
    return (path if path.is_absolute() else base / path).resolve()


def plan_corpus_digest(instances, mapping, corpus):
    digest = hashlib.sha256()
    for name in instances:
        if name not in mapping:
            raise plan_CampaignError(f"{name}: missing from TLSF map")
        path = plan_resolve(corpus, mapping[name])
        if not path.is_file() or not path.is_relative_to(corpus):
            raise plan_CampaignError(f"{name}: TLSF path outside or missing from corpus: {path}")
        digest.update(json.dumps([name, str(path.relative_to(corpus)), plan_sha256_file(path)],
                                 separators=(",", ":")).encode() + b"\n")
    return digest.hexdigest()


def plan_verify_observation_labels(entry, rows, *, tlsf_map, corpus, status_exceptions):
    """Check row source labels against the campaign's frozen input content."""
    native = entry["kind"] in {"arm", "race"}
    try:
        mapping = plan_coverage.read_tlsf_map(pathlib.Path(tlsf_map))
        exceptions = (plan_coverage.read_status_exceptions(
            pathlib.Path(status_exceptions), pathlib.Path(corpus), required=True)
            if native else {})
    except plan_coverage.CoverageError as error:
        raise plan_CampaignError(str(error)) from error
    for row in rows:
        name = row["instance"]
        if name not in mapping:
            raise plan_CampaignError(f"{name}: absent from frozen TLSF map")
        tlsf = mapping[name]
        expected_file = (pathlib.Path(tlsf).name if native or entry["tool"] == "acacia"
                         else pathlib.Path(name).name)
        try:
            source = ("exception" if tlsf in exceptions else
                      "status" if plan_coverage.expected_verdict(pathlib.Path(corpus) / tlsf)
                      is not None else "none") if native else "none"
        except plan_coverage.CoverageError as error:
            raise plan_CampaignError(str(error)) from error
        if row["tlsf_file"] != expected_file or row["expectation_source"] != source:
            raise plan_CampaignError(f"{name}: observation TLSF or expected-status label "
                                "differs from frozen inputs")


def plan_wrapper_text(binary, runtime):
    return ("#!/bin/sh\n"
            f"export LD_LIBRARY_PATH={shlex.quote(str(runtime))}:\"${{LD_LIBRARY_PATH-}}\"\n"
            f"exec {shlex.quote(str(binary))} \"$@\"\n")


def plan_read_comparison(path, cap, *, selected=None, subset_list=None):
    if type(cap) is not int or cap <= 0:
        raise plan_CampaignError("campaign cap must be a positive integer")
    path = path.resolve()
    spec = json.loads(path.read_text())
    if spec.get("schema") != 1 or not isinstance(spec.get("series"), list):
        raise plan_CampaignError("comparison needs schema 1 and a series list")
    if spec.get("memory_max") != "8G" or spec.get("memory_swap_max") != "0":
        raise plan_CampaignError("P4 requires MemoryMax=8G and MemorySwapMax=0")
    base = path.parent
    paths = {key: plan_resolve(base, spec[key]) for key in
             ("list", "tlsf_map", "tlsf_corpus", "status_exceptions")}
    runtime_sha256s = (plan_resolve(base, spec["runtime_sha256s"])
                      if spec.get("runtime_sha256s") else None)
    if runtime_sha256s is not None:
        paths["runtime_sha256s"] = runtime_sha256s
    if any(not value.exists() for value in paths.values()):
        raise plan_CampaignError("a comparison input path does not exist")
    original_list = paths["list"]
    full_instances = plan_coverage.read_instance_list(original_list)
    if not full_instances or len(set(full_instances)) != len(full_instances):
        raise plan_CampaignError("frozen list is empty or contains duplicate instances")
    if subset_list is not None:
        subset_list = pathlib.Path(subset_list).resolve()
        if not subset_list.is_file():
            raise plan_CampaignError(f"subset list missing: {subset_list}")
        paths["list"] = subset_list
    instances = plan_coverage.read_instance_list(paths["list"])
    if not instances or len(set(instances)) != len(instances):
        raise plan_CampaignError("comparison list is empty or contains duplicate instances")
    if not set(instances) <= set(full_instances):
        raise plan_CampaignError("subset list contains an instance outside the frozen list")
    mapping = plan_coverage.read_tlsf_map(paths["tlsf_map"])
    corpus_sha = plan_corpus_digest(instances, mapping, paths["tlsf_corpus"])
    frozen = {"schema": 1, "comparison": str(path), "comparison_sha256": plan_sha256_file(path),
              "frozen_list": str(original_list),
              "frozen_list_sha256": plan_sha256_file(original_list),
              "cap_s": cap, "inputs": {key: {"path": str(value), "sha256": plan_sha256_file(value)}
                                      for key, value in paths.items() if value.is_file()},
              "corpus": str(paths["tlsf_corpus"]), "corpus_sha256": corpus_sha,
              "instances": len(instances), "memory_max": "8G", "memory_swap_max": "0",
              "verification": ("input paths and bytes are checked on resume and execution; "
                               "comparison authority, source revisions, solver outcomes and "
                               "host sample accuracy are unverified"),
              "series": {}}
    names = set()
    for record in spec["series"]:
        label = record.get("label", "")
        if selected is not None and label not in selected:
            continue
        kind = record.get("kind", "")
        if not plan_SAFE.fullmatch(label) or label in names:
            raise plan_CampaignError(f"invalid or duplicate series label {label!r}")
        names.add(label)
        if kind not in {"arm", "race", "legacy"}:
            raise plan_CampaignError(f"{label}: kind must be arm, race or legacy")
        binary = plan_resolve(base, record["binary"])
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise plan_CampaignError(f"{label}: executable missing: {binary}")
        binary_sha = plan_sha256_file(binary)
        if not plan_HEX.fullmatch(record.get("binary_sha256", "")) or \
                binary_sha != record["binary_sha256"]:
            raise plan_CampaignError(f"{label}: frozen binary SHA-256 mismatch")
        runtime = plan_resolve(base, record["runtime_lib_dir"]) if record.get("runtime_lib_dir") else None
        if not isinstance(record.get("pre_spot_216", False), bool):
            raise plan_CampaignError(f"{label}: pre_spot_216 must be a boolean")
        pre_216 = record.get("pre_spot_216", False) or kind == "legacy" and \
            record.get("tool") == "acacia1x"
        runtime_identity = (plan_preserved_runtime_identity(runtime, runtime_sha256s)
                            if pre_216 or runtime else None)
        arms = record.get("arms", [])
        if not isinstance(arms, list):
            raise plan_CampaignError(f"{label}: arms must be an array")
        if "portfolio_size" not in record:
            raise plan_CampaignError(f"{label}: portfolio_size must be declared")
        size = record["portfolio_size"]
        default_arms = record.get("use_default_arms", False)
        if not isinstance(default_arms, bool):
            raise plan_CampaignError(f"{label}: use_default_arms must be a boolean")
        flags = record.get("flags", "")
        if not isinstance(flags, str):
            raise plan_CampaignError(f"{label}: flags must be a string")
        try:
            flag_words = shlex.split(flags)
        except ValueError as error:
            raise plan_CampaignError(f"{label}: invalid flags: {error}") from error
        if kind != "legacy" and any(word == "--arms" or word.startswith("--arms=")
                                    for word in flag_words):
            raise plan_CampaignError(f"{label}: put --arms in the arms array")
        if kind != "legacy" and any(word.startswith(("-r", "-u")) and
                                    not word.startswith("--") for word in flag_words):
            raise plan_CampaignError(f"{label}: flags cannot change the launched arms")
        if kind != "legacy" and (not all(isinstance(a, str) and a for a in arms) or
                                  len(set(arms)) != len(arms)):
            raise plan_CampaignError(f"{label}: invalid arms")
        if kind == "arm" and len(arms) != 1:
            raise plan_CampaignError(f"{label}: standalone leg needs one arm")
        if kind == "race":
            if len(arms) < 2:
                raise plan_CampaignError(f"{label}: race needs distinct arms")
        if kind == "legacy":
            if size is not None:
                raise plan_CampaignError(f"{label}: legacy tool needs portfolio_size null")
        elif type(size) is not int or size != len(arms):
            raise plan_CampaignError(f"{label}: portfolio_size must match launched arms")
        if default_arms and kind != "race":
            raise plan_CampaignError(f"{label}: only a race can use build default arms")
        if kind == "legacy" and arms:
            raise plan_CampaignError(f"{label}: legacy tool does not accept arms")
        build_options = None
        if default_arms:
            if not record.get("build_options"):
                raise plan_CampaignError(f"{label}: build_options required for default arms")
            build_options = plan_resolve(base, record["build_options"])
            options = json.loads(build_options.read_text())
            matches = [item["value"] for item in options
                       if item.get("name") == "acacia_default_arms"]
            if len(matches) != 1 or not isinstance(matches[0], str) or \
                    matches[0].split(",") != arms:
                raise plan_CampaignError(f"{label}: build default arms differ from declared arms")
        tool = record.get("tool", "acacia")
        if kind == "legacy" and tool not in {"ltlsynt", "acacia1x", "acacia"}:
            raise plan_CampaignError(f"{label}: unsupported legacy tool {tool}")
        if kind != "legacy" and tool != "acacia":
            raise plan_CampaignError(f"{label}: native series require Acacia")
        if kind == "legacy" and tool in {"ltlsynt", "acacia1x"}:
            if "instances_dir" not in record or "syfco_failures" not in record:
                raise plan_CampaignError(f"{label}: converted tool needs frozen pairs and failures")
            if tool == "ltlsynt" and "semantics_map" not in record:
                raise plan_CampaignError(f"{label}: ltlsynt converted pairs need a semantics_map")
        runner = plan_SUBSET if kind == "legacy" else plan_NATIVE
        entry = {
            "label": label, "kind": kind, "tool": tool, "binary": str(binary),
            "binary_sha256": binary_sha, "source_revision": record["source_revision"],
            "preset": record.get("preset", ""), "flags": flags,
            "arms": arms, "portfolio_size": size, "use_default_arms": default_arms,
            "build_options": str(build_options) if build_options else "",
            "build_options_sha256": plan_sha256_file(build_options) if build_options else "",
            "runtime_lib_dir": str(runtime) if runtime else "",
            "runtime_lib_sha256": plan_directory_digest(runtime) if runtime else "",
            "pre_spot_216": pre_216, "runtime_identity": runtime_identity,
            "wrapper_sha256": (hashlib.sha256(plan_wrapper_text(binary, runtime).encode()).hexdigest()
                               if runtime else ""),
            "runner": str(runner), "runner_sha256": plan_sha256_file(runner),
            "route_records": bool(record.get("route_records", False)),
            "source_revision_verification": "unverified declaration from comparison JSON",
            "pre_spot_216_verification": "unverified declaration; runtime bytes verified when enabled",
            "observation_verification": "runner output is checked against cap, panel and treatment; solver result is unverified",
        }
        if kind == "legacy" and tool in {"ltlsynt", "acacia1x"}:
            for key in ("instances_dir", "syfco_failures"):
                item = plan_resolve(base, record[key])
                if not item.exists():
                    raise plan_CampaignError(f"{label}: {key} missing: {item}")
                entry[key] = str(item)
                entry[f"{key}_sha256"] = (plan_sha256_file(item) if item.is_file()
                                           else plan_directory_digest(item))
            failures = set(plan_coverage.read_instance_list(pathlib.Path(entry["syfco_failures"])))
            pairs = {}
            pair_root = pathlib.Path(entry["instances_dir"])
            for name in instances:
                if name in failures:
                    continue
                pairs[name] = {}
                input_path = (pair_root / name).resolve()
                if not input_path.is_relative_to(pair_root):
                    raise plan_CampaignError(f"{label}: converted input outside pair directory: {name}")
                part_path = input_path.with_suffix(".part")
                for role, pair in (("input", input_path), ("part", part_path)):
                    if not pair.is_file():
                        raise plan_CampaignError(f"{label}: converted pair missing: {pair}")
                    pairs[name][role] = {"path": str(pair), "sha256": plan_sha256_file(pair)}
            entry["converted_pairs"] = pairs
            entry["conversion_verification"] = (
                "converted pair, SyFCo failure list and semantics map bytes are pinned; "
                "their origin and semantic correctness are unverified")
            if tool == "ltlsynt":
                semantics_path = plan_resolve(base, record["semantics_map"])
                if not semantics_path.is_file():
                    raise plan_CampaignError(f"{label}: semantics_map missing: {semantics_path}")
                with semantics_path.open(newline="") as stream:
                    reader = csv.DictReader(stream, delimiter="\t")
                    if reader.fieldnames != ["instance", "semantics"]:
                        raise plan_CampaignError(f"{label}: invalid semantics_map header")
                    semantics = {}
                    for row in reader:
                        name, model = row["instance"], row["semantics"]
                        if not name or name in semantics or model not in {"Mealy", "Moore"}:
                            raise plan_CampaignError(f"{label}: invalid semantics_map row {name!r}")
                        semantics[name] = model
                if set(instances) - failures - set(semantics):
                    raise plan_CampaignError(f"{label}: semantics_map lacks a converted input")
                entry["semantics_map"] = str(semantics_path)
                entry["semantics_map_sha256"] = plan_sha256_file(semantics_path)
        if kind == "legacy" and tool != "acacia":
            syfco_name = record.get("syfco", "syfco")
            syfco = shutil.which(syfco_name)
            if syfco is None:
                raise plan_CampaignError(f"{label}: SyFCo executable missing: {syfco_name}")
            syfco = pathlib.Path(syfco).resolve()
            entry["syfco"] = str(syfco)
            entry["syfco_sha256"] = plan_sha256_file(syfco)
        frozen["series"][label] = entry
    if "portfolio_size" in spec:
        raise plan_CampaignError("portfolio_size belongs on each series")
    if selected is not None and names != set(selected):
        raise plan_CampaignError(f"selected series absent from comparison: {sorted(set(selected) - names)}")
    if not names:
        raise plan_CampaignError("no series selected")
    return frozen, paths




MODES = {
    'subset': subset_main,
}

if __name__ == "__main__":
    raise SystemExit(main())
