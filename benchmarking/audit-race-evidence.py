#!/usr/bin/env python3
"""Recompute portfolio census and race coverage from the saved summaries.

Like arm-census-report.py and select-portfolio-arms.py, count an answer only
when its verdict is decisive and smallest_cap_solved <= --cap. In particular,
decisive_seconds is never a substitute for the staged cap that produced it.

Labels are scoped by source directory, since panel-census and hard-set reuse
arm names over different instance sets. By default audit each source's union,
pairwise intersections and per-instance oracle. Add groups with repeatable
--group NAME=SOURCE/LABEL,SOURCE/LABEL; an unqualified label is accepted only
when unambiguous. Census unions are isolated oracles, not measured races.
PAR-2 is the mean over every row in the label's summary; its sum is also emitted.
"""

from __future__ import annotations

import argparse
import csv
import io
import itertools
import math
import pathlib
import statistics
import sys
from dataclasses import dataclass, field

import benchlib


SOURCES = ("panel-census", "hard-set", "race-curve")
RACE_LABELS = ("race4-shipped", "race5-plus-guarded-real", "race2-best")
COLUMNS = (
    "solver_label", "instance", "smallest_cap_solved", "decisive_result",
    "decisive_seconds", "still_unsolved_at_max_cap", "failure_kind_at_max_cap", "max_cap_s",
)


class EvidenceError(Exception):
    """Evidence is missing, malformed or unsafe to score."""


@dataclass
class Row:
    verdict: str
    solved_at: float | None
    seconds: float | None
    max_cap: float

    def exclusion(self, cap: float) -> str | None:
        if self.verdict not in benchlib.SOLVED:
            return "non_decisive"
        if self.solved_at is None:
            return "missing_cap"
        if self.solved_at > cap:
            return "above_cap"
        return None


@dataclass
class Summary:
    source: str
    label: str
    rows: dict[str, Row] = field(default_factory=dict)
    raw_caps: set[float] | None = None

    @property
    def key(self) -> str:
        return f"{self.source}/{self.label}"

    def answers(self, cap: float) -> dict[str, tuple[str, float]]:
        return {name: (row.verdict, row.seconds) for name, row in self.rows.items()
                if row.exclusion(cap) is None}


def number(raw: str, context: str, *, optional=False) -> float | None:
    if raw == "" and optional:
        return None
    try:
        value = float(raw)
    except ValueError:
        raise EvidenceError(f"{context}: invalid number {raw!r}") from None
    if not math.isfinite(value) or value < 0:
        raise EvidenceError(f"{context}: expected a finite nonnegative number, got {raw!r}")
    return value


def tsv_reader(handle):
    return csv.DictReader((line for line in handle if not line.lstrip().startswith("#")),
                          delimiter="\t")


def load_raw_caps(path: pathlib.Path) -> set[float] | None:
    try:
        handle = path.open(newline="", encoding="utf-8")
    except FileNotFoundError:
        return None
    with handle:
        reader = tsv_reader(handle)
        if not reader.fieldnames or "cap_s" not in reader.fieldnames:
            raise EvidenceError(f"{path}: raw TSV header lacks cap_s")
        caps = set()
        for line, raw in enumerate(reader, 2):
            context = f"{path}:{line}"
            if None in raw or any(value is None for value in raw.values()):
                raise EvidenceError(f"{context}: malformed TSV row")
            caps.add(number(raw["cap_s"], context))
        return caps


def load_summaries(evidence_dir: pathlib.Path) -> dict[str, Summary]:
    summaries = {}
    for path in sorted(evidence_dir.rglob("*-summary.tsv")):
        source = path.parent.relative_to(evidence_dir).as_posix()
        with path.open(newline="", encoding="utf-8") as handle:
            reader = tsv_reader(handle)
            if reader.fieldnames != list(COLUMNS):
                raise EvidenceError(f"{path}: unexpected summary header")
            count = 0
            for line, raw in enumerate(reader, 2):
                context = f"{path}:{line}"
                if None in raw or any(value is None for value in raw.values()):
                    raise EvidenceError(f"{context}: malformed TSV row")
                label, instance = raw["solver_label"], raw["instance"]
                if not label or not instance:
                    raise EvidenceError(f"{context}: empty solver_label or instance")
                key = f"{source}/{label}"
                if key not in summaries:
                    summaries[key] = Summary(
                        source, label, raw_caps=load_raw_caps(path.with_name(f"{label}.tsv")))
                summary = summaries[key]
                if instance in summary.rows:
                    raise EvidenceError(
                        f"{context}: duplicate (label, instance): {key}, {instance}")
                solved_at = number(raw["smallest_cap_solved"], context, optional=True)
                verdict = raw["decisive_result"]
                seconds = (number(raw["decisive_seconds"], context)
                           if verdict in benchlib.SOLVED else None)
                summary.rows[instance] = Row(
                    verdict, solved_at, seconds, number(raw["max_cap_s"], context))
                count += 1
            if not count:
                raise EvidenceError(f"MISSING DATA: {path}: summary contains no rows")
    if not summaries:
        raise EvidenceError(f"MISSING DATA: no *-summary.tsv under {evidence_dir}")
    return summaries


def verdict_conflicts(summaries: dict[str, Summary]) -> list[str]:
    # Scan all decisive verdicts, including those outside the requested cap:
    # cap filtering must not hide a recorded soundness conflict.
    by_instance = {}
    for key, summary in summaries.items():
        for instance, row in summary.rows.items():
            if row.verdict in benchlib.SOLVED:
                by_instance.setdefault(instance, {})[key] = row.verdict
    return [f"VERDICT CONFLICT: {instance}: "
            + ", ".join(f"{key}={verdict}" for key, verdict in sorted(verdicts.items()))
            for instance, verdicts in sorted(by_instance.items())
            if len(set(verdicts.values())) > 1]


def instance_difference(left: str, left_set: set, right: str, right_set: set) -> list[str]:
    difference = left_set ^ right_set
    if not difference:
        return []
    messages = [f"MISSING DATA: {left} vs {right}: symmetric difference ({len(difference)}): "
                + ", ".join(sorted(difference))]
    for name, missing in ((left, right_set - left_set), (right, left_set - right_set)):
        if missing:
            messages.append(f"  missing from {name}: " + ", ".join(sorted(missing)))
    return messages


def source_validation(evidence_dir: pathlib.Path, summaries: dict[str, Summary]) -> list[str]:
    problems = []
    present = {summary.source for summary in summaries.values()}
    for source in SOURCES:
        if source not in present:
            problems.append(f"MISSING DATA: no summaries for {source}")
    for source in sorted(present):
        members = [s for s in summaries.values() if s.source == source]
        reference = members[0]
        for member in members[1:]:
            problems.extend(instance_difference(
                reference.key, set(reference.rows), member.key, set(member.rows)))
        list_path = evidence_dir / source / "instances.list"
        if list_path.exists():
            listed = [line.strip() for line in list_path.read_text(encoding="utf-8").splitlines()
                      if line.strip() and not line.lstrip().startswith("#")]
            if len(listed) != len(set(listed)):
                problems.append(f"MISSING DATA: {list_path}: duplicate instances")
            for member in members:
                problems.extend(instance_difference(
                    str(list_path), set(listed), member.key, set(member.rows)))
        elif source in {"hard-set", "race-curve"}:
            problems.append(f"MISSING DATA: {list_path} is absent")
    for label in RACE_LABELS:
        if f"race-curve/{label}" not in summaries:
            problems.append(f"MISSING DATA: no race-curve summary for {label}")
    return problems


def requested_groups(specs: list[str], summaries: dict[str, Summary]) -> dict[str, list[str]]:
    groups = {source: sorted(key for key, s in summaries.items() if s.source == source)
              for source in sorted({s.source for s in summaries.values()})}
    # Keep the reviewer comparison tied to exactly these three measured races.
    groups["race-curve"] = [f"race-curve/{label}" for label in RACE_LABELS]
    for spec in specs:
        name, separator, labels = spec.partition("=")
        name = name.strip()
        if not separator or not name or name in groups:
            raise EvidenceError(f"invalid or duplicate --group {spec!r}; use NAME=LABEL,LABEL")
        members = []
        for label in labels.split(","):
            label = label.strip()
            matches = [key for key, summary in summaries.items()
                       if label == key or label == summary.label]
            if len(matches) != 1:
                raise EvidenceError(f"group {name}: label {label!r} is absent or ambiguous; "
                                    "use SOURCE/LABEL")
            if matches[0] in members:
                raise EvidenceError(f"group {name}: duplicate label {label!r}")
            members.append(matches[0])
        groups[name] = members
    return groups


def score(answers: dict[str, tuple[str, float]], total: int, cap: float) -> dict:
    times = [seconds for _, seconds in answers.values()]
    real = sum(verdict == "REALIZABLE" for verdict, _ in answers.values())
    par2_sum = benchlib.par2(sum(times), total - len(answers), cap)
    return {"instances": total, "solved": len(answers), "REAL": real,
            "UNREAL": len(answers) - real, "PAR-2": f"{par2_sum / total:.6f}",
            "PAR-2 sum": f"{par2_sum:.6f}",
            "median solved s": f"{statistics.median(times):.6f}" if times
            else "unavailable: no solved instances"}


def tables(summaries: dict[str, Summary], groups: dict[str, list[str]], cap: float) -> list:
    sources, labels, exclusions = [], [], []
    for source in sorted({s.source for s in summaries.values()}):
        members = [s for s in summaries.values() if s.source == source]
        max_caps = {row.max_cap for s in members for row in s.rows.values()}
        solved_caps = {row.solved_at for s in members for row in s.rows.values()
                       if row.solved_at is not None}
        if len(solved_caps) > 1 or any(s.raw_caps and len(s.raw_caps) > 1 for s in members):
            mode = "staged"
        elif all(s.raw_caps is not None and len(s.raw_caps) == 1 for s in members):
            mode = "uniform"
        else:
            mode = ("undetermined (summary only; a staged schedule whose early caps answered "
                    "nothing is indistinguishable)")
        sources.append({
            "source": source, "labels": len(members),
            "mode": mode,
            "max_cap_s": ",".join(f"{c:g}" for c in sorted(max_caps)),
            "smallest caps present": ",".join(f"{c:g}" for c in sorted(solved_caps)),
        })
    answers = {key: summary.answers(cap) for key, summary in summaries.items()}
    for key, summary in sorted(summaries.items()):
        labels.append({"label": key, **score(answers[key], len(summary.rows), cap)})
        reasons = [row.exclusion(cap) for row in summary.rows.values()]
        exclusions.append({"label": key, "excluded total": sum(r is not None for r in reasons),
                           "above cap": reasons.count("above_cap"),
                           "missing cap": reasons.count("missing_cap"),
                           "non-decisive": reasons.count("non_decisive")})

    unions, intersections, oracles = [], [], []
    for group, members in groups.items():
        instances = sorted(summaries[members[0]].rows)
        union = {}
        for instance in instances:
            solved_by = [key for key in members if instance in answers[key]]
            if solved_by:
                fastest = min(solved_by, key=lambda key: answers[key][instance][1])
                verdict, seconds = answers[fastest][instance]
                union[instance] = (verdict, seconds)
            oracles.append({
                "group": group, "instance": instance, "solved": bool(solved_by),
                "verdict": union[instance][0] if solved_by else "UNSOLVED",
                "oracle seconds": f"{union[instance][1]:.6f}" if solved_by else "",
                "solved by": ",".join(solved_by),
            })
        unions.append({"group": group, "labels": ",".join(members),
                       **score(union, len(instances), cap), "oracle solved": len(union)})
        for left, right in itertools.combinations(members, 2):
            intersections.append({"group": group, "left": left, "right": right,
                                  "intersection solved": len(answers[left].keys()
                                                             & answers[right].keys())})

    review = [{"label": label, "instances": len(summaries[f"race-curve/{label}"].rows),
               "solved": len(answers[f"race-curve/{label}"])} for label in RACE_LABELS]
    race_union = next(row for row in unions if row["group"] == "race-curve")
    review.extend({"label": name, "instances": race_union["instances"],
                   "solved": race_union["solved"]} for name in ("union", "oracle"))
    return [
        ("Race review over race-curve/instances.list", review),
        ("Source caps", sources), ("Per-label scores", labels),
        ("Excluded rows (disjoint reasons)", exclusions), ("Group unions and oracles", unions),
        ("Pairwise intersections", intersections), ("Per-instance oracles", oracles),
    ]


def render(report: list, evidence_dir: pathlib.Path, cap: float, output_format: str) -> str:
    if output_format == "tsv":
        # One rectangular table, discriminated by section. Empty cells mean the
        # column is inapplicable to that section, never a missing instance row.
        fields = list(dict.fromkeys(["section", "evidence_dir", "cap_s"]
                                   + [key for _, rows in report for row in rows for key in row]))
        handle = io.StringIO(newline="")
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for title, rows in report:
            writer.writerows({"section": title, "evidence_dir": str(evidence_dir), "cap_s": cap,
                              **row} for row in rows)
        return handle.getvalue()

    def cell(value) -> str:
        return str(value).replace("&", "&amp;").replace("<", "&lt;").replace(
            "|", "&#124;").replace("\n", "<br>")

    lines = ["# Portfolio evidence audit", "", f"Evidence: {evidence_dir}; cap: {cap:g}s.", "",
             "Solved requires a decisive verdict AND smallest_cap_solved <= cap.",
             "PAR-2 is the mean over all label instances; each non-answer costs 2*cap.",
             "Staged classification requires multiple smallest-solved caps or raw run caps;",
             "uniform classification requires a raw TSV with one cap for every label.",
             "Group union and oracle coverage are identical; oracle time takes the fastest answer.",
             "Census unions are isolated upper bounds, not measured concurrent races.", ""]
    for title, rows in report:
        detailed = title in {"Pairwise intersections", "Per-instance oracles"}
        if detailed:
            lines.extend(["<details>", f"<summary>{title}</summary>", ""])
        else:
            lines.extend([f"## {title}", ""])
        if rows:
            fields = list(rows[0])
            lines.append("| " + " | ".join(fields) + " |")
            lines.append("| " + " | ".join("---" for _ in fields) + " |")
            lines.extend("| " + " | ".join(cell(row[key]) for key in fields) + " |"
                         for row in rows)
        else:
            lines.append("No pairs requested.")
        lines.append("")
        if detailed:
            lines.extend(["</details>", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-dir", type=pathlib.Path,
                        default=benchlib.ROOT / "benchmarking/portfolio-evidence-20260910")
    parser.add_argument("--cap", type=float, default=17.0)
    parser.add_argument("--group", action="append", default=[], metavar="NAME=LABEL,LABEL",
                        help="additional union/intersections/oracle (repeatable; use SOURCE/LABEL)")
    parser.add_argument("--output", type=pathlib.Path,
                        help="default: stdout; '-' also means stdout")
    parser.add_argument("--format", choices=("markdown", "tsv"), default="markdown")
    args = parser.parse_args()
    if not math.isfinite(args.cap) or args.cap <= 0:
        parser.error("--cap must be finite and greater than zero")
    try:
        evidence_dir = args.evidence_dir.expanduser().resolve()
        summaries = load_summaries(evidence_dir)
        problems = verdict_conflicts(summaries) + source_validation(evidence_dir, summaries)
        if problems:
            raise EvidenceError("\n".join(problems))
        groups = requested_groups(args.group, summaries)
        for name, members in groups.items():
            first = members[0]
            for other in members[1:]:
                problems.extend(instance_difference(
                    f"{name}: {first}", set(summaries[first].rows),
                    other, set(summaries[other].rows)))
        if problems:
            raise EvidenceError("\n".join(problems))
        rendered = render(tables(summaries, groups, args.cap), evidence_dir, args.cap, args.format)
        if args.output and str(args.output) != "-":
            args.output.write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
    except (EvidenceError, OSError, UnicodeError, csv.Error) as error:
        print(f"FATAL: {error}\nRefusing to score incomplete or conflicting evidence.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
