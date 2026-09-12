#!/usr/bin/env python3
"""Select and freeze a deterministic diagnostic cohort from saved race evidence.

Run before evaluating sprint candidates. Only the instance list and the three
named summaries inform selection; no solver is run. Missing label rows abort
selection, since missing evidence cannot establish an unsolved instance.

Tier C membership follows family frequency, then family name, choosing the
smallest instance in each family. Both outputs list tiers A, B, C in order,
sorting instances within each tier. Headers record a canonical command with
output destinations omitted so different output paths produce identical bytes
at the same Git HEAD. Paths inside the repository are recorded relative to it.
"""

from __future__ import annotations

import argparse
import csv
import math
import pathlib
import shlex
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass

import benchlib
from family_metadata import family_of


RACE4 = "race4-shipped"
RACE5 = "race5-plus-guarded-real"
RACE2 = "race2-best"
LABELS = (RACE4, RACE5, RACE2)
DEFAULT_EVIDENCE = "benchmarking/portfolio-evidence-20260910/race-curve"
SUMMARY_COLUMNS = (
    "solver_label", "instance", "smallest_cap_solved", "decisive_result",
    "decisive_seconds", "still_unsolved_at_max_cap", "failure_kind_at_max_cap", "max_cap_s",
)
OUTPUT_COLUMNS = ("instance", "tier", "family", "reason")


@dataclass(frozen=True)
class Row:
    verdict: str
    solved_at: float | None
    seconds: float | None

    def exclusion(self, cap: float) -> str | None:
        """Use audit-race-evidence.py's decisive-verdict and staged-cap filter."""
        if self.verdict not in benchlib.SOLVED:
            return "non_decisive"
        if self.solved_at is None:
            return "missing_cap"
        if self.solved_at > cap:
            return "above_cap"
        return None


def number(raw: str, context: str, *, optional: bool = False) -> float | None:
    if raw == "" and optional:
        return None
    try:
        value = float(raw)
    except ValueError:
        raise ValueError(f"{context}: invalid number {raw!r}") from None
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"{context}: expected a finite nonnegative number, got {raw!r}")
    return value


def load_evidence(evidence_dir: pathlib.Path) -> tuple[list[str], dict[str, dict[str, Row]]]:
    list_path = evidence_dir / "instances.list"
    instances = [line.strip() for line in list_path.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.lstrip().startswith("#")]
    if not instances:
        raise ValueError(f"MISSING DATA: {list_path}: no instances")
    if len(instances) != len(set(instances)):
        raise ValueError(f"{list_path}: duplicate instances")
    instances.sort()
    universe = set(instances)
    summaries = {}
    problems = []
    for label in LABELS:
        path = evidence_dir / f"{label}-summary.tsv"
        rows = {}
        if not path.is_file():
            problems.append(f"MISSING DATA: {path}: summary is absent")
            continue
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(
                (line for line in handle if not line.lstrip().startswith("#")), delimiter="\t"
            )
            if reader.fieldnames != list(SUMMARY_COLUMNS):
                raise ValueError(f"{path}: unexpected summary header")
            for line, raw in enumerate(reader, 2):
                context = f"{path}:{line}"
                if None in raw or any(value is None for value in raw.values()):
                    raise ValueError(f"{context}: malformed TSV row")
                if raw["solver_label"] != label:
                    raise ValueError(f"{context}: expected solver_label {label!r}")
                instance = raw["instance"]
                if not instance:
                    raise ValueError(f"{context}: empty instance")
                if instance in rows:
                    raise ValueError(f"{context}: duplicate instance {instance!r} for {label}")
                solved_at = number(raw["smallest_cap_solved"], context, optional=True)
                verdict = raw["decisive_result"]
                seconds = (number(raw["decisive_seconds"], context)
                           if verdict in benchlib.SOLVED else None)
                rows[instance] = Row(verdict, solved_at, seconds)
        missing = sorted(universe - rows.keys())
        if missing:
            problems.append(f"MISSING DATA: {label}: {len(missing)} missing rows: "
                            + ", ".join(missing))
        extra = sorted(rows.keys() - universe)
        if extra:
            problems.append(f"{label}: instances absent from instances.list: " + ", ".join(extra))
        summaries[label] = rows
    if problems:
        raise ValueError("\n".join(problems))
    return instances, summaries


def select_cohort(
    instances: list[str], summaries: dict[str, dict[str, Row]], cap: float, unsolved_families: int
) -> tuple[list[dict[str, str]], list[str]]:
    solved = {label: {name for name, row in summaries[label].items()
                      if row.exclusion(cap) is None} for label in LABELS}
    tier_a = solved[RACE5] - solved[RACE4] - solved[RACE2]
    race2_only = solved[RACE2] - solved[RACE4]
    race4_only = solved[RACE4] - solved[RACE2]
    tier_b = race2_only | race4_only
    unsolved = set(instances) - solved[RACE4] - solved[RACE5] - solved[RACE2]
    by_family = defaultdict(list)
    for instance in sorted(unsolved):
        by_family[family_of(instance)].append(instance)
    families = sorted(by_family, key=lambda family: (-len(by_family[family]), family))
    tier_c = [min(by_family[family]) for family in families[:unsolved_families]]

    cohort = []
    for tier, members in (("A", tier_a), ("B", tier_b), ("C", tier_c)):
        for instance in sorted(members):
            if tier == "A":
                seconds = summaries[RACE5][instance].seconds
                reason = f"unique to {RACE5} ({seconds:.2f}s)"
            elif tier == "B":
                winner = RACE2 if instance in race2_only else RACE4
                seconds = summaries[winner][instance].seconds
                reason = f"{winner} only ({seconds:.2f}s)"
            else:
                reason = "unsolved by all three"
            cohort.append({"instance": instance, "tier": tier,
                           "family": family_of(instance), "reason": reason})

    counts = [
        f"Tier A (guarded-real control): {len(tier_a)}",
        f"Tier B (crossover): {len(tier_b)} "
        f"({RACE2} only: {len(race2_only)}; {RACE4} only: {len(race4_only)})",
        f"Tier C (unsolved, family-diverse): {len(tier_c)} selected from "
        f"{len(unsolved)} unsolved instances across {len(families)} families",
        f"Total: {len(cohort)}",
    ]
    return cohort, counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-dir", type=pathlib.Path,
                        default=benchlib.ROOT / DEFAULT_EVIDENCE,
                        help=f"saved race summaries (default: {DEFAULT_EVIDENCE})")
    parser.add_argument("--cap", type=float, default=17.0,
                        help="maximum smallest_cap_solved (default: 17.0)")
    parser.add_argument("--unsolved-families", type=int, default=25,
                        help="maximum number of tier C families (default: 25)")
    parser.add_argument("--output", type=pathlib.Path, help="write the commented instance list")
    parser.add_argument("--tsv", type=pathlib.Path, help="write the tier/family/reason sidecar")
    args = parser.parse_args(argv)
    if not math.isfinite(args.cap) or args.cap <= 0:
        parser.error("--cap must be finite and greater than zero")
    if args.unsolved_families < 0:
        parser.error("--unsolved-families must be nonnegative")
    if args.output and args.tsv and args.output.resolve() == args.tsv.resolve():
        parser.error("--output and --tsv must name different files")

    try:
        evidence_dir = args.evidence_dir.expanduser().resolve()
        instances, summaries = load_evidence(evidence_dir)
        cohort, counts = select_cohort(instances, summaries, args.cap, args.unsolved_families)
        try:
            evidence_name = evidence_dir.relative_to(benchlib.ROOT).as_posix()
        except ValueError:
            evidence_name = evidence_dir.as_posix()
        if args.output:
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=benchlib.ROOT, check=True,
                capture_output=True, text=True,
            ).stdout.strip()
            command = shlex.join([
                "python3", "benchmarking/select-diagnostic-cohort.py",
                "--evidence-dir", evidence_name, "--cap", str(args.cap),
                "--unsolved-families", str(args.unsolved_families),
            ])
            header = [
                "Diagnostic cohort",
                f"Command (output destinations omitted): {command}",
                f"Evidence directory: {evidence_name}",
                f"Cap: {args.cap}s",
                f"Git HEAD: {head}",
                *counts,
            ]
            content = "\n".join([*(f"# {line}" for line in header),
                                 *(row["instance"] for row in cohort)]) + "\n"
            args.output.write_text(content, encoding="utf-8", newline="\n")
        if args.tsv:
            with args.tsv.open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=OUTPUT_COLUMNS, delimiter="\t",
                                        lineterminator="\n")
                writer.writeheader()
                writer.writerows(cohort)
        print(f"Evidence: {evidence_name} ({len(instances)} instances; cap {args.cap}s)")
        print("\n".join(counts))
    except (OSError, ValueError, csv.Error, subprocess.SubprocessError) as error:
        print(f"FATAL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
