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

import argparse
import csv
import math
import pathlib
from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass

SOLVED_RESULTS = frozenset(("REALIZABLE", "UNREALIZABLE"))
NON_SOLVED_RESULTS = (
    "TIMEOUT",
    "RESOURCE_LIMIT",
    "UNKNOWN",
    "ERROR",
    "SYFCO-FAIL",
)
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


@dataclass(frozen=True)
class Summary:
    series: str
    solved: int
    total: int
    par2: float
    solved_time: float
    non_solved: Counter[str]


def _split_assignment(value: str, option: str) -> tuple[str, str]:
    if "=" not in value:
        raise ValueError(f"{option} expects LABEL=VALUE, got {value!r}")
    label, assigned = (part.strip() for part in value.split("=", 1))
    if not label or not assigned:
        raise ValueError(f"{option} expects non-empty LABEL=VALUE, got {value!r}")
    return label, assigned


def load_csv(path: pathlib.Path) -> dict[str, RunResult]:
    """Load one run-subset.py CSV, rejecting malformed or duplicate rows."""
    rows: dict[str, RunResult] = {}
    with path.open(newline="") as handle:
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


def summarize(
    label: str, rows: Mapping[str, RunResult], timeout: float
) -> Summary:
    """Compute solved count, solved time, non-answer counts, and PAR-2."""
    solved_rows = [result for result in rows.values() if result.solved]
    solved_time = sum(result.seconds for result in solved_rows)
    non_solved = Counter(
        result.result for result in rows.values() if not result.solved
    )
    par2 = solved_time + 2 * timeout * sum(non_solved.values())
    return Summary(
        label,
        len(solved_rows),
        len(rows),
        par2,
        solved_time,
        non_solved,
    )


def _markdown_cell(value: str) -> str:
    return value.replace("\n", " ").replace("|", "\\|")


def render_markdown(summaries: Sequence[Summary]) -> str:
    """Render summaries as a PAR-2-ranked Markdown table."""
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
    ]
    alignments = ["---", "---:", "---:", "---:", "---:"] + [
        "---:" for _ in outcomes
    ]
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
) -> tuple[pathlib.Path, pathlib.Path]:
    """Write PNG and PDF cactus plots and return their paths."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axis = plt.subplots(figsize=(8, 5))
    colormap = matplotlib.colormaps["viridis"]
    denominator = max(len(series) - 1, 1)
    for index, (label, rows) in enumerate(series.items()):
        seconds = sorted(result.seconds for result in rows.values() if result.solved)
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
                png, pdf = write_cactus_plot(series, args.title, args.out_prefix)
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


if __name__ == "__main__":
    raise SystemExit(main())
