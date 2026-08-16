#!/usr/bin/env python3
"""Build a deterministic, stratified benchmark panel from Meson JSON logs.

An instance counts as answered only when its Meson result is OK, its output
contains a standalone REALIZABLE/UNREALIZABLE verdict, and it finished before
the requested cap.  This deliberately rejects historical harness passes that
returned UNKNOWN, hit a resource limit, or laundered an exception.
"""

from __future__ import annotations

import argparse
import collections
import csv
import math
import pathlib
import random
import re
from dataclasses import dataclass

from benchlib import load_meson_jsonl


VERDICT_RE = re.compile(r"(?:^|\]\s)(UNREALIZABLE|REALIZABLE)\s*$", re.MULTILINE)
STRATA = ("easy", "border", "gap", "open")


@dataclass(frozen=True)
class ToolResult:
    duration: float
    verdict: str | None
    answered: bool


@dataclass(frozen=True)
class Candidate:
    instance: str
    stratum: str
    acacia_s: float
    ltlsynt_s: float
    verdict: str | None
    family: str
    source_campaign: str


def standalone_verdict(stdout: str | None) -> str | None:
    if not stdout:
        return None
    matches = VERDICT_RE.findall(stdout)
    if not matches:
        return None
    verdicts = set(matches)
    if len(verdicts) != 1:
        raise ValueError(f"conflicting printed verdicts: {sorted(verdicts)}")
    return matches[-1]


def instance_from_row(row: dict, corpus_dir: pathlib.Path | None = None) -> str | None:
    command = row.get("command")
    if isinstance(command, list):
        for index, token in enumerate(command[:-1]):
            if token == "-F":
                path = pathlib.Path(command[index + 1])
                if corpus_dir is not None and path.resolve().parent != corpus_dir.resolve():
                    return None
                return path.name
    if corpus_dir is not None:
        # A basename-only fallback cannot distinguish duplicate names from
        # two suites in one Meson run.  Corpus-filtered references therefore
        # require the normal explicit `-F PATH` command metadata.
        return None
    name = str(row.get("name", ""))
    match = re.search(r"/([^/\s]+\.ltl)(?:\s|$)", name)
    return match.group(1) if match else None


def load_tool(
    path: pathlib.Path, cap: float, corpus_dir: pathlib.Path | None = None
) -> dict[str, ToolResult]:
    results: dict[str, ToolResult] = {}
    for row in load_meson_jsonl(path):
        instance = instance_from_row(row, corpus_dir)
        if instance is None:
            continue
        duration = float(row.get("duration", 0.0) or 0.0)
        verdict = standalone_verdict(row.get("stdout"))
        answered = row.get("result") == "OK" and verdict is not None and duration < cap
        result = ToolResult(duration, verdict, answered)
        previous = results.get(instance)
        if previous is not None and previous != result:
            raise ValueError(f"duplicate, inconsistent result for {instance} in {path}")
        results[instance] = result
    return results


def family_of(instance: str) -> str:
    """Collapse common parameterized benchmark names into stable families."""
    stem = pathlib.Path(instance).stem
    stem = re.sub(r"_[0-9a-f]{8}$", "", stem, flags=re.IGNORECASE)

    patterns = (
        (r"^(prioritized_arbiter_unreal)", 1),
        (r"^(round_robin_arbiter_unreal)", 1),
        (r"^(full_arbiter_unreal)", 1),
        (r"^(ltl2dba_[A-Za-z]+)", 1),
        (r"^(ltl2dpa)", 1),
        (r"^(robot_grid)", 1),
        (r"^(collector)_v", 1),
    )
    for pattern, group in patterns:
        match = re.match(pattern, stem)
        if match:
            return match.group(group)

    # Strip a trailing sequence of numeric parameters without a nested
    # repetition regex.  The previous expression could backtrack
    # exponentially on long digit runs that did not match at the end.
    suffix_start = len(stem)
    if suffix_start and stem[-1].isdigit():
        while suffix_start:
            while suffix_start and stem[suffix_start - 1].isdigit():
                suffix_start -= 1
            if suffix_start == 0 or stem[suffix_start - 1] not in "_-":
                break
            suffix_start -= 1
            if suffix_start == 0 or not stem[suffix_start - 1].isdigit():
                break
    family = stem[:suffix_start].rstrip("_-")
    return family or "numeric"


def assign_candidate(
    instance: str,
    acacia: ToolResult,
    ltlsynt: ToolResult,
    source_campaign: str,
) -> Candidate:
    if acacia.answered and acacia.duration < 1.0:
        stratum = "easy"
    elif acacia.answered:
        stratum = "border"
    elif ltlsynt.answered:
        stratum = "gap"
    else:
        stratum = "open"

    if acacia.verdict and ltlsynt.verdict and acacia.verdict != ltlsynt.verdict:
        raise ValueError(
            f"verdict mismatch on {instance}: acacia={acacia.verdict}, "
            f"ltlsynt={ltlsynt.verdict}"
        )
    verdict = acacia.verdict if acacia.answered else ltlsynt.verdict if ltlsynt.answered else None
    return Candidate(
        instance,
        stratum,
        acacia.duration,
        ltlsynt.duration,
        verdict,
        family_of(instance),
        source_campaign,
    )


def load_references(
    references: list[pathlib.Path],
    acacia_name: str,
    ltlsynt_name: str,
    cap: float,
    corpus_dir: pathlib.Path | None = None,
) -> tuple[dict[str, Candidate], set[str]]:
    # References are ordered newest first.  Coverage is their union; when an
    # instance occurs in more than one campaign, the first (latest) result is
    # authoritative and its directory name is retained as provenance.
    candidates: dict[str, Candidate] = {}
    observed: set[str] = set()
    for reference in references:
        acacia_path = reference / f"{acacia_name}.json"
        ltlsynt_path = reference / f"{ltlsynt_name}.json"
        if not acacia_path.is_file() or not ltlsynt_path.is_file():
            raise FileNotFoundError(
                f"{reference} must contain {acacia_name}.json and {ltlsynt_name}.json"
            )
        acacia = load_tool(acacia_path, cap, corpus_dir)
        ltlsynt = load_tool(ltlsynt_path, cap, corpus_dir)
        shared = acacia.keys() & ltlsynt.keys()
        observed.update(shared)
        for instance in sorted(shared):
            candidates.setdefault(
                instance,
                assign_candidate(
                    instance, acacia[instance], ltlsynt[instance], reference.name
                ),
            )
    return candidates, observed


def apportioned_counts(pool: list[Candidate], quota: int) -> dict[str, int]:
    counts = collections.Counter(candidate.verdict or "UNKNOWN" for candidate in pool)
    total = sum(counts.values())
    raw = {verdict: quota * count / total for verdict, count in counts.items()}
    apportioned = {verdict: math.floor(value) for verdict, value in raw.items()}
    remaining = quota - sum(apportioned.values())
    order = sorted(raw, key=lambda verdict: (-(raw[verdict] - apportioned[verdict]), verdict))
    for verdict in order[:remaining]:
        apportioned[verdict] += 1
    return apportioned


def family_round_robin(pool: list[Candidate], quota: int, seed: int) -> list[Candidate]:
    if quota > len(pool):
        raise ValueError(f"quota {quota} exceeds pool of {len(pool)}")
    rng = random.Random(seed)
    families: dict[str, list[Candidate]] = collections.defaultdict(list)
    for candidate in pool:
        families[candidate.family].append(candidate)
    for values in families.values():
        values.sort(key=lambda candidate: candidate.instance)
        rng.shuffle(values)
    family_names = sorted(families)
    rng.shuffle(family_names)

    selected: list[Candidate] = []
    while len(selected) < quota:
        progress = False
        for family in family_names:
            values = families[family]
            if values and len(selected) < quota:
                selected.append(values.pop())
                progress = True
        if not progress:
            raise RuntimeError("round-robin sampler exhausted its pool")
    return selected


def sample_stratum(pool: list[Candidate], quota: int, seed: int) -> list[Candidate]:
    targets = apportioned_counts(pool, quota)
    selected: list[Candidate] = []
    for offset, verdict in enumerate(sorted(targets)):
        verdict_pool = [candidate for candidate in pool if (candidate.verdict or "UNKNOWN") == verdict]
        selected.extend(family_round_robin(verdict_pool, targets[verdict], seed + offset))
    selected.sort(key=lambda candidate: candidate.instance)
    return selected


def parse_quota(raw: str) -> int | None:
    if raw == "all":
        return None
    quota = int(raw)
    if quota < 0:
        raise argparse.ArgumentTypeError("quota must be non-negative or 'all'")
    return quota


def write_panel(prefix: pathlib.Path, selected: list[Candidate], metadata: list[str]) -> None:
    prefix.parent.mkdir(parents=True, exist_ok=True)
    list_path = prefix.with_suffix(".list")
    tsv_path = prefix.with_suffix(".tsv")
    with list_path.open("w") as handle:
        for line in metadata:
            handle.write(f"# {line}\n")
        for candidate in selected:
            handle.write(f"{candidate.instance}\n")

    fields = [
        "instance",
        "stratum",
        "acacia_s",
        "ltlsynt_s",
        "verdict",
        "family",
        "source_campaign",
    ]
    with tsv_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for candidate in selected:
            writer.writerow(
                {
                    "instance": candidate.instance,
                    "stratum": candidate.stratum,
                    "acacia_s": f"{candidate.acacia_s:.6f}",
                    "ltlsynt_s": f"{candidate.ltlsynt_s:.6f}",
                    "verdict": candidate.verdict or "UNKNOWN",
                    "family": candidate.family,
                    "source_campaign": candidate.source_campaign,
                }
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--reference",
        action="append",
        required=True,
        type=pathlib.Path,
        help="reference directory, repeat newest first (coverage is unioned)",
    )
    parser.add_argument("--acacia", default="best_decomp_mona")
    parser.add_argument("--ltlsynt", default="ltlsynt")
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path,
                        help="output prefix; .list and .tsv are appended")
    parser.add_argument("--cap", type=float, default=17.0)
    parser.add_argument("--seed", type=int, default=20260804)
    parser.add_argument("--easy", type=parse_quota, default=40)
    parser.add_argument("--border", type=parse_quota, default=None)
    parser.add_argument("--gap", type=parse_quota, default=60)
    parser.add_argument("--open", type=parse_quota, default=15)
    args = parser.parse_args()

    candidates, observed = load_references(
        args.reference, args.acacia, args.ltlsynt, args.cap, args.corpus
    )
    corpus = {path.name for path in args.corpus.glob("*.ltl")}
    covered = corpus & observed
    eligible = corpus & candidates.keys()
    uncovered = corpus - covered
    outside = observed - corpus
    print(
        f"corpus={len(corpus)} covered={len(covered)} eligible={len(eligible)} "
        f"uncovered={len(uncovered)} "
        f"reference_outside_corpus={len(outside)}"
    )

    quotas = {"easy": args.easy, "border": args.border, "gap": args.gap, "open": args.open}
    selected: list[Candidate] = []
    composition: list[str] = []
    for index, stratum in enumerate(STRATA):
        pool = sorted(
            (candidate for name, candidate in candidates.items()
             if name in corpus and candidate.stratum == stratum),
            key=lambda candidate: candidate.instance,
        )
        requested = quotas[stratum]
        quota = len(pool) if requested is None else min(requested, len(pool))
        if requested is not None and requested > len(pool):
            print(
                f"{stratum}: requested={requested} exceeds pool={len(pool)}; "
                "taking the complete pool"
            )
        chosen = sample_stratum(pool, quota, args.seed + index * 1000)
        selected.extend(chosen)
        verdicts = collections.Counter(candidate.verdict or "UNKNOWN" for candidate in chosen)
        families = collections.Counter(candidate.family for candidate in chosen)
        largest_family = max(families.values(), default=0)
        composition.append(
            f"{stratum}: pool={len(pool)} take={len(chosen)} "
            f"real={verdicts['REALIZABLE']} unreal={verdicts['UNREALIZABLE']} "
            f"unknown={verdicts['UNKNOWN']} largest_family={largest_family}"
        )
        print(composition[-1])

    metadata = [
        "Generated by benchmarking/make-panel.py; do not edit by hand.",
        f"seed={args.seed} cap={args.cap:g}s covered={len(covered)} "
        f"eligible={len(eligible)} uncovered={len(uncovered)}",
        *composition,
    ]
    write_panel(args.output, selected, metadata)
    print(f"wrote {len(selected)} entries to {args.output.with_suffix('.list')}")
    print(f"wrote provenance to {args.output.with_suffix('.tsv')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
