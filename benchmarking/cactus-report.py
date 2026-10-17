#!/usr/bin/env python3
"""Turn run-subset.py CSVs into a PAR-2 table and a cactus plot.

Only REALIZABLE and UNREALIZABLE rows are solved.  Every other outcome is
charged twice the timeout in the PAR-2 score and omitted from the cactus
curve.  A virtual-best series models a portfolio by retaining the fastest
solved member for each instance.

Example:
  cactus-report.py \\
      --csv current=current.csv --csv baseline=baseline.csv \\
      --virtual-best portfolio=current,baseline \\
      --title "SYNTCOMP comparison" --timeout 17 \\
      --out-prefix plots/comparison --markdown plots/comparison.md
"""

from __future__ import annotations
from benchlib import dispatch_mode
import argparse
import csv
from collections import Counter
import json
import math
from pathlib import Path
import random
import re
import shlex
import sys
from benchlib import PhaseRecords, load_phase_records, phase_record_dir, phase_plan_stage
from benchlib import load_tool
import hashlib
import pathlib
from scope_memory import memory_statistic
from benchlib import par2 as par2_score
import io
from benchlib import (
    CACTUS_NON_SOLVED_RESULTS as NON_SOLVED_RESULTS,
    CACTUS_SOLVED_RESULTS as SOLVED_RESULTS,
)
from collections.abc import Mapping, Sequence
from dataclasses import dataclass

_panel = load_tool("benchmarking/make-panel.py")
parse_origin = _panel.metadata_parse_origin
read_tsv = _panel.metadata_read_tsv


KNOWN_RESULTS = SOLVED_RESULTS | frozenset(NON_SOLVED_RESULTS)
MARKERS = ("o", "s", "^", "D", "v", "P", "X", "*", "+", "x")
LINESTYLES = ("-", "--", "-.", ":")


@dataclass(frozen=True)
class RunResult:
    result: str
    seconds: float
    exit: str = ""

    @property
    def solved(self) -> bool:
        return self.result in SOLVED_RESULTS


class CsvRows(dict[str, RunResult]):
    """Rows with the hash of the exact bytes parsed, preserving the mapping API."""

    def __init__(self, raw_dataset_hash: str):
        super().__init__()
        self.raw_dataset_hash = raw_dataset_hash


@dataclass(frozen=True)
class Summary:
    series: str
    solved: int
    total: int
    par2: float
    solved_time: float
    non_solved: Counter[str]
    real: int
    unreal: int
    par2_mean: float
    cactus_endpoint: int
    raw_dataset_hash: str


def _split_assignment(value: str, option: str) -> tuple[str, str]:
    if "=" not in value:
        raise ValueError(f"{option} expects LABEL=VALUE, got {value!r}")
    label, assigned = (part.strip() for part in value.split("=", 1))
    if not label or not assigned:
        raise ValueError(f"{option} expects non-empty LABEL=VALUE, got {value!r}")
    return label, assigned


def load_csv(path: pathlib.Path) -> dict[str, RunResult]:
    """Load one run-subset.py CSV, rejecting malformed or duplicate rows."""
    data = path.read_bytes()
    rows = CsvRows(hashlib.sha256(data).hexdigest())
    with io.StringIO(data.decode("utf-8"), newline="") as handle:
        reader = csv.DictReader(handle)
        required = {"instance", "result", "seconds", "exit"}
        missing_columns = required - set(reader.fieldnames or ())
        if missing_columns:
            columns = ", ".join(sorted(missing_columns))
            raise ValueError(f"{path}: missing CSV column(s): {columns}")

        for line_no, row in enumerate(reader, start=2):
            instance = (row.get("instance") or "").strip()
            if not instance:
                raise ValueError(f"{path}:{line_no}: missing instance")
            if instance in rows:
                raise ValueError(f"{path}:{line_no}: duplicate instance {instance!r}")

            result = (row.get("result") or "").strip().upper()
            if result not in KNOWN_RESULTS:
                raise ValueError(
                    f"{path}:{line_no} ({instance}): unknown result {result!r}"
                )
            seconds_text = (row.get("seconds") or "").strip()
            try:
                seconds = float(seconds_text)
            except ValueError as exc:
                raise ValueError(
                    f"{path}:{line_no} ({instance}): invalid seconds {seconds_text!r}"
                ) from exc
            if not math.isfinite(seconds) or seconds < 0:
                raise ValueError(
                    f"{path}:{line_no} ({instance}): seconds must be finite and "
                    f"non-negative, got {seconds_text!r}"
                )
            rows[instance] = RunResult(result, seconds, row.get("exit") or "")

    if not rows:
        raise ValueError(f"{path}: no benchmark rows")
    return rows


def validate_instance_sets(
    series: Mapping[str, Mapping[str, RunResult]],
) -> None:
    """Require every input series to contain exactly the same instances."""
    items = list(series.items())
    if not items:
        raise ValueError("at least one --csv series is required")
    reference_label, reference_rows = items[0]
    reference = set(reference_rows)
    differences = []
    for label, rows in items[1:]:
        instances = set(rows)
        missing = sorted(reference - instances)
        extra = sorted(instances - reference)
        if missing or extra:
            details = []
            if missing:
                details.append(f"missing: {', '.join(missing)}")
            if extra:
                details.append(f"extra: {', '.join(extra)}")
            differences.append(
                f"{label!r} differs from {reference_label!r} ({'; '.join(details)})"
            )
    if differences:
        raise ValueError("instance sets differ: " + "; ".join(differences))


def make_virtual_best(
    members: Sequence[Mapping[str, RunResult]],
) -> dict[str, RunResult]:
    """Return the fastest solved member per instance for a portfolio."""
    if not members:
        raise ValueError("a virtual-best series needs at least one member")
    instances = members[0].keys()
    portfolio = {}
    for instance in instances:
        solved = [member[instance] for member in members if member[instance].solved]
        portfolio[instance] = (
            min(solved, key=lambda result: result.seconds)
            if solved
            else RunResult("UNSOLVED", 0.0)
        )
    return portfolio


def cactus_seconds(rows: Mapping[str, RunResult]) -> list[float]:
    """The plotted curve: sorted per-instance wall times of solved rows."""
    return sorted(result.seconds for result in rows.values() if result.solved)


def validate_summary(summary: Summary) -> None:
    """Keep verdict counts, scoring, and the plotted endpoint in agreement."""
    if summary.solved != summary.cactus_endpoint:
        raise ValueError(f"{summary.series}: solved count differs from cactus curve endpoint")
    if summary.real + summary.unreal != summary.solved:
        raise ValueError(f"{summary.series}: REAL + UNREAL differs from solved count")
    if summary.total <= 0 or summary.solved + sum(summary.non_solved.values()) != summary.total:
        raise ValueError(f"{summary.series}: outcome counts differ from dataset size")
    if not math.isfinite(summary.par2) or not math.isclose(
        summary.par2_mean * summary.total, summary.par2, rel_tol=1e-12, abs_tol=1e-9,
    ):
        raise ValueError(f"{summary.series}: PAR-2 mean * N differs from total")


def summarize(
    label: str, rows: Mapping[str, RunResult], timeout: float
) -> Summary:
    """Compute solved count, solved time, non-answer counts, and PAR-2."""
    solved_rows = [result for result in rows.values() if result.solved]
    solved_time = sum(result.seconds for result in solved_rows)
    non_solved = Counter(
        result.result for result in rows.values() if not result.solved
    )
    par2 = par2_score(solved_time, sum(non_solved.values()), timeout)
    summary = Summary(
        label,
        len(solved_rows),
        len(rows),
        par2,
        solved_time,
        non_solved,
        sum(result.result == "REALIZABLE" for result in solved_rows),
        sum(result.result == "UNREALIZABLE" for result in solved_rows),
        par2 / len(rows) if rows else 0.0,
        len(cactus_seconds(rows)),
        getattr(rows, "raw_dataset_hash", ""),
    )
    validate_summary(summary)
    return summary


def _markdown_cell(value: str) -> str:
    return value.replace("\n", " ").replace("|", "\\|")


def render_markdown(summaries: Sequence[Summary]) -> str:
    """Render summaries as a PAR-2-ranked Markdown table."""
    for summary in summaries:
        validate_summary(summary)
    ranked = sorted(summaries, key=lambda summary: (summary.par2, summary.series))
    best_solved = max(summary.solved for summary in ranked)
    best_par2 = min(summary.par2 for summary in ranked)
    extra_outcomes = sorted(
        {
            outcome
            for summary in ranked
            for outcome in summary.non_solved
            if outcome not in NON_SOLVED_RESULTS
        }
    )
    outcomes = [*NON_SOLVED_RESULTS, *extra_outcomes]
    headers = [
        "series",
        "solved",
        "of",
        "PAR-2 (s)",
        "total time on solved (s)",
        *outcomes,
        "REAL",
        "UNREAL",
        "PAR-2 mean (s)",
        "Raw dataset hash",
    ]
    alignments = ["---", "---:", "---:", "---:", "---:"] + [
        "---:" for _ in outcomes
    ] + ["---:", "---:", "---:", "---"]
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join(alignments) + "|",
    ]
    for summary in ranked:
        solved = str(summary.solved)
        if summary.solved == best_solved:
            solved = f"**{solved}**"
        par2 = f"{summary.par2:.3f}"
        if summary.par2 == best_par2:
            par2 = f"**{par2}**"
        cells = [
            _markdown_cell(summary.series),
            solved,
            str(summary.total),
            par2,
            f"{summary.solved_time:.3f}",
            *(str(summary.non_solved[outcome]) for outcome in outcomes),
            str(summary.real),
            str(summary.unreal),
            f"{summary.par2_mean:.3f}",
            summary.raw_dataset_hash or "derived",
        ]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def write_markdown(path: pathlib.Path, summaries: Sequence[Summary]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(render_markdown(summaries))


def write_cactus_plot(
    series: Mapping[str, Mapping[str, RunResult]],
    title: str,
    out_prefix: pathlib.Path,
    summaries: Sequence[Summary] | None = None,
) -> tuple[pathlib.Path, pathlib.Path]:
    """Write PNG and PDF cactus plots and return their paths."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axis = plt.subplots(figsize=(8, 5))
    colormap = matplotlib.colormaps["viridis"]
    denominator = max(len(series) - 1, 1)
    for index, (label, rows) in enumerate(series.items()):
        seconds = cactus_seconds(rows)
        if summaries is not None:
            summary = next(summary for summary in summaries if summary.series == label)
            validate_summary(summary)
            if len(seconds) != summary.solved:
                raise ValueError(f"{label}: solved count differs from cactus curve endpoint")
        axis.plot(
            range(1, len(seconds) + 1),
            seconds,
            color=colormap(index / denominator),
            label=label,
            linestyle=LINESTYLES[index % len(LINESTYLES)],
            marker=MARKERS[index % len(MARKERS)],
            markevery=max(1, len(seconds) // 25),
            markersize=4,
            linewidth=1.8,
        )

    axis.set_yscale("log", nonpositive="clip")
    axis.set_xlabel("Number of instances solved")
    axis.set_ylabel("Wall-clock seconds")
    axis.set_title(title)
    axis.grid(True, which="both", linestyle=":", linewidth=0.7, alpha=0.65)
    axis.legend()
    figure.tight_layout()

    out_prefix.parent.mkdir(parents=True, exist_ok=True)
    png = pathlib.Path(f"{out_prefix}.png")
    pdf = pathlib.Path(f"{out_prefix}.pdf")
    figure.savefig(png, dpi=180, bbox_inches="tight")
    figure.savefig(pdf, bbox_inches="tight")
    plt.close(figure)
    return png, pdf


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--csv",
        action="append",
        required=True,
        metavar="LABEL=PATH",
        help="run-subset.py CSV and its plot label (repeatable)",
    )
    parser.add_argument("--title", required=True, help="cactus plot title")
    parser.add_argument(
        "--timeout",
        type=float,
        default=17.0,
        help="per-instance timeout used for PAR-2 (default: 17)",
    )
    parser.add_argument(
        "--out-prefix",
        type=pathlib.Path,
        metavar="PATH",
        help="write PATH.png and PATH.pdf",
    )
    parser.add_argument(
        "--markdown",
        type=pathlib.Path,
        metavar="PATH",
        help="write the PAR-2 Markdown table to PATH",
    )
    parser.add_argument(
        "--virtual-best",
        action="append",
        default=[],
        metavar="LABEL=L1,L2,...",
        help="add a per-instance virtual-best portfolio (repeatable)",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    dispatched = dispatch_mode(MODES, argv)
    if dispatched is not None:
        return dispatched
    parser = build_parser()
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and greater than zero")
    if args.out_prefix is None and args.markdown is None:
        parser.error("at least one of --out-prefix or --markdown is required")

    try:
        series: dict[str, dict[str, RunResult]] = {}
        for value in args.csv:
            label, path_text = _split_assignment(value, "--csv")
            if label in series:
                raise ValueError(f"duplicate series label {label!r}")
            series[label] = load_csv(pathlib.Path(path_text))
        validate_instance_sets(series)

        for value in args.virtual_best:
            label, member_text = _split_assignment(value, "--virtual-best")
            if label in series:
                raise ValueError(f"duplicate series label {label!r}")
            member_labels = [member.strip() for member in member_text.split(",")]
            if not member_labels or any(not member for member in member_labels):
                raise ValueError(
                    f"--virtual-best expects LABEL=L1,L2,..., got {value!r}"
                )
            unknown = [member for member in member_labels if member not in series]
            if unknown:
                raise ValueError(
                    f"virtual-best {label!r} names unknown series: "
                    f"{', '.join(unknown)}"
                )
            series[label] = make_virtual_best(
                [series[member] for member in member_labels]
            )

        summaries = [
            summarize(label, rows, args.timeout) for label, rows in series.items()
        ]
        if args.markdown is not None:
            write_markdown(args.markdown, summaries)
        if args.out_prefix is not None:
            try:
                png, pdf = write_cactus_plot(series, args.title, args.out_prefix, summaries)
            except ImportError as exc:
                missing_module = exc.name or "matplotlib"
                raise ValueError(
                    f"plotting requires the missing module {missing_module!r}; "
                    "the Markdown table can still be produced by omitting "
                    "--out-prefix"
                ) from exc
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    if args.markdown is not None:
        print(f"wrote {args.markdown}")
    if args.out_prefix is not None:
        print(f"wrote {png}")
        print(f"wrote {pdf}")
    return 0


# benchmarking/tools/threeway-join.py: retained policy and helpers.
threeway_ROOT = pathlib.Path(__file__).resolve().parents[1]


sys.path.insert(0, str(threeway_ROOT / "benchmarking"))


def threeway__load(name, path):
    from benchlib import load_tool
    relative = pathlib.Path(path).resolve().relative_to(pathlib.Path(__file__).resolve().parents[1])
    return load_tool(relative.as_posix())


threeway_coverage = threeway__load("syntcomp26_coverage", threeway_ROOT / "benchmarking/run-syntcomp26-coverage.py")


threeway_cactus = threeway__load("cactus_report", threeway_ROOT / "benchmarking/cactus-report.py")


threeway_campaign = threeway_coverage


threeway_SOLVED = frozenset({"REALIZABLE", "UNREALIZABLE"})


threeway_CACTUS_RESULT = {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}


threeway_SUBTYPES = ("TIMEOUT", "MEMOUT", "UNKNOWN", "ERROR", "CRASH", "SYFCO-FAIL", "AUDIT-ERROR")


threeway_UNTIMED_CONVERSION_TOOLS = frozenset({"ltlsynt", "acacia1x"})


threeway_IDENTITY_FIELDS = ("solver_label", "acacia_sha", "binary_sha256", "preset", "flags",
                   "memory_max", "memory_swap_max")


class threeway_JoinError(Exception):
    """Inputs that cannot be joined honestly."""


def threeway_is_signal_crash(exit_code: str) -> bool:
    """run-subset stores run.returncode, possibly via systemd-run's 128+signal encoding."""
    code = int(exit_code)
    return code < 0 or code >= 128


def threeway_audit_solved_verdicts(rows: dict[str, dict[str, str]],
                          others: dict[str, dict[str, dict[str, str]]],
                          expected: dict[str, str], cap: float) -> list[dict[str, str]]:
    """Flag a legacy series' solved verdicts that cannot be trusted, in place.

    Acacia 1.x predates the fix for signal-killed workers being read as
    REALIZABLE, so its exit convention is not trusted blindly.  A solved row
    is flagged when it contradicts another series' solved verdict, the
    expected status, or its own exit code; a flagged row is rescored as ERROR
    charged at the cap.  Rows are cactus dicts (result, seconds, exit).
    """
    audit = []
    for name, row in rows.items():
        verdict = row["result"]
        if verdict not in threeway_SOLVED:
            continue
        conflicting = [tool for tool, other in others.items()
                       if other[name]["result"] in threeway_SOLVED and other[name]["result"] != verdict]
        expected_conflict = expected.get(name) not in (None, verdict)
        exit_code = str(row["exit"]).strip()
        unexpected_exit = exit_code != ("0" if verdict == "REALIZABLE" else "1")
        # run-subset.py writes run.returncode. Direct subprocess signals
        # are negative; systemd-run may encode them as 128 + signal.
        signal_crash = threeway_is_signal_crash(exit_code)
        if conflicting or expected_conflict or unexpected_exit:
            audit.append({"instance": name, "verdict": verdict, "exit": exit_code,
                          "contradicts_tools": ",".join(conflicting),
                          "expected_status": expected.get(name, ""),
                          "contradicts_expected": str(expected_conflict).lower(),
                          "unexpected_exit": str(unexpected_exit).lower(),
                          "signal_crash": str(signal_crash).lower()})
            # A suspect verdict must not earn solved credit in the report.
            row["result"] = "ERROR"
            row["seconds"] = str(cap)
    return audit


def threeway_split_assignment(value, option):
    label, sep, path = value.partition("=")
    if not sep or not label.strip() or not path.strip():
        raise threeway_JoinError(f"{option} expects LABEL=PATH, got {value!r}")
    return label.strip(), pathlib.Path(path.strip())


def threeway_load_series(label, path, cap, instances):
    rows = threeway_coverage.load_output(path, allow_missing_memory=True)
    keyed = {}
    for row in rows:
        if row["instance"] in keyed:
            raise threeway_JoinError(f"{label}: duplicate instance {row['instance']}")
        try:
            seconds = float(row["seconds"])
        except ValueError:
            raise threeway_JoinError(f"{label}: invalid seconds for {row['instance']}") from None
        if not math.isfinite(seconds) or seconds < 0 or \
                (row["result"] in threeway_SOLVED and seconds > cap):
            raise threeway_JoinError(f"{label}: invalid measured time for {row['instance']}")
        keyed[row["instance"]] = row
    if set(keyed) != set(instances):
        missing = sorted(set(instances) - set(keyed))
        extra = sorted(set(keyed) - set(instances))
        raise threeway_JoinError(f"{label}: instance set differs from the list; "
                        f"missing {len(missing)} {missing[:5]}, extra {len(extra)} {extra[:5]}")
    caps = {row["cap_s"] for row in rows}
    if caps != {str(cap)}:
        raise threeway_JoinError(f"{label}: rows are at cap(s) {sorted(caps)}, not {cap}")
    identity = {}
    for field in threeway_IDENTITY_FIELDS:
        values = {row[field] for row in rows}
        if len(values) != 1:
            raise threeway_JoinError(f"{label}: mixed {field} {sorted(values)}")
        identity[field] = values.pop()
    derived = "provenance" in rows[0]
    if derived:
        source_rows = {}
        for name, row in keyed.items():
            source_cap = int(row["source_cap_s"])
            provenance = row["provenance"]
            valid = source_cap > 0 and (
                provenance == f"measured-{cap}s" and source_cap == cap or
                provenance == f"recycled-{source_cap}s" and source_cap < cap)
            if not valid:
                raise threeway_JoinError(f"{label}: invalid derived provenance for {name}")
            source = (path.parent / row["source_run"]).resolve()
            if source not in source_rows:
                original = threeway_coverage.load_output(source, allow_missing_memory=True)
                if any("provenance" in item for item in original):
                    raise threeway_JoinError(f"{label}: source run is itself derived: {source}")
                keyed_source = {}
                for item in original:
                    if item["instance"] in keyed_source:
                        raise threeway_JoinError(f"{label}: duplicate source instance {item['instance']}")
                    keyed_source[item["instance"]] = item
                source_rows[source] = (threeway_sha256_file(source), keyed_source)
            source_sha, originals = source_rows[source]
            if source_sha != row["source_run_sha256"]:
                raise threeway_JoinError(f"{label}: source run SHA-256 differs for {name}")
            original = originals.get(name)
            if original is None:
                raise threeway_JoinError(f"{label}: {name} absent from source run {source}")
            if original["cap_s"] != str(source_cap):
                raise threeway_JoinError(f"{label}: {name} source cap differs from derived provenance")
            for field in threeway_coverage.OUTPUT_COLUMNS + threeway_coverage.ROUTE_COLUMNS + threeway_coverage.MEMORY_COLUMNS:
                if field == "cap_s" and provenance.startswith("recycled-"):
                    continue
                if row.get(field) != original.get(field):
                    raise threeway_JoinError(f"{label}: {name} derived {field} differs from source run")
    sources = sorted({(row["provenance"], row["source_run"], row["source_run_sha256"])
                      for row in rows}) if derived else []
    return {"label": label, "path": str(path), "sha256": threeway_sha256_file(path),
            "rows": keyed, "identity": identity, "derived": derived, "sources": sources}


def threeway_sha256_file(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def threeway_corpus_digest(instances, mapping, corpus):
    digest = hashlib.sha256()
    for name in instances:
        if name not in mapping:
            raise threeway_JoinError(f"{name}: absent from the TLSF map")
        path = (corpus / mapping[name]).resolve()
        if not path.is_file() or not path.is_relative_to(corpus.resolve()):
            raise threeway_JoinError(f"{name}: missing TLSF in corpus: {path}")
        digest.update(json.dumps([name, str(path.relative_to(corpus.resolve())),
                                  threeway_sha256_file(path)], separators=(",", ":")).encode() + b"\n")
    return digest.hexdigest()


def threeway_row_source(row):
    cap = row.get("source_cap_s") or row.get("cap_s", "")
    return {"source_cap_s": cap,
            "source_run": row.get("source_run") or row.get("input_path", ""),
            "provenance": row.get("provenance") or f"measured-{cap}s"}


def threeway_expected_verdicts(instances, tlsf_map_path, corpus, exceptions_path):
    mapping = threeway_coverage.read_tlsf_map(tlsf_map_path)
    exceptions = threeway_coverage.read_status_exceptions(exceptions_path, corpus, required=True)
    expected, source = {}, {}
    for name in instances:
        if name not in mapping:
            raise threeway_JoinError(f"{name}: absent from the TLSF map")
        tlsf = mapping[name]
        if tlsf in exceptions:
            verdict, origin = exceptions[tlsf], "exception"
        else:
            verdict, origin = threeway_coverage.expected_verdict(corpus / tlsf), "status"
        if verdict is not None:
            expected[name] = verdict
            source[name] = origin
    return expected, source


def threeway_cactus_rows(series, cap):
    """Scoring view: cactus vocabulary, with the exit code for the audit."""
    view = {}
    for name, row in series["rows"].items():
        result = threeway_CACTUS_RESULT.get(row["result"], row["result"])
        seconds = row["seconds"] if result in threeway_SOLVED else str(cap)
        view[name] = {"result": result, "seconds": seconds, "exit": row["exit_code"],
                      "subtype": row["result"] if result not in threeway_SOLVED else "",
                      **threeway_row_source({**row, "input_path": series["path"]})}
    return view


def threeway_score(label, view, cap):
    runs = {name: threeway_cactus.RunResult(row["result"], float(row["seconds"]), row["exit"])
            for name, row in view.items()}
    return threeway_cactus.summarize(label, runs, cap)


def threeway_peak_memory(series):
    rows = list(series["rows"].values())
    scope = memory_statistic(rows, "scope_memory_peak_bytes")
    rss = memory_statistic(rows, "max_process_rss_bytes")
    return {"max_mib": round(scope["max_bytes"] / 2**20, 1) if scope["max_bytes"] else None,
            "median_mib": round(scope["median_bytes"] / 2**20, 1) if scope["median_bytes"] else None,
            "rows_with_peak": scope["numerator"],
            "rows_without_peak": scope["denominator"] - scope["numerator"],
            "scope": scope, "process_rss": rss}


def threeway_conversion_boundary(label, untimed, manifests):
    tools = {(item["series"].get("kind"), item["series"].get("tool"))
             for item in manifests
             if item.get("verification", "").startswith("campaign specification")}
    converted = {(kind, tool) for kind, tool in tools
                 if kind == "legacy" and tool in threeway_UNTIMED_CONVERSION_TOOLS}
    if label in untimed:
        if tools and any(kind in {"arm", "race"} or kind == "legacy" and
                         tool == "acacia" for kind, tool in tools):
            raise threeway_JoinError(f"{label}: --untimed-conversion conflicts with manifest runner")
        return ("SyFCo conversion declared outside the timed run (unverified); "
                "SYFCO-FAIL scored as unsolved")
    if tools and converted == tools:
        return "SyFCo conversion outside the timed run; SYFCO-FAIL scored as unsolved"
    if tools and all(kind in {"arm", "race"} or kind == "legacy" and tool == "acacia"
                     for kind, tool in tools):
        return "TLSF parsed inside the timed run (native frontend)"
    return "timing boundary unverified; supply a campaign manifest or --untimed-conversion"


def threeway_match_manifests(label, series, attached, cap, panel):
    """Tie manifest panels to the observed rows and their actual source files."""
    if not attached:
        return
    observed = series["identity"]
    for item in attached:
        frozen = item["series"]
        if frozen.get("binary_sha256") != observed["binary_sha256"] or \
                frozen.get("source_revision") != observed["acacia_sha"]:
            raise threeway_JoinError(f"{label}: frozen manifest binary or revision differs "
                            "from the observation")
        if item["verification"].startswith("campaign specification"):
            native = frozen["kind"] in {"arm", "race"}
            expected_flags = threeway_campaign.plan_native_flags(frozen) if native else frozen["flags"]
            expected_preset = frozen["preset"] if native else frozen["tool"]
            if observed["flags"] != expected_flags or observed["preset"] != expected_preset or \
                    observed["memory_max"] != "8G" or observed["memory_swap_max"] != "0":
                raise threeway_JoinError(f"{label}: observation treatment differs from manifest")
    if not series["derived"]:
        for item in attached:
            if item["selected"] != panel:
                raise threeway_JoinError(f"{label}: manifest selected panel differs from observed rows")
            if item["cap_s"] != cap:
                raise threeway_JoinError(f"{label}: manifest cap differs from observation")
            if pathlib.Path(series["path"]).resolve() != \
                    (item["path"].parent / f"{item['key']}-{cap}s.tsv").resolve():
                raise threeway_JoinError(f"{label}: observation path differs from campaign manifest run")
        return
    covered = set()
    measured = {name for name, row in series["rows"].items()
                if row["provenance"] == f"measured-{cap}s"}
    recycled = {name for name, row in series["rows"].items()
                if row["provenance"] == "recycled-17s" and row["source_cap_s"] == "17"}
    if measured | recycled != panel or measured & recycled:
        raise threeway_JoinError(f"{label}: invalid derived row provenance or source cap")
    for item in attached:
        source_cap = item["cap_s"]
        if source_cap not in {17, cap}:
            raise threeway_JoinError(f"{label}: derived source manifest has wrong or missing cap")
        if source_cap == cap and cap == 17:
            raise threeway_JoinError(f"{label}: derived series needs a longer cap than its source")
        expected = measured if source_cap == cap else recycled
        source = (item["path"].parent / f"{item['key']}-{source_cap}s.tsv").resolve()
        source_rows = threeway_coverage.load_output(source, allow_missing_memory=True)
        source_names = [row["instance"] for row in source_rows]
        if len(source_names) != len(set(source_names)):
            raise threeway_JoinError(f"{label}: duplicate instance in source run {source}")
        if set(source_names) != item["selected"]:
            missing = sorted(item["selected"] - set(source_names))
            extra = sorted(set(source_names) - item["selected"])
            raise threeway_JoinError(f"{label}: source run panel differs from manifest: "
                            f"missing {missing[:5]}, extra {extra[:5]}")
        if any(row["cap_s"] != str(source_cap) for row in source_rows):
            raise threeway_JoinError(f"{label}: source run cap differs from manifest")
        linked = set()
        for name in expected:
            row = series["rows"][name]
            threeway_row_source = (pathlib.Path(series["path"]).parent / row["source_run"]).resolve()
            if threeway_row_source == source:
                if row["source_run_sha256"] != threeway_sha256_file(source):
                    raise threeway_JoinError(f"{label}: source run SHA-256 differs from manifest source")
                linked.add(name)
        if source_cap == cap:
            if item["selected"] != linked:
                raise threeway_JoinError(f"{label}: rerun manifest selected panel differs from "
                                "measured-60s rows")
        elif item["selected"] != panel or linked != panel - measured:
            raise threeway_JoinError(f"{label}: recycled rows differ from 17 s panel complement")
        if covered & linked:
            raise threeway_JoinError(f"{label}: overlapping source manifests")
        covered.update(linked)
    if covered != panel:
        raise threeway_JoinError(f"{label}: derived rows lack matching source manifests: "
                        f"{sorted(panel - covered)}")


def threeway_join(args):
    if type(args.cap) is not int or args.cap <= 0:
        raise threeway_JoinError("--cap must be a positive integer")
    instances = threeway_coverage.read_instance_list(args.list)
    if not instances or len(set(instances)) != len(instances):
        raise threeway_JoinError(f"{args.list}: empty list or duplicate instances")
    mapping = threeway_coverage.read_tlsf_map(args.tlsf_map)
    corpus_sha = threeway_corpus_digest(instances, mapping, args.tlsf_corpus)
    if args.noise_floor is not None:
        if not math.isfinite(args.noise_floor) or args.noise_floor < 0:
            raise threeway_JoinError("--noise-floor must be finite and nonnegative")
        source = args.noise_floor_source or ""
        if f"cap={args.cap}" not in source.split() or f"corpus={corpus_sha}" not in source.split():
            raise threeway_JoinError("--noise-floor-source must state matching cap=<seconds> "
                            "and corpus=<SHA-256>")
    public, internal = {}, {}
    for option, target in (("--series", public), ("--internal", internal)):
        for value in getattr(args, option.strip("-")) or []:
            label, path = threeway_split_assignment(value, option)
            if label in public or label in internal:
                raise threeway_JoinError(f"duplicate series label {label!r}")
            target[label] = threeway_load_series(label, path, args.cap, instances)
    if len(public) < 2:
        raise threeway_JoinError("at least two --series are required")
    everything = {**public, **internal}
    if args.new not in public:
        raise threeway_JoinError(f"--new {args.new!r} must name a --series")
    for label in args.legacy_audit or []:
        if label not in everything:
            raise threeway_JoinError(f"--legacy-audit {label!r} names no series")
    for label in args.untimed_conversion or []:
        if label not in everything:
            raise threeway_JoinError(f"--untimed-conversion {label!r} names no series")
    expected, expected_source = threeway_expected_verdicts(
        instances, args.tlsf_map, args.tlsf_corpus, args.status_exceptions)
    views = {label: threeway_cactus_rows(series, args.cap) for label, series in everything.items()}

    audits = {}
    for label in args.legacy_audit or []:
        others = {other: views[other] for other in everything if other != label
                  and other not in (args.legacy_audit or [])}
        audits[label] = threeway_audit_solved_verdicts(views[label], others, expected,
                                                          args.cap)
        for entry in audits[label]:
            views[label][entry["instance"]]["subtype"] = "AUDIT-ERROR"

    conflicts = []
    trusted = [label for label in everything if label not in (args.legacy_audit or [])]
    for name in instances:
        verdicts = {label: views[label][name]["result"] for label in trusted
                    if views[label][name]["result"] in threeway_SOLVED}
        for label, verdict in verdicts.items():
            if name in expected and verdict != expected[name]:
                conflicts.append({"instance": name, "series": label, "verdict": verdict,
                                  "against": f"expected ({expected_source[name]})",
                                  "other_verdict": expected[name]})
        if len(set(verdicts.values())) > 1:
            conflicts.append({"instance": name, "series": ",".join(sorted(verdicts)),
                              "verdict": ";".join(f"{k}={v}" for k, v in sorted(verdicts.items())),
                              "against": "each other", "other_verdict": ""})

    def solved(label, name):
        return views[label][name]["result"] in threeway_SOLVED

    report = {"cap_s": args.cap, "instances": len(instances), "new": args.new,
              "list": str(args.list), "list_sha256": threeway_sha256_file(args.list),
              "corpus_sha256": corpus_sha,
              "status_exceptions_sha256": threeway_sha256_file(args.status_exceptions),
              "noise_floor_s": args.noise_floor, "noise_floor_source": args.noise_floor_source,
              "verification": ("input, corpus and attached source bytes are checked; "
                               "solver outcomes, source revision, status correction authority "
                               "and any declared noise floor remain unverified"),
              "series": {}, "paired": {}, "conflicts": conflicts, "audits": audits}
    manifests = {}
    for path in args.manifest or []:
        record = json.loads(path.read_text())
        manifest_cap = record.get("cap_s")
        if type(manifest_cap) is not int or manifest_cap <= 0:
            raise threeway_JoinError(f"{path}: manifest needs a valid positive cap_s")
        if record.get("comparison"):
            try:
                selected_path = record["inputs"]["list"]["path"]
                rebuilt, _ = threeway_campaign.plan_read_comparison(
                    pathlib.Path(record["comparison"]), manifest_cap,
                    selected=list(record["series"]), subset_list=selected_path)
            except (threeway_campaign.plan_CampaignError, OSError, KeyError, TypeError,
                    ValueError) as error:
                raise threeway_JoinError(f"{path}: cannot verify campaign inputs: {error}") from error
            if rebuilt != record:
                raise threeway_JoinError(f"{path}: campaign manifest differs from current input content")
            if pathlib.Path(record["frozen_list"]).resolve() != args.list.resolve() or \
                    pathlib.Path(record["corpus"]).resolve() != args.tlsf_corpus.resolve() or \
                    record["inputs"]["tlsf_map"]["sha256"] != threeway_sha256_file(args.tlsf_map) or \
                    record["inputs"]["status_exceptions"]["sha256"] != \
                    report["status_exceptions_sha256"]:
                raise threeway_JoinError(f"{path}: join inputs differ from campaign manifest inputs")
            verification = "campaign specification and input bytes verified"
        else:
            verification = ("partial: no campaign specification; binary bytes, revision, "
                            "runner, runtime and converted inputs unverified")
        if record.get("frozen_list_sha256") != report["list_sha256"]:
            raise threeway_JoinError(f"{path}: frozen list SHA-256 differs from join observations")
        inputs = record.get("inputs")
        listed = inputs.get("list", {}) if isinstance(inputs, dict) else {}
        if not isinstance(listed, dict) or not listed.get("path") or \
                threeway_sha256_file(listed["path"]) != listed.get("sha256"):
            raise threeway_JoinError(f"{path}: selected list SHA-256 differs from manifest")
        selected = threeway_coverage.read_instance_list(pathlib.Path(listed["path"]))
        if len(selected) != len(set(selected)) or not set(selected) <= set(instances) or \
                record.get("corpus_sha256") != \
                threeway_corpus_digest(selected, mapping, args.tlsf_corpus):
            raise threeway_JoinError(f"{path}: corpus SHA-256 or selected list differs from observations")
        for key, value in record.get("series", {}).items():
            identity = {"manifest": str(path), "manifest_sha256": threeway_sha256_file(path),
                        "series": value, "list_sha256": record.get("frozen_list_sha256"),
                        "corpus_sha256": record.get("corpus_sha256"),
                        "verification": verification}
            manifests.setdefault(key, []).append({**identity, "path": path, "key": key,
                                                   "selected": set(selected),
                                                   "cap_s": manifest_cap})
    for label, series in everything.items():
        summary = threeway_score(label, views[label], args.cap)
        subtypes = {subtype: sum(row["subtype"] == subtype for row in views[label].values())
                    for subtype in threeway_SUBTYPES}
        group = public if label in public else internal
        exclusive = sum(solved(label, name) and not any(
            solved(other, name) for other in group if other != label) for name in instances)
        exclusive_all = sum(solved(label, name) and not any(
            solved(other, name) for other in everything if other != label) for name in instances)
        recycled = sum(row.get("provenance", "").startswith("recycled")
                       for row in series["rows"].values())
        entry = {
            "role": "public" if label in public else "internal",
            "solved": summary.solved, "real": summary.real, "unreal": summary.unreal,
            "par2_total_s": round(summary.par2, 6), "par2_mean_s": round(summary.par2_mean, 6),
            "solved_time_s": round(summary.solved_time, 6),
            "failure_subtypes": subtypes,
            "exclusive_solves_in_group": exclusive,
            "exclusive_solves_all_series": exclusive_all,
            "peak_memory": threeway_peak_memory(series),
            "derived": series["derived"], "recycled_rows": recycled,
            "measured_rows": len(instances) - recycled, "sources": series["sources"],
            "input": series["path"], "input_sha256": series["sha256"],
            "identity": series["identity"],
            "observation_verification": (
                "TSV bytes and cited source rows verified; solver outcome, source revision, "
                "and recycling eligibility or validation require independent evidence"
                if series["derived"] else
                "TSV bytes verified; solver outcome and source revision require independent evidence"),
        }
        manifest_key = series["identity"]["solver_label"]
        relevant = []
        if manifest_key in manifests:
            relevant = [item for item in manifests[manifest_key]
                        if series["derived"] or item["cap_s"] == args.cap]
            if relevant:
                threeway_match_manifests(label, series, relevant, args.cap, set(instances))
                for item in relevant:
                    if item["verification"].startswith("campaign specification"):
                        try:
                            threeway_campaign.plan_verify_observation_labels(
                                item["series"], series["rows"].values(),
                                tlsf_map=args.tlsf_map, corpus=args.tlsf_corpus,
                                status_exceptions=args.status_exceptions)
                        except threeway_campaign.plan_CampaignError as error:
                            raise threeway_JoinError(f"{label}: {error}") from error
            if relevant:
                entry["manifest_identity"] = [
                    {key: value for key, value in item.items()
                     if key not in {"path", "key", "selected", "cap_s"}}
                    for item in relevant]
        if series["derived"] and not relevant:
            raise threeway_JoinError(f"{label}: derived series needs its 17 s and rerun manifests")
        entry["timing_boundary"] = threeway_conversion_boundary(
            label, args.untimed_conversion or [], relevant)
        report["series"][label] = entry
    new = report["series"][args.new]
    for label in everything:
        if label == args.new:
            continue
        gains = [name for name in instances if solved(args.new, name) and not solved(label, name)]
        losses = [name for name in instances if solved(label, name) and not solved(args.new, name)]
        delta = new["par2_total_s"] - report["series"][label]["par2_total_s"]
        reading = "no noise floor supplied; not interpreted"
        if args.noise_floor is not None:
            reading = ("within the noise floor: not evidence on its own"
                       if abs(delta) <= args.noise_floor else "exceeds the noise floor")
        report["paired"][label] = {"gains": gains, "losses": losses,
                                   "par2_delta_s": round(delta, 6), "noise_reading": reading}
    return report, views


def threeway_render(report, labels_in_order):
    cap = report["cap_s"]

    def tag(label):
        entry = report["series"][label]
        return (f"{label} (derived: {entry['recycled_rows']} rows recycled)"
                if entry["derived"] else label)

    lines = []
    if report["conflicts"]:
        lines += [f"**NOT FINAL: {len(report['conflicts'])} verdict conflict(s) need "
                  "adjudication (see conflicts TSV).**", ""]
    lines += [f"# Closing comparison at {cap} s ({report['instances']} instances)", ""]
    for role, title in (("public", "Three-way"), ("internal", "Internal baselines")):
        labels = [label for label in labels_in_order if report["series"][label]["role"] == role]
        if not labels:
            continue
        lines += [f"## {title}", "",
                  "| Series | Solved | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | "
                  + " | ".join(threeway_SUBTYPES) + " | Exclusive | Peak MiB (max / median) |",
                  "|---|" + "---:|" * (6 + len(threeway_SUBTYPES)) + "---|"]
        for label in labels:
            entry = report["series"][label]
            peak = entry["peak_memory"]
            lines.append(
                f"| {tag(label)} | {entry['solved']} | {entry['real']} | {entry['unreal']} | "
                f"{entry['par2_total_s']:.3f} | {entry['par2_mean_s']:.3f} | "
                + " | ".join(str(entry["failure_subtypes"][s]) for s in threeway_SUBTYPES)
                + f" | {entry['exclusive_solves_in_group']} | "
                f"{peak['max_mib']} / {peak['median_mib']} |")
        lines.append("")
        for label in labels:
            for statistic, values in report["series"][label]["peak_memory"].items():
                if statistic not in {"scope", "process_rss"}:
                    continue
                lines.append(f"{label} {statistic}: {values['numerator']}/{values['denominator']} "
                             f"campaign rows; missing reasons: {values['missing_reasons']}; "
                             f"{values['selection_bias']}. Maximum is of the observed subset.")
        lines.append("")
    lines += [f"## Paired against {report['new']}", "",
              "| Other series | Gains | Losses | PAR-2 delta (s) | Reading |", "|---|---:|---:|---:|---|"]
    for label, pair in report["paired"].items():
        lines.append(f"| {tag(label)} | {len(pair['gains'])} | {len(pair['losses'])} | "
                     f"{pair['par2_delta_s']:+.3f} | {pair['noise_reading']} |")
    floor = report["noise_floor_s"]
    lines += ["", ("Noise floor: " + (f"{floor} s ({report['noise_floor_source']})."
                                      if floor is not None else
                                      "none supplied for this cap; PAR-2 differences are "
                                      "reported, not interpreted.")),
              "", "## Timing boundary and provenance", ""]
    for label in labels_in_order:
        entry = report["series"][label]
        identity = entry["identity"]
        lines.append(f"- **{tag(label)}**: {entry['timing_boundary']}. Binary SHA-256 "
                     f"`{identity['binary_sha256']}`, revision `{identity['acacia_sha']}`, "
                     f"preset `{identity['preset']}`, flags `{identity['flags']}`, "
                     f"MemoryMax {identity['memory_max']}, swap {identity['memory_swap_max']}. "
                     f"Input `{entry['input']}` ({entry['input_sha256']}).")
        lines.append(f"  Verification: {entry['observation_verification']}.")
        if "manifest_identity" in entry:
            lines.append(f"  Campaign manifest: `{json.dumps(entry['manifest_identity'], sort_keys=True)}`.")
        for provenance, source, digest in entry["sources"]:
            lines.append(f"  - {provenance}: `{source}` ({digest})")
    for label, audit in report["audits"].items():
        lines.append(f"- {label}: {len(audit)} solved verdict(s) failed the legacy audit and "
                     "are scored as ERROR at the cap (AUDIT-ERROR).")
    lines.append("- Status exceptions were applied in join scoring and passed by the "
                 "orchestrator to the native runner for conflict collection.")
    lines.append(f"- Verification limits: {report['verification']}.")
    return "\n".join(lines) + "\n"


def threeway_write_outputs(report, views, out, labels_in_order):
    out.mkdir(parents=True, exist_ok=True)
    cap = report["cap_s"]
    stem = out / f"threeway-{cap}s"
    stem.with_suffix(".json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    stem.with_suffix(".md").write_text(threeway_render(report, labels_in_order))
    with open(f"{stem}-conflicts.tsv", "w", newline="") as stream:
        writer = csv.DictWriter(stream, ["instance", "series", "verdict", "against",
                                         "other_verdict"], delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(report["conflicts"])
    with open(f"{stem}-gains-losses.tsv", "w", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(["other_series", "kind", "instance", "new_result", "new_seconds",
                         "new_source_cap_s", "new_source_run", "new_provenance",
                         "other_result", "other_seconds", "other_source_cap_s",
                         "other_source_run", "other_provenance"])
        for label, pair in report["paired"].items():
            for kind in ("gains", "losses"):
                for name in pair[kind]:
                    new, other = views[report["new"]][name], views[label][name]
                    new_source = threeway_row_source(new)
                    other_source = threeway_row_source(other)
                    writer.writerow([label, kind, name, new["result"], new["seconds"],
                                     *new_source.values(), other["result"], other["seconds"],
                                     *other_source.values()])
    for label, audit in report["audits"].items():
        with open(f"{stem}-audit-{threeway_slug(label)}.tsv", "w", newline="") as stream:
            columns = ["instance", "verdict", "exit", "contradicts_tools", "expected_status",
                       "contradicts_expected", "unexpected_exit", "signal_crash"]
            writer = csv.DictWriter(stream, columns, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(audit)
    for label in labels_in_order:
        # Scored cactus CSVs, for cactus-report.py plots; audit rescoring included.
        with open(out / f"{threeway_slug(label)}-{cap}s-scored.csv", "w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["instance", "result", "seconds", "exit", "source_cap_s",
                             "source_run", "provenance"])
            for name, row in views[label].items():
                writer.writerow([name, row["result"], row["seconds"], row["exit"],
                                 *threeway_row_source(row).values()])
    return stem


def threeway_slug(label):
    return "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in label)


def threeway_build_parser():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cap", required=True, type=int)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--series", action="append", metavar="LABEL=TSV")
    parser.add_argument("--internal", action="append", metavar="LABEL=TSV")
    parser.add_argument("--new", required=True)
    parser.add_argument("--legacy-audit", action="append", metavar="LABEL")
    parser.add_argument("--untimed-conversion", action="append", metavar="LABEL",
                        help="declare a series timed on pre-converted inputs when no "
                             "campaign manifest supplies the runner identity")
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-corpus", required=True, type=pathlib.Path)
    parser.add_argument("--status-exceptions", required=True, type=pathlib.Path)
    parser.add_argument("--noise-floor", type=float,
                        help="same-configuration PAR-2 spread for this corpus and cap (s)")
    parser.add_argument("--noise-floor-source", default="",
                        help="where the floor was measured; required with --noise-floor")
    parser.add_argument("--manifest", action="append", type=pathlib.Path,
                        help="comparison manifest, for binary identities "
                             "behind wrappers")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    return parser


def threeway_main(argv=None):
    parser = threeway_build_parser()
    args = parser.parse_args(argv)
    if args.noise_floor is not None and not args.noise_floor_source:
        parser.error("--noise-floor needs --noise-floor-source")
    try:
        report, views = threeway_join(args)
    except (threeway_JoinError, threeway_coverage.CoverageError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    labels = list(report["series"])
    stem = threeway_write_outputs(report, views, args.out, labels)
    print(stem.with_suffix(".md").read_text(), end="")
    print(f"wrote {stem}.md, {stem}.json and sidecars")
    if report["conflicts"]:
        print(f"NOT FINAL: {len(report['conflicts'])} verdict conflict(s)", file=sys.stderr)
        return 3
    return 0


# benchmarking/classify-coverage.py: retained policy and helpers.
def classification_load_tool(name, path):
    from benchlib import load_tool
    relative = pathlib.Path(path).resolve().relative_to(pathlib.Path(__file__).resolve().parents[1])
    return load_tool(relative.as_posix())


classification_HERE = Path(__file__).resolve().parent


classification_join = classification_load_tool("classification_join", classification_HERE / "tools/threeway-join.py")


classification_write_tsv = classification_join.coverage.atomic_write_tsv


classification_SOLVED = classification_join.SOLVED


classification_UNKNOWN = "UNKNOWN"


def classification_metadata(instances, mapping, manifest, conversion, corpus):
    """Use declared origins; never call the direct-file family-name heuristic."""
    result = {}
    for name in instances:
        tlsf = mapping[name]
        source, converted = manifest[tlsf], conversion[tlsf]
        path = (corpus / tlsf).resolve()
        if not path.is_relative_to(corpus.resolve()):
            raise ValueError(f"source escapes corpus: {tlsf}")
        if classification_join.sha256_file(path) != source["sha256"] or \
                converted["source_sha256"] != source["sha256"]:
            raise ValueError(f"source digest differs: {name}")
        text = path.read_text()
        # PARAMETERS in expanded files may be gone; the param origin retains
        # their exact declared assignments. Direct files are inspected as text.
        clean = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', "", text, flags=re.S)
        if source["origin"].startswith("param:"):
            parsed = parse_origin(source["origin"], name)
            params = parsed["parameter_values"]
            declared = bool(params)
        elif source["origin"].startswith("direct:"):
            block = re.search(r"\bPARAMETERS\s*\{([^{}]*)\}", clean, re.S)
            params = dict(re.findall(r"([A-Za-z_]\w*)\s*=\s*([^;]+);", block[1])) \
                if block else {}
            declared = bool(block and block[1].strip())
        else:
            raise ValueError(f"unsupported manifest origin: {source['origin']}")
        result[name] = {
            "instance": name, "tlsf_file": tlsf, "origin": source["origin"],
            "source_sha256": source["sha256"], "declared_parameters": str(declared).lower(),
            "parameters_json": json.dumps(params, sort_keys=True),
            "parameter_dimension": len(params), "tlsf_bytes": path.stat().st_size,
            "inputs": int(converted["inputs"]), "outputs": int(converted["outputs"]),
            "semantics": converted["semantics"], "effective_target": converted["effective_target"],
        }
    return result


def classification_observed_polarity(rows):
    verdicts = {row["result"] for row in rows if row["result"] in classification_SOLVED}
    if len(verdicts) > 1:
        raise ValueError("opposing observed verdicts")
    return {"REALIZABLE": "REAL", "UNREALIZABLE": "UNREAL"}.get(
        next(iter(verdicts), ""), classification_UNKNOWN)


def classification_classify(instances, meta, acacia, comparator, cap, near_fraction=0.8):
    """Partition the entire manifest, retaining individual row provenance."""
    if set(acacia) != set(instances) or set(comparator) != set(instances):
        raise ValueError("row set differs from logical instance list")
    out = []
    for name in sorted(instances):
        a, b = acacia[name], comparator[name]
        for row in (a, b):
            # Converted-pair rows put the exact logical alias in tlsf_file.
            # Accept only that map key or its explicitly mapped TLSF value.
            if row["tlsf_file"] not in {name, meta[name]["tlsf_file"]}:
                raise ValueError(f"source-map mismatch: {name}")
            seconds = float(row["seconds"])
            if not math.isfinite(seconds) or seconds < 0 or \
                    (row["result"] in classification_SOLVED and seconds > cap):
                raise ValueError(f"invalid wall time: {name}")
            if row["result"] in classification_SOLVED and "exit_code" in row and row["exit_code"] != \
                    ("0" if row["result"] == "REALIZABLE" else "1"):
                raise ValueError(f"solved verdict/exit mismatch: {name}")
        pair = (a["result"] in classification_SOLVED, b["result"] in classification_SOLVED)
        kind = {(False, True): "ltlsynt-only", (True, False): "Acacia-only",
                (False, False): "both-unsolved", (True, True): "common-solved"}[pair]
        near = pair == (True, True) and max(float(a["seconds"]), float(b["seconds"])) \
            >= cap * near_fraction
        row = {**meta[name], "cap_s": cap, "set": kind,
               "near_cap": str(near).lower(), "polarity": classification_observed_polarity((a, b))}
        for label, observation in (("acacia", a), ("comparator", b)):
            row.update({f"{label}_{key}": observation.get(key, "") for key in (
                "result", "seconds", "binary_sha256", "acacia_sha", "source_run",
                "source_run_sha256", "source_cap_s", "provenance")})
            row[f"{label}_provenance"] = observation.get("provenance", f"measured-{cap}s")
        out.append(row)
    return out


def classification_structural_sample(rows, size, seed):
    """Round-robin log2(size)/log2(AP)/parameter strata, seeded within strata."""
    rng = random.Random(seed)
    strata = {}
    for row in sorted(rows, key=lambda item: item["instance"]):
        key = (int(row["tlsf_bytes"]).bit_length() - 1,
               (int(row["inputs"]) + int(row["outputs"])).bit_length() - 1,
               int(row["parameter_dimension"]))
        strata.setdefault(key, []).append(row)
    for group in strata.values():
        rng.shuffle(group)
    keys = sorted(strata)
    rng.shuffle(keys)
    chosen = []
    while keys and len(chosen) < size:
        for key in keys[:]:
            chosen.append({**strata[key].pop(), "sample_stratum": json.dumps(key),
                           "sample_seed": seed, "sample_rank": len(chosen) + 1})
            if not strata[key]:
                keys.remove(key)
            if len(chosen) == size:
                break
    return chosen


def classification_stop_category(reason):
    """Map explicit recorded stop reasons, with an UNKNOWN fallback."""
    if reason in {"mp-class", "semantics", "exact_reduction"}:
        return "exact-reduction rejection"
    if reason.startswith("budget-") or reason in {"allocation", "aag-size", "publish-size",
                                                       "metadata-size"}:
        return "reduction resources"
    if reason in {"seed_window", "seed_polarity", "parameters"}:
        return "seed window"
    if reason.startswith(("schema", "bus_schema", "typed_alignment", "rank_shape")):
        return "schema fitting"
    if reason in {"policy_reconstruct", "mode_relation"}:
        return "U policy reconstruction"
    if reason in {"seed_check", "internal_check", "check", "checking"}:
        return "checking"
    if reason == "translation-acceptance-set-limit":
        return "translation size"
    if reason in {"translation-exception", "translation-time", "translation-size"}:
        return reason.replace("-", " ")
    return classification_UNKNOWN


def classification_phase_attribution(events):
    """Preserve first recorded decline and last completed stage; no counter guesses.

    Combined-arm summaries are emitted after the call, so their order is not
    a timeline. A decline in them is a route stop, not a portfolio failure.
    """
    visible = [e for e in events if e.get("phase") != "record_summary"]
    stops = []
    for event in visible:
        phase = event.get("phase", "")
        if phase == "budget_decline":
            reason = event.get("stage", "")
        else:
            reason = next((phase[len(prefix):] for prefix in
                           ("reduce_decline_", "lift_decline_", "decline_")
                           if phase.startswith(prefix)), "")
        if reason:
            stops.append((reason, classification_stop_category(reason)))
    route = next((e["phase"].removeprefix("route_") for e in visible
                  if e.get("phase", "").startswith("route_")), classification_UNKNOWN)
    return {"first_stop_reason": stops[0][0] if stops else classification_UNKNOWN,
            "first_stop_category": stops[0][1] if stops else classification_UNKNOWN,
            "last_observed_stage": (visible[-1].get("phase") or visible[-1].get("stage") or
                                    visible[-1].get("event", classification_UNKNOWN)) if visible else classification_UNKNOWN,
            "last_plan_stage": phase_plan_stage(
                visible[-1].get("stage") or visible[-1].get("phase", ""),
                visible[-1].get("route", "")) if visible else "unknown",
            "recorded_route": route,
            "all_stop_reasons": json.dumps([reason for reason, _ in stops])}


def classification_table_rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def classification_requested_arms(flags):
    tokens = shlex.split(flags)
    for index, token in enumerate(tokens):
        if token == "--arms" and index + 1 < len(tokens):
            return tokens[index + 1].split(",")
        if token.startswith("--arms="):
            return token.split("=", 1)[1].split(",")
    return []


def classification_evidence_path(root, value):
    path = (root / value).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"evidence path escapes root: {value}")
    return path


def classification_arm_attribution(records, arm, standalone=False):
    """An anonymous legacy producer is identifiable only in an explicit solo run."""
    producer = arm
    if standalone and not records.arm_processes(arm) and records.arm_processes("legacy"):
        producer = "legacy"
    processes = records.arm_processes(producer)
    events = [e for process in processes for e in process.events if e.get("arm") == producer]
    # Parent lifecycle annotations have no arm label; join only the emitting
    # physical worker, never a portfolio index or another worker in the PID file.
    workers = [group for group in records.worker_events().values()
               if any(e.get("arm") == producer for e in group)]
    if workers:
        events = [e for group in workers for e in group
                  if not e.get("arm") or e.get("arm") == producer]
    unambiguous = len(workers) == 1 if workers else len(processes) == 1
    # File/PID order across producers cannot establish a first or last stage.
    detail = classification_phase_attribution(events if unambiguous else [])
    terminal = [e for e in events if e.get("phase") == "budget_decline" or
                str(e.get("phase", "")).startswith(("reduce_decline_", "lift_decline_"))]
    stop = classification_phase_attribution(terminal if unambiguous else [])
    detail.update(first_terminal_obstruction=stop["first_stop_category"],
                  terminal_reason=stop["first_stop_reason"], processes=len(processes),
                  records_complete=bool(processes) and all(p.finished for p in processes)
                  and records.dropped == 0,
                  record_paths=json.dumps([str(records.directory / p.name) for p in processes]),
                  phase_sequence=json.dumps([e.get("phase") for e in events]),
                  producer_arm=producer if processes else classification_UNKNOWN)
    return detail


def classification_load_evidence(manifest_path, mapping, selected, verdicts):
    """Read explicit row/phase roots; campaign labels are provenance data only.

    Invocation results do not override recorded per-arm obstructions.
    Manifest columns: campaign, root, rows, phase_root, arm, regime, comparability.
    Optional row_identity_map binds runner aliases; selection=all retains a full probe panel.
    Paths below root are archive-relative. No file discovery selects observations.
    """
    observations, hashes = [], {str(manifest_path): classification_join.sha256_file(manifest_path)}
    for entry in classification_table_rows(manifest_path):
        root = Path(entry["root"])
        if not root.is_absolute():
            root = manifest_path.parent / root
        path = classification_evidence_path(root, entry["rows"])
        hashes[str(path)] = classification_join.sha256_file(path)
        aliases = {}
        if entry.get("row_identity_map"):
            alias_path = Path(entry["row_identity_map"])
            if not alias_path.is_absolute():
                alias_path = manifest_path.parent / alias_path
            hashes[str(alias_path)] = classification_join.sha256_file(alias_path)
            aliases = read_tsv(alias_path, "row_instance")
        seen = set()
        for row in classification_table_rows(path):
            raw_name = row["instance"]
            alias = aliases.get(raw_name)
            if aliases and (not alias or alias["row_tlsf"] != row["tlsf_file"]):
                raise ValueError(f"auxiliary alias/source mismatch: {raw_name}")
            name = alias["instance"] if alias else raw_name
            identity = (name, row["cap_s"], row["run_index"])
            if identity in seen:
                raise ValueError(f"duplicate evidence row: {identity}")
            seen.add(identity)
            tlsf = alias["tlsf_file"] if alias else row["tlsf_file"]
            if name not in mapping or tlsf not in {name, mapping[name]}:
                raise ValueError(f"auxiliary source identity mismatch: {name}")
            cap, seconds = int(row["cap_s"]), float(row["seconds"])
            if cap <= 0 or not math.isfinite(seconds) or seconds < 0 or \
                    (row["result"] in classification_SOLVED and seconds > cap):
                raise ValueError(f"invalid evidence wall time: {name}")
            if row["result"] in classification_SOLVED and row["exit_code"] != \
                    ("0" if row["result"] == "REALIZABLE" else "1"):
                raise ValueError(f"evidence solved verdict/exit mismatch: {name}")
            classification_observed_polarity([*verdicts.get(name, []), row])
            verdicts.setdefault(name, []).append({"result": row["result"]})
            if name not in selected and entry.get("selection") != "all":
                continue
            arms = classification_requested_arms(row.get("flags", ""))
            if not arms and entry.get("arm"):
                arms = [entry["arm"]]
            if not arms:
                raise ValueError("evidence needs explicit requested arms")
            solo = len(arms) == 1
            records = load_phase_records(classification_evidence_path(root, entry["phase_root"]) /
                phase_record_dir(Path(), row["solver_label"], cap, name, row["run_index"])) \
                if entry.get("phase_root") else PhaseRecords(root, ())
            snapshots, snapshot_paths = [], []
            if entry.get("worker_root"):
                directory = classification_evidence_path(root, entry["worker_root"]) / \
                    row["solver_label"] / str(cap) / raw_name
                if not directory.resolve().is_relative_to(root.resolve()):
                    raise ValueError("worker records escape evidence root")
                for snapshot_path in sorted(directory.glob("*.json")):
                    snapshot = json.loads(snapshot_path.read_text())
                    if snapshot.get("instance") != raw_name:
                        raise ValueError("worker snapshot identity mismatch")
                    snapshots.append(snapshot)
                    snapshot_paths.append(str(snapshot_path))
                    hashes[str(snapshot_path)] = classification_join.sha256_file(snapshot_path)
            for arm in arms:
                detail = classification_arm_attribution(records, arm, standalone=solo)
                matching = [snapshot for snapshot in snapshots if ":".join(
                    snapshot.get(k, "") for k in
                    ("polarity", "transform", "requested_backend")) == arm]
                if detail["last_observed_stage"] == classification_UNKNOWN and len(matching) == 1:
                    detail["last_observed_stage"] = matching[0].get("stage", classification_UNKNOWN)
                for record in json.loads(detail["record_paths"]):
                    hashes[record] = classification_join.sha256_file(Path(record))
                observations.append({"campaign": entry["campaign"], "instance": name,
                    "context": "standalone" if solo else "race", "arm": arm,
                    "evidence_cap_s": cap, "evidence_view": "measured archival",
                    "result": row["result"], "seconds": seconds,
                    **{key: row.get(key, "") for key in (
                        "binary_sha256", "acacia_sha", "preset", "flags", "run_index",
                        "memory_max", "memory_swap_max", "allowed_cpus", "cpu_quota")},
                    "regime": entry["regime"], "comparability": entry["comparability"],
                    "row_path": str(path), "row_instance": raw_name,
                    "resource_reason": row.get("resource_reason", ""),
                    "worker_snapshot_paths": json.dumps(snapshot_paths), **detail})
    return observations, hashes


def classification_load_probes(manifest_path, mapping):
    """Bind translation-only diagnostics to captured worker formula bytes.

    Each manifest row explicitly links instance, worker_record, formula, diagnostic,
    arm, root, campaign, and regime. Snapshot stages never imply a terminal stop.
    """
    observations, hashes = [], {str(manifest_path): classification_join.sha256_file(manifest_path)}
    for entry in classification_table_rows(manifest_path):
        root = Path(entry["root"])
        if not root.is_absolute():
            root = manifest_path.parent / root
        name = entry["instance"]
        if name not in mapping:
            raise ValueError(f"unknown probe identity: {name}")
        paths = {key: classification_evidence_path(root, entry[key]) for key in
                 ("worker_record", "formula", "diagnostic")}
        hashes.update({str(path): classification_join.sha256_file(path) for path in paths.values()})
        worker = json.loads(paths["worker_record"].read_text())
        formula = paths["formula"].read_text().removesuffix("\n")
        arm = ":".join(worker.get(k, "") for k in
                       ("polarity", "transform", "requested_backend"))
        if worker.get("instance") != name or worker.get("worker_formula") != formula or \
                arm != entry["arm"]:
            raise ValueError(f"probe worker/formula binding mismatch: {name}")
        text = paths["diagnostic"].read_text()
        error = re.search(r"^translation_error=(.*)$", text, re.M)
        reason = ("translation-acceptance-set-limit" if error and
                  error[1].startswith("Too many acceptance sets used.") else
                  "translation-exception" if error else classification_UNKNOWN)
        # The probe's parsed/simplified counts establish those stages only.
        counts = re.search(r"simplified promise_occurrences=(\d+) unique_promises=(\d+)", text)
        observations.append({"campaign": entry["campaign"], "instance": name,
            "arm": arm, "context": "standalone translation probe", "regime": entry["regime"],
            "first_terminal_obstruction": classification_stop_category(reason), "terminal_reason": reason,
            "first_stop_category": classification_stop_category(reason), "first_stop_reason": reason,
            "last_observed_stage": "translation exception" if reason != classification_UNKNOWN else
                ("formula simplified" if counts else classification_UNKNOWN),
            "unique_promises": counts[2] if counts else classification_UNKNOWN,
            "translation_error": error[1] if error else classification_UNKNOWN,
            "worker_snapshot_stage": worker.get("stage", classification_UNKNOWN),
            **{key: str(path) for key, path in paths.items()},
            "limitation": "translation-only historical probe; no weakening attempt telemetry"})
    return observations, hashes


def classification_standalone_boundary(missing, observations):
    """Historical within-cap successes are candidates, never matched race causation."""
    result = []
    for row in missing:
        solo = [e for e in observations if e["instance"] == row["instance"] and
                e["context"] == "standalone"]
        successes = [e for e in solo if e["result"] in classification_SOLVED and
                     float(e["seconds"]) <= int(row["cap_s"]) and
                     int(e["evidence_cap_s"]) >= int(row["cap_s"])]
        failures = [e for e in solo if e["result"] not in classification_SOLVED]
        result.append({"cap_s": row["cap_s"], "instance": row["instance"],
            "standalone_observations": len(solo), "standalone_nonsolves": len(failures),
            "within_cap_historical_solves": len(successes),
            "standalone_solving_arms": json.dumps(sorted({e["arm"] for e in successes})),
            "standalone_solve_sources": json.dumps([{k: e[k] for k in
                ("campaign", "arm", "row_path", "binary_sha256", "seconds", "regime")}
                for e in successes]),
            "pattern": "historical standalone solve / current race miss" if successes else
                ("standalone nonsolve observed" if failures else classification_UNKNOWN),
            "race_only_interference_proven": classification_UNKNOWN,
            "limitation": "unmatched binaries/runtime/routes; higher-cap rows are archival"})
    return result


def classification_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("list", "tlsf-map", "manifest", "conversion", "corpus", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--series", action="append", required=True, metavar="CAP:ROLE=TSV")
    parser.add_argument("--evidence-manifest", type=Path)
    parser.add_argument("--probe-manifest", type=Path)
    parser.add_argument("--near-fraction", type=float, default=0.8)
    parser.add_argument("--sample-size", type=int, default=24)
    parser.add_argument("--seed", type=int, default=207)
    args = parser.parse_args(argv)
    if not 0 < args.near_fraction <= 1 or args.sample_size < 0:
        parser.error("near fraction must be in (0,1]; sample size must be nonnegative")
    instances = classification_join.coverage.read_instance_list(args.list)
    if not instances or len(instances) != len(set(instances)):
        raise ValueError("empty list or duplicate logical identities")
    mapping = classification_join.coverage.read_tlsf_map(args.tlsf_map)
    meta = classification_metadata(instances, mapping, read_tsv(args.manifest, "instance"),
                    read_tsv(args.conversion, "instance"), args.corpus)
    series, inputs = {}, {}
    for value in args.series:
        label, path = classification_join.split_assignment(value, "--series")
        cap_text, role = label.split(":")
        cap = int(cap_text)
        if cap <= 0 or role not in {"acacia", "comparator"} or (cap, role) in series:
            raise ValueError(f"invalid or duplicate series: {label}")
        loaded = classification_join.load_series(label, path, cap, instances)
        series[cap, role] = loaded["rows"]
        inputs[label] = {k: v for k, v in loaded.items() if k != "rows"}
    # Opposite verdicts across caps are also a correctness conflict.
    for name in instances:
        classification_observed_polarity([rows[name] for rows in series.values()])
    args.out.mkdir(parents=True, exist_ok=True)
    evidence_hashes = {}
    observations, probes = [], []
    verdicts = {name: [rows[name] for rows in series.values()] for name in instances}
    selected = {name for (cap, role), rows in series.items() if role == "acacia"
                for name in instances if rows[name]["result"] not in classification_SOLVED and
                series[cap, "comparator"][name]["result"] in classification_SOLVED}
    if args.evidence_manifest:
        observations, hashes = classification_load_evidence(args.evidence_manifest, mapping, selected, verdicts)
        evidence_hashes.update(hashes)
        if observations:
            classification_write_tsv(args.out / "missing-evidence-observations.tsv",
                      list(observations[0]), observations)
    if args.probe_manifest:
        probes, hashes = classification_load_probes(args.probe_manifest, mapping)
        evidence_hashes.update(hashes)
        if probes:
            classification_write_tsv(args.out / "translation-probe-observations.tsv", list(probes[0]), probes)
    boundary = []
    summary = []
    report = ["# Coverage classification", "",
              f"Near cap: either solved time >= {args.near_fraction:g} × cap.", "",
              "| Cap (s) | Acacia solved | Comparator solved | Comparator-only | "
              "Acacia-only | Both-unsolved | Common near cap |",
              "|---:|---:|---:|---:|---:|---:|---:|"]
    for cap in sorted({cap for cap, _ in series}):
        rows = classification_classify(instances, meta, series[cap, "acacia"], series[cap, "comparator"],
                        cap, args.near_fraction)
        view = "derived" if any(inputs[f"{cap}:{role}"]["derived"]
                                    for role in ("acacia", "comparator")) else "measured"
        for row in rows:
            row["view"] = view
        counts = Counter(row["set"] for row in rows)
        report.append(f"| {cap} | {counts['common-solved'] + counts['Acacia-only']} | "
                      f"{counts['common-solved'] + counts['ltlsynt-only']} | "
                      f"{counts['ltlsynt-only']} | {counts['Acacia-only']} | "
                      f"{counts['both-unsolved']} | "
                      f"{sum(row['near_cap'] == 'true' for row in rows)} |")
        classification_write_tsv(args.out / f"classification-{cap}s.tsv", list(rows[0]), rows)
        for kind in ("ltlsynt-only", "Acacia-only", "both-unsolved", "common-solved"):
            selected = [row for row in rows if row["set"] == kind and
                        (kind != "common-solved" or row["near_cap"] == "true")]
            filename = kind if kind != "common-solved" else "common-solved-near-cap"
            classification_write_tsv(args.out / f"{filename}-{cap}s.tsv", list(rows[0]), selected)
        missing = [row for row in rows if row["set"] == "ltlsynt-only"]
        boundary.extend(classification_standalone_boundary(missing, observations))
        splits = Counter((row["polarity"], row["declared_parameters"]) for row in missing)
        for polarity in ("REAL", "UNREAL", classification_UNKNOWN):
            for declared in ("false", "true"):
                summary.append({"cap_s": cap, "polarity": polarity,
                                "declared_parameters": declared,
                                "missing": splits[polarity, declared]})
        sample = classification_structural_sample([row for row in rows if row["set"] == "both-unsolved"],
                                   args.sample_size, args.seed)
        classification_write_tsv(args.out / f"both-unsolved-sample-{cap}s.tsv",
                  [*rows[0], "sample_stratum", "sample_seed", "sample_rank"], sample)
    if boundary:
        classification_write_tsv(args.out / "standalone-race-boundary.tsv", list(boundary[0]), boundary)
    classification_write_tsv(args.out / "missing-split.tsv", list(summary[0]), summary)
    provenance = {"series": inputs, "near_fraction": args.near_fraction,
                  "sample_size": args.sample_size, "seed": args.seed,
                  "selection": ("round-robin floor(log2(bytes)), floor(log2(AP)), "
                                "parameter dimension"),
                  "evidence_inputs": evidence_hashes,
                  "arguments": argv if argv is not None else sys.argv[1:], "inputs": {str(path): classification_join.sha256_file(path) for path in
                      (args.list, args.tlsf_map, args.manifest, args.conversion)}}
    (args.out / "classification-provenance.json").write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n")
    views = "; ".join(f"{cap} s " + ("derived" if any(inputs[f"{cap}:{role}"]["derived"]
                      for role in ("acacia", "comparator")) else "measured")
                      for cap in sorted({cap for cap, _ in series}))
    report.extend(["", f"Views: {views}.", "", "Polarity uses conclusive observations at each cap only. "
                   "Source hashes, source-map aliases and declared origins are checked. "
                   "Derived source hashes/rows are checked by the existing closing join.", "",
                   f"Structural sample: seed {args.seed}, up to {args.sample_size} per cap; "
                   "round-robin log2(bytes)/log2(AP)/parameter-dimension strata. "
                   "Logical IDs order ties before seeded shuffling; verdicts do not select cases.",
                   "", "Missing polarity/parameter counts: [TSV](missing-split.tsv). "
                   "Provenance: [JSON](classification-provenance.json).", ""])
    (args.out / "coverage-report.md").write_text("\n".join(report))
    return 0


MODES = {
    'classify': classification_main,
    'threeway': threeway_main,
}

if __name__ == "__main__":
    raise SystemExit(main())
