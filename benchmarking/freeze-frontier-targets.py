#!/usr/bin/env python3
"""Freeze the top-20 theory targets from the preselected frontier candidates.

A4 ranked candidates on what the corpus knows: family structure, parameter
position, source size.  This stage adds what the solver actually did on them —
automaton size, semantic action count, rank dimension — so that "this instance
is hard" becomes "this instance is hard *because* its action table is enormous"
or "*because* its rank dimension explodes".  That distinction is what a family
theorem needs, and it is the reason the diagnostics campaign runs at all.

The set is frozen here, **before** any external-solver annotation.  Nothing in
this module reads or invokes `ltlsynt`: a target set chosen partly by which
instances another tool happens to solve would tell us about that tool, not
about where Acacia stops.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib
import sys

# Committed so the ranking can be audited and reproduced rather than inferred
# from the output.  Tiers are exclusive: an instance scores the +6 automaton
# term or the +4 one, never both.
WEIGHTS = {
    "minimal_point": 12,
    "unsolved_at_cap": 10,
    "neighbour_fast": 8,
    "aut_states_small": 6,
    "aut_states_medium": 4,
    "actions_small": 4,
    "actions_medium": 3,
    "rank_small": 4,
    "rank_medium": 2,
    "tlsf_small": 3,
    "clean_cutoff": 3,
    "infrastructure_failure": -8,
}

# A memory limit is not an infrastructure failure.  Exhausting 8 GiB is a real
# limit of the backward antichain — and precisely the limit a forward reachable
# solver might move — so these keep full eligibility.  Only a solver that
# returned no usable answer (ERROR/UNKNOWN) is penalised.
INFRASTRUCTURE_RESULTS = {"ERROR", "UNKNOWN"}
MEMORY_RESULTS = {"MEMOUT", "CRASH"}


def read_tsv(path: pathlib.Path) -> list[dict]:
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def read_csv(path: pathlib.Path) -> list[dict]:
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


def to_int(text: str | None, default: int = 0) -> int:
    try:
        return int(float(text))
    except (TypeError, ValueError):
        return default


def aggregate_diagnostics(rows: list[dict]) -> dict[str, dict]:
    """Collapse the per-worker diagnostic rows into one record per instance.

    An instance is decomposed into several workers, each emitting its own row,
    so a naive "first row wins" would describe whichever worker happened to be
    listed first rather than the one that made the instance hard.  Sizes are
    taken as maxima over the workers, action counts as a sum, and the verdict
    from the worker that ran longest.
    """
    by_instance: dict[str, list[dict]] = collections.defaultdict(list)
    for row in rows:
        name = row.get("instance", "").strip()
        if name:
            by_instance[name].append(row)

    aggregated: dict[str, dict] = {}
    for name, group in by_instance.items():
        slowest = max(group, key=lambda r: to_int(r.get("total_ms")))
        aggregated[name] = {
            "aut_states_max": max(to_int(r.get("aut_states")) for r in group),
            "aut_edges_max": max(to_int(r.get("aut_edges")) for r in group),
            # The handoff says to compute the numeric rank coordinate count as
            # bool_threshold when it is not emitted directly, which it is not.
            "numeric_rank_coordinates": max(
                to_int(r.get("bool_threshold")) for r in group
            ),
            "max_f_size_max": max(to_int(r.get("max_f_size")) for r in group),
            "loops_max": max(to_int(r.get("loops")) for r in group),
            "k_attempts_max": max(to_int(r.get("k_attempts")) for r in group),
            "actions_seen_sum": sum(to_int(r.get("actions_seen")) for r in group),
            "worker_count": len(group),
            "result": slowest.get("result", ""),
            "final_reason": slowest.get("final_reason", ""),
        }
    return aggregated


def score_candidate(row: dict, diag: dict | None, cutoff_families: set[str]):
    """Return (score, breakdown) for one candidate."""
    score = 0
    parts: list[str] = []

    def award(key: str, label: str) -> None:
        nonlocal score
        score += WEIGHTS[key]
        parts.append(f"{label}{WEIGHTS[key]:+d}")

    if row.get("pair_kind") == "cover" and row.get("parameter_dimension", "0") != "0":
        award("minimal_point", "minimal")
    award("unsolved_at_cap", "unsolved")

    neighbour_seconds = row.get("neighbour_seconds", "")
    try:
        if neighbour_seconds and float(neighbour_seconds) <= 5.0:
            award("neighbour_fast", "nbr5s")
    except ValueError:
        pass

    if diag is not None:
        if diag["aut_states_max"] and diag["aut_states_max"] <= 100:
            award("aut_states_small", "aut100")
        elif diag["aut_states_max"] and diag["aut_states_max"] <= 300:
            award("aut_states_medium", "aut300")

        if diag["actions_seen_sum"] and diag["actions_seen_sum"] <= 500:
            award("actions_small", "act500")
        elif diag["actions_seen_sum"] and diag["actions_seen_sum"] <= 2000:
            award("actions_medium", "act2000")

        rank = diag["numeric_rank_coordinates"]
        if rank and rank <= 64:
            award("rank_small", "rank64")
        elif rank and rank <= 256:
            award("rank_medium", "rank256")

    if to_int(row.get("tlsf_bytes")) and to_int(row.get("tlsf_bytes")) <= 20 * 1024:
        award("tlsf_small", "tlsf20k")

    if row.get("family_key") in cutoff_families:
        award("clean_cutoff", "cutoff")

    result = (row.get("unsolved_result") or "").upper()
    if result in INFRASTRUCTURE_RESULTS:
        award("infrastructure_failure", "infra")

    return score, ",".join(parts)


def is_memory_bounded(row: dict) -> bool:
    return (row.get("failure_kind") == "memory_limit"
            or (row.get("unsolved_result") or "").upper() in MEMORY_RESULTS)


def select(scored: list[dict], target_count: int, memory_quota: int,
           per_family: int = 2) -> list[dict]:
    """Greedy by score under a per-family cap, then top up the memory quota."""
    chosen: list[dict] = []
    family_counts: collections.Counter[str] = collections.Counter()

    for row in scored:
        if len(chosen) >= target_count:
            break
        if family_counts[row["family_key"]] >= per_family:
            continue
        chosen.append(row)
        family_counts[row["family_key"]] += 1
        row["selection_reason"] = "score"

    memory_rows = [r for r in chosen if is_memory_bounded(r)]
    if len(memory_rows) < memory_quota:
        remaining = [r for r in scored if r not in chosen and is_memory_bounded(r)]
        # Round-robin over distinct families: taking the highest scorers alone
        # put every quota slot in one family, which characterises one benchmark
        # generator rather than the memory failure mode.
        grouped: dict[str, list[dict]] = collections.defaultdict(list)
        for row in remaining:
            grouped[row["family_key"]].append(row)
        ordered_families = sorted(
            grouped, key=lambda k: (-grouped[k][0]["score"], k)
        )
        queue: list[dict] = []
        depth = 0
        while any(len(grouped[f]) > depth for f in ordered_families):
            for family in ordered_families:
                if len(grouped[family]) > depth:
                    queue.append(grouped[family][depth])
            depth += 1

        for row in queue:
            if len(memory_rows) >= memory_quota:
                break
            if family_counts[row["family_key"]] >= per_family:
                continue
            # Displace the lowest-scoring ordinary row.  Prefer a family with a
            # spare representative; if every chosen row is the only one from its
            # family — the normal case under a max-2 cap — a strict "never
            # displace a sole representative" rule would make the quota
            # unfillable, so fall back to displacing one, but only when the
            # incoming row brings a family not already present.  Family
            # diversity is then unchanged and the memory failure mode is gained.
            ordinary = [r for r in chosen if r["selection_reason"] == "score"]
            ordinary.sort(key=lambda r: r["score"])
            victim = next(
                (r for r in ordinary if family_counts[r["family_key"]] > 1), None
            )
            if victim is None and family_counts[row["family_key"]] == 0:
                victim = next(iter(ordinary), None)
            if victim is None:
                break
            chosen.remove(victim)
            family_counts[victim["family_key"]] -= 1
            row["selection_reason"] = "memory_quota"
            chosen.append(row)
            family_counts[row["family_key"]] += 1
            memory_rows.append(row)

    chosen.sort(key=lambda r: (-r["score"], r["instance"]))
    for index, row in enumerate(chosen, start=1):
        row["rank"] = index
    return chosen


COLUMNS = [
    "rank", "score", "score_breakdown", "selection_reason", "instance", "tlsf_file",
    "family_key", "family_display", "parameter_values_json", "tlsf_bytes",
    "unsolved_result", "failure_kind", "aut_states_max", "aut_edges_max",
    "numeric_rank_coordinates", "actions_seen_sum", "max_f_size_max", "loops_max",
    "k_attempts_max", "worker_count", "diagnostics_present", "neighbour_instance",
    "neighbour_params_json", "neighbour_seconds", "neighbour_result", "pair_kind",
]


def main(argv=None) -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    bench = root / "benchmarking"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preselection", type=pathlib.Path,
                        default=bench / "syntcomp26-frontier-preselection.tsv")
    parser.add_argument("--diagnostics", type=pathlib.Path,
                        default=bench / "syntcomp26-frontier-diagnostics.tsv")
    parser.add_argument("--frontiers", type=pathlib.Path,
                        default=bench / "syntcomp26-family-frontiers.tsv")
    parser.add_argument("--target-count", type=int, default=20)
    parser.add_argument("--memory-quota", type=int, default=2)
    parser.add_argument("--out-tsv", type=pathlib.Path,
                        default=bench / "syntcomp26-frontier-targets.tsv")
    parser.add_argument("--out-list", type=pathlib.Path,
                        default=bench / "syntcomp26-frontier-targets.list")
    args = parser.parse_args(argv)

    candidates = read_tsv(args.preselection)
    diagnostics = (aggregate_diagnostics(read_csv(args.diagnostics))
                   if args.diagnostics.is_file() else {})
    cutoff_families = {
        row["family_key"] for row in read_tsv(args.frontiers)
        if row.get("classification") == "clean_cutoff"
    } if args.frontiers.is_file() else set()

    scored: list[dict] = []
    for row in candidates:
        diag = diagnostics.get(row["instance"])
        score, breakdown = score_candidate(row, diag, cutoff_families)
        record = dict(row)
        record.update({
            "score": score,
            "score_breakdown": breakdown,
            "diagnostics_present": "true" if diag else "false",
            "aut_states_max": diag["aut_states_max"] if diag else "",
            "aut_edges_max": diag["aut_edges_max"] if diag else "",
            "numeric_rank_coordinates": diag["numeric_rank_coordinates"] if diag else "",
            "actions_seen_sum": diag["actions_seen_sum"] if diag else "",
            "max_f_size_max": diag["max_f_size_max"] if diag else "",
            "loops_max": diag["loops_max"] if diag else "",
            "k_attempts_max": diag["k_attempts_max"] if diag else "",
            "worker_count": diag["worker_count"] if diag else "",
        })
        scored.append(record)

    scored.sort(key=lambda r: (-r["score"], r["instance"]))
    chosen = select(scored, args.target_count, args.memory_quota)

    args.out_tsv.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out_tsv, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS, delimiter="\t",
                                extrasaction="ignore", lineterminator="\n")
        writer.writeheader()
        writer.writerows(chosen)
    args.out_list.write_text("".join(f"{r['instance']}\n" for r in chosen))

    families = {r["family_key"] for r in chosen}
    quota = sum(1 for r in chosen if r["selection_reason"] == "memory_quota")
    missing = sum(1 for r in chosen if r["diagnostics_present"] == "false")
    print(f"wrote {args.out_tsv} ({len(chosen)} targets)")
    print(f"  families: {len(families)}  memory-quota rows: {quota}  "
          f"without diagnostics: {missing}")
    print(f"  score range: {chosen[-1]['score']}..{chosen[0]['score']}" if chosen else "")
    return 0


if __name__ == "__main__":
    sys.exit(main())
