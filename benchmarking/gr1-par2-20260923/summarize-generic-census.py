#!/usr/bin/env python3
"""Summarize slice reducibility and separately measured admission sets."""

from __future__ import annotations

import argparse
import collections
import csv
import pathlib


def percentile(values: list[float], fraction: float) -> float:
    values = sorted(values)
    position = (len(values) - 1) * fraction
    low = int(position)
    high = min(len(values) - 1, low + 1)
    return values[low] + (values[high] - values[low]) * (position - low)


def main() -> None:
    here = pathlib.Path(__file__).resolve().parent / "campaign"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=pathlib.Path, default=here / "generic-census.tsv")
    parser.add_argument("--admission", type=pathlib.Path,
                        default=here / "generic-admission.tsv")
    parser.add_argument("--output", type=pathlib.Path,
                        default=here / "generic-census-summary.md")
    args = parser.parse_args()
    with args.input.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 1524 or len({row["id"] for row in rows}) != 1524:
        raise ValueError("census must contain exactly 1,524 distinct inputs")
    successes = [float(row["elapsed_s"]) for row in rows if row["reducible"] == "yes"]
    all_times = [float(row["elapsed_s"]) for row in rows]
    reasons = collections.Counter(row["reason"] for row in rows if row["reducible"] == "no")
    bins = (0.1, 0.5, 1.0, 2.0, 5.0, 11.333334, float("inf"))
    counts = collections.Counter(next(bound for bound in bins if time <= bound)
                                 for time in successes)
    lines = ["# Generic exact GR(1) reduction census", "",
             "Actual TLSF inputs, no parameter overrides. One serial reduction attempt per "
             "input, bounded at 11.333333 s (2/3 of the 17 s campaign cap). "
             "This measures **reducibility under the lift slice**, not actual "
             "Stage A admission.", "",
             f"- Inputs: **{len(rows):,}**", f"- Reducible: **{len(successes):,}**",
             f"- Declined: **{len(rows) - len(successes):,}**", "",
             "| Reduction time | Reducible inputs |", "|---|---:|"]
    previous = 0.0
    for bound in bins:
        label = (f"({previous:g}, {bound:g}] s" if bound != float("inf") else
                 f"> {previous:g} s")
        lines.append(f"| {label} | {counts[bound]} |")
        previous = bound
    lines.extend(["", "| Population | min | median | P90 | P99 | max |",
                  "|---|---:|---:|---:|---:|---:|"])
    for name, values in (("Reducible", successes), ("All attempts", all_times)):
        formatted = [min(values), percentile(values, 0.5), percentile(values, 0.9),
                     percentile(values, 0.99), max(values)]
        lines.append(f"| {name} | " + " | ".join(f"{value:.3f} s" for value in formatted) + " |")
    lines.extend(["", "Decline reasons:", ""])
    for reason, count in reasons.most_common():
        lines.append(f"- `{reason}`: {count:,}")
    if args.admission.is_file():
        with args.admission.open(newline="", encoding="utf-8") as stream:
            admission_rows = list(csv.DictReader(stream, delimiter="\t"))
        census_ids = {row["id"] for row in rows}
        if (len(admission_rows) != len(rows) or
                {row["id"] for row in admission_rows} != census_ids):
            raise ValueError("admission set must have the same 1,524 distinct inputs")
        lines.extend(["", "## Actual admission under the unchanged wrapper gate", "",
                      "The wrapper caps eligibility at `min(1 s, 5% of cap)`. "
                      "We reran exact reduction for every input at each gate, "
                      "serially. The default lift slice is longer than either gate. "
                      "Admission means exact reduction finished within this gate; "
                      "it does not imply a later solver/checker verdict.", "",
                      "| Outer cap | Eligibility gate | Admitted | Declined |",
                      "|---:|---:|---:|---:|"])
        admitted_sets: dict[int, set[str]] = {}
        for cap in (17, 60):
            field = f"admitted_{cap}"
            ids = sorted(row["id"] for row in admission_rows if row[field] == "yes")
            admitted_sets[cap] = set(ids)
            if any(row[field] not in {"yes", "no"} for row in admission_rows):
                raise ValueError(f"invalid admission flag: {cap}")
            list_path = here / f"generic-admission-{cap}.list"
            list_path.write_text("\n".join(ids) + "\n", encoding="utf-8")
            lines.append(f"| {cap} s | {min(1.0, 0.05 * cap):.2f} s | "
                         f"{len(ids):,} | {len(rows) - len(ids):,} |")
        lines.extend(["", f"Relative to the 572 slice-reducible inputs, "
                      f"{len(successes) - len(admitted_sets[17])} miss the 17 s gate and "
                      f"{len(successes) - len(admitted_sets[60])} miss the 60 s gate. "
                      f"The 60 s set adds "
                      f"{len(admitted_sets[60] - admitted_sets[17])} inputs; "
                      f"the 17 s set has "
                      f"{len(admitted_sets[17] - admitted_sets[60])} unique inputs.", "",
                      "Exact observed sets: `generic-admission-17.list` and "
                      "`generic-admission-60.list`; per-input status, elapsed time, "
                      "and decline reason: `generic-admission.tsv`."])
    lines.extend(["", "Rows: `generic-census.tsv`. The time distribution is observed "
                  "wall time for the reduction attempt, including process cleanup; "
                  "the tool deadline is 11.333333 s, so timeout rows can exceed it "
                  "slightly. Censored timeouts are included in the all-attempts "
                  "distribution.", ""])
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
