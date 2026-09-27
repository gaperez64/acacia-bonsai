#!/usr/bin/env python3
"""Rank seven isolated 60 s arms and create a deterministic plain smoke sample."""
from __future__ import annotations
import argparse
import csv
import itertools
import math
import pathlib
import random
import runpy
import statistics

HERE = pathlib.Path(__file__).resolve().parent
ARMS = ("real:small:backward", "real:small:forward",
        "unreal:formula:spot-guarded-sparse", "unreal:automaton:forward",
        "real:gr1:oxidd", "unreal:gr1:oxidd", "real:param-lift:oxidd")
SOLVED = {"REALIZABLE", "UNREALIZABLE"}
RESULTS = SOLVED | {"UNKNOWN", "TIMEOUT", "RESOURCE_LIMIT", "ERROR"}
CAP = 60.0
SEED = 20260926


def read_ids(path: pathlib.Path) -> list[str]:
    ids = [s for line in path.read_text().splitlines()
           if (s := line.strip()) and not s.startswith("#")]
    if len(ids) != 1524 or len(set(ids)) != len(ids):
        raise ValueError(f"{path}: expected 1,524 distinct original IDs")
    return ids


def read_leg(path: pathlib.Path, ids: list[str]) -> dict[str, tuple[str, float]]:
    if not path.is_file():
        raise FileNotFoundError(f"missing leg {path}; wait for export-cactus")
    rows = {}
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["instance", "result", "seconds", "exit"]:
            raise ValueError(f"{path}: expected four-column cactus CSV")
        for row in reader:
            name, result = row["instance"], row["result"]
            seconds = float(row["seconds"])
            if name in rows or result not in RESULTS or not math.isfinite(seconds) or not 0 <= seconds <= CAP:
                raise ValueError(f"{path}: invalid or duplicate row {name}")
            rows[name] = (result, seconds)
    if set(rows) != set(ids):
        raise ValueError(f"{path}: missing {len(set(ids)-set(rows))}, extra {len(set(rows)-set(ids))} IDs")
    sidecar = path.with_suffix(".raw.tsv")
    if not sidecar.is_file():
        raise FileNotFoundError(f"missing leg sidecar {sidecar}; wait for export-cactus")
    seen = set()
    with sidecar.open(newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["instance", "result", "exit_code", "seconds", "cap_s"]:
            raise ValueError(f"{sidecar}: unexpected columns")
        for row in reader:
            name = row["instance"]
            if name in seen or name not in rows or float(row["cap_s"]) != CAP:
                raise ValueError(f"{sidecar}: invalid, duplicate, or extra row {name}")
            seen.add(name)
            result, seconds = rows[name]
            expected = {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}.get(row["result"], row["result"])
            if expected != result or (result not in SOLVED and seconds != CAP):
                raise ValueError(f"{sidecar}: status mismatch for {name}")
            if result in SOLVED and float(row["seconds"]) != seconds:
                raise ValueError(f"{sidecar}: solved time mismatch for {name}")
    if seen != set(ids):
        raise ValueError(f"{sidecar}: missing {len(set(ids)-seen)} IDs")
    return rows


def winner(legs: dict, name: str, subset: tuple[str, ...]) -> str | None:
    solved = [(legs[arm][name][1], i, arm) for i, arm in enumerate(subset)
              if legs[arm][name][0] in SOLVED]
    return min(solved)[2] if solved else None


def rank(legs: dict, ids: list[str]) -> dict[int, list[tuple]]:
    for name in ids:
        verdicts = {legs[arm][name][0] for arm in ARMS if legs[arm][name][0] in SOLVED}
        if len(verdicts) > 1:
            raise ValueError(f"conflicting solved verdicts for {name}: {verdicts}")
    ranked = {}
    for size in (4, 5):
        entries = []
        for subset in itertools.combinations(ARMS, size):
            solved, solved_seconds = 0, 0.0
            unique, credit = dict.fromkeys(subset, 0), dict.fromkeys(subset, 0)
            for name in ids:
                successes = [(arm, legs[arm][name][1]) for arm in subset if legs[arm][name][0] in SOLVED]
                if successes:
                    solved += 1
                    solved_seconds += min(seconds for _, seconds in successes)
                    credit[winner(legs, name, subset)] += 1
                    if len(successes) == 1:
                        unique[successes[0][0]] += 1
            entries.append((solved, solved_seconds + 2 * CAP * (len(ids)-solved), subset, unique, credit))
        ranked[size] = sorted(entries, key=lambda x: (-x[0], x[1], x[2]))
    return ranked


def reference_verdicts(legs: dict, ids: list[str], main_root: pathlib.Path) -> tuple[dict[str, str], list[str]]:
    """Use solved legs and independent 60 s references; never invent a verdict."""
    known = {name: set() for name in ids}
    for arm in ARMS:
        for name in ids:
            if legs[arm][name][0] in SOLVED:
                known[name].add(legs[arm][name][0])
    references = [HERE / "derived-60s/epoch-1/B-cap60-epoch1.csv",
                  HERE / "derived-60s/epoch-2/B-cap60-epoch2.csv",
                  HERE / "derived-60s/ltlsynt-cap60.csv",
                  main_root / "benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv"]
    fresh_tacas = HERE / "full/60s/TACAS23/v1-cap60.csv"
    if fresh_tacas.is_file():
        references.append(fresh_tacas)
    conflicts = []
    for path in references:
        with path.open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != len(ids) or {row["instance"] for row in rows} != set(ids):
            raise ValueError(f"{path}: expected exactly the original instance IDs")
        for row in rows:
            if row["result"] in SOLVED:
                name, verdict = row["instance"], row["result"]
                if known[name] and verdict not in known[name]:
                    conflicts.append(f"{path.name}:{name}")
                else:
                    known[name].add(verdict)
    return ({name: next(iter(verdicts)) for name, verdicts in known.items() if verdicts}, conflicts)


def thermal_candidates(legs: dict, ids: list[str], ranked: dict, samples: pathlib.Path) -> tuple[dict, dict]:
    """Apply thermal-annotate.py's candidate and hot/unknown label policy."""
    thermal = runpy.run_path(str(HERE / "thermal-annotate.py"))
    with samples.open(newline="") as stream:
        grouped = {}
        for row in csv.DictReader(stream, delimiter="\t"):
            grouped.setdefault(row["leg"], []).append(row)
    measures = {leg: thermal["metrics"](rows) for leg, rows in grouped.items() if len(rows) >= 2}
    clean_rates = [measures[f"arm-{i}"][1] for i in range(1, len(ARMS) + 1)
                   if f"arm-{i}" in measures and thermal["coverage"](grouped[f"arm-{i}"]) >= .8
                   and math.isfinite(measures[f"arm-{i}"][1])]
    median = statistics.median(clean_rates) if clean_rates else float("nan")
    labels = {}
    for i, arm in enumerate(ARMS, 1):
        leg = f"arm-{i}"
        rate = measures.get(leg, (float("nan"), float("nan"), -1))[1]
        cov = thermal["coverage"](grouped.get(leg, []))
        labels[arm] = ("thermal unknown" if cov < .8 or not math.isfinite(rate) or not clean_rates
                       else "anomalous" if rate > 1.25 * median else "clean")
    noise = thermal["noise_by_instance"](HERE / "derived-60s/epoch-1/B-cap60-epoch1.csv",
                                          HERE / "derived-60s/epoch-2/B-cap60-epoch2.csv", ids)
    relevant = thermal["selection_relevant"](legs, ids, ranked, noise)
    return {arm: set(relevant[arm]) if labels[arm] != "clean" else set() for arm in ARMS}, labels


def bound(legs: dict, ids: list[str], subset: tuple[str, ...], candidates: dict,
          verdicts: dict, worst: bool) -> tuple[int, float, dict[str, set[str]]]:
    solved = 0
    score = 0.0
    changed = {}
    for name in ids:
        original = [legs[arm][name][1] for arm in subset if legs[arm][name][0] in SOLVED]
        nominal = min(original) if original else 2 * CAP
        altered = []
        possible = set()
        for arm in subset:
            result, seconds = legs[arm][name]
            if name in candidates[arm] and worst and result in SOLVED and .8 * CAP <= seconds <= CAP:
                possible.add(arm)
            elif name in candidates[arm] and not worst and result in {"TIMEOUT", "RESOURCE_LIMIT", "ERROR"} and name in verdicts:
                altered.append(.8 * CAP)
                possible.add(arm)
            elif result in SOLVED:
                altered.append(seconds)
        value = min(altered) if altered else 2 * CAP
        solved += bool(altered)
        score += value
        if value != nominal:
            changed[name] = possible
    return solved, score, changed


def report_robustness(legs: dict, ids: list[str], ranked: dict,
                      samples: pathlib.Path, main_root: pathlib.Path) -> None:
    candidates, labels = thermal_candidates(legs, ids, ranked, samples)
    verdicts, conflicts = reference_verdicts(legs, ids, main_root)
    print("Robustness bound: hot/thermal-unknown selection-relevant reruns; "
          "top worst case versus each challenger best case.")
    print("Thermal labels: " + ", ".join(f"arm-{i}={labels[arm]} ({len(candidates[arm])} candidates)"
                                        for i, arm in enumerate(ARMS, 1)))
    print(f"Known verdicts for possible new solves: {len(verdicts)}/{len(ids)}")
    if conflicts:
        print("Conflicting reference verdicts ignored: " + ", ".join(conflicts))
    for size in (4, 5):
        top, runner = ranked[size][:2]
        worst_solved, worst_score, worst_changes = bound(legs, ids, top[2], candidates, verdicts, True)
        print(f"{size}-arm nominal runner-up gap: solved={top[0]-runner[0]:+d}, "
              f"PAR-2={runner[1]-top[1]:+.6f} s; top={','.join(top[2])}")
        print(f"{size}-arm top worst case: solved={worst_solved}/{len(ids)} PAR-2={worst_score:.6f} s")
        threats = []
        needed = set()
        for challenger in ranked[size][1:]:
            best_solved, best_score, best_changes = bound(legs, ids, challenger[2], candidates, verdicts, False)
            if best_solved > worst_solved or (best_solved == worst_solved and best_score <= worst_score + 1e-8):
                decisive = {name: worst_changes.get(name, set()) | best_changes.get(name, set())
                            for name in worst_changes.keys() | best_changes.keys()}
                reruns = {(arm, name) for name, arms in decisive.items() for arm in arms}
                needed.update(reruns)
                threats.append((challenger, best_solved, best_score, decisive, reruns))
        if not threats:
            print(f"{size}-arm ROBUST: top beats all {len(ranked[size])-1} challengers.")
        else:
            print(f"{size}-arm NOT ROBUST: {len(threats)} challengers can overtake or tie.")
            for challenger, best_solved, best_score, decisive, reruns in threats:
                print(f"  challenger best solved={best_solved}/{len(ids)} PAR-2={best_score:.6f} s "
                      f"arms={','.join(challenger[2])}; decisive instances={','.join(name for name in ids if name in decisive)}")
            print(f"{size}-arm required reruns ({len(needed)} arm/instance pairs): " +
                  ", ".join(f"arm-{ARMS.index(arm)+1}:{name}" for arm, name in sorted(needed, key=lambda x: (ARMS.index(x[0]), x[1]))))


def make_sample(legs: dict, ids: list[str], chosen: tuple[str, ...], n: int, output: pathlib.Path) -> None:
    if n < len(chosen)+4 or n > len(ids):
        raise ValueError(f"--smoke-sample must be between {len(chosen)+4} and {len(ids)}")
    rng = random.Random(SEED)
    buckets = {arm: [] for arm in chosen}
    buckets["all-timeout"], buckets["unknown-heavy"] = [], []
    for name in ids:
        arm = winner(legs, name, chosen)
        if arm:
            buckets[arm].append(name)
        elif all(legs[a][name][0] == "TIMEOUT" for a in chosen):
            buckets["all-timeout"].append(name)
        elif sum(legs[a][name][0] == "UNKNOWN" for a in chosen) >= math.ceil(len(chosen)/2):
            buckets["unknown-heavy"].append(name)
    for values in buckets.values():
        rng.shuffle(values)
    selected = set()
    # Reserve the two mandatory picks per populated stratum before filling the
    # remainder. A small N must never silently turn into a larger sample.
    mandatory = sum(min(2, len(values)) for values in buckets.values())
    if n < mandatory:
        raise ValueError(f"--smoke-sample {n} is too small for {mandatory} mandatory stratum picks")
    positions = {key: min(len(values), max(2, n // (2*len(buckets)))) for key, values in buckets.items()}
    while sum(positions.values()) > n:
        for key in reversed(tuple(positions)):
            if positions[key] > min(2, len(buckets[key])):
                positions[key] -= 1
                break
    for key, values in buckets.items():
        selected.update(values[:positions[key]])
    while len(selected) < n:
        progressed = False
        for key, values in buckets.items():
            if positions[key] < len(values):
                selected.add(values[positions[key]])
                positions[key] += 1
                progressed = True
                if len(selected) == n:
                    break
        if not progressed:
            rest = [name for name in ids if name not in selected]
            rng.shuffle(rest)
            selected.update(rest[:n-len(selected)])
            break
    ordered = [name for name in ids if name in selected]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(ordered) + "\n")
    print(f"smoke sample: {len(ordered)} IDs, seed={SEED}, file={output}")
    print("strata: " + ", ".join(f"{key}={sum(x in selected for x in values)}" for key, values in buckets.items()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", type=pathlib.Path, required=True)
    parser.add_argument("--leg", action="append", default=[], metavar="ARM=CSV", help="override standard perarm-m1/legs/arm-N export")
    parser.add_argument("--smoke-sample", type=int, metavar="N")
    parser.add_argument("--arms", help="chosen comma-separated 4 or 5 arms, for sampling")
    parser.add_argument("--smoke-output", type=pathlib.Path)
    parser.add_argument("--robustness", action="store_true", help="bound the effect of thermal rerun candidates on selection")
    parser.add_argument("--thermal-samples", type=pathlib.Path,
                        help="monitor samples TSV (default: main-root/build_scratch/thermal/samples.tsv)")
    args = parser.parse_args()
    paths = {arm: HERE / "perarm-m1" / "legs" / f"arm-{i}" / f"arm-{i}-cap60.csv" for i, arm in enumerate(ARMS, 1)}
    seen = set()
    for spec in args.leg:
        arm, separator, path = spec.partition("=")
        if not separator or arm not in ARMS or arm in seen or not path:
            parser.error(f"invalid or duplicate --leg {spec!r}")
        paths[arm] = pathlib.Path(path)
        seen.add(arm)
    if (args.smoke_sample is None) != (args.arms is None):
        parser.error("--smoke-sample and --arms must be supplied together")
    if args.smoke_output and args.smoke_sample is None:
        parser.error("--smoke-output requires --smoke-sample")
    chosen = tuple(args.arms.split(",")) if args.arms else ()
    if chosen and (len(chosen) not in (4, 5) or len(set(chosen)) != len(chosen) or any(a not in ARMS for a in chosen)):
        parser.error("--arms must name four or five distinct known arms")
    try:
        ids = read_ids(args.list)
        missing = [f"arm-{i} ({arm}): {paths[arm]}" for i, arm in enumerate(ARMS, 1) if not paths[arm].is_file()]
        if missing:
            raise ValueError("waiting for missing leg exports:\n  " + "\n  ".join(missing))
        legs = {arm: read_leg(paths[arm], ids) for arm in ARMS}
        ranked = rank(legs, ids)
        if seen:
            print("FIXTURE ONLY: --leg overrides substitute measurements; rankings and robustness are artificial.")
        print("Virtual best of isolated 60 s legs; maximize solved, then minimize PAR-2.")
        print("Unique among all seven: " + ", ".join(
            f"{arm}={sum(legs[arm][name][0] in SOLVED and all(legs[other][name][0] not in SOLVED for other in ARMS if other != arm) for name in ids)}"
            for arm in ARMS))
        for size in (4, 5):
            print(f"Top 10 {size}-arm portfolios:")
            for position, (solved, score, subset, unique, credit) in enumerate(ranked[size][:10], 1):
                print(f"{position:2}. solved={solved}/{len(ids)} PAR-2={score:.6f} s arms={','.join(subset)}")
                print("    unique: " + ", ".join(f"{arm}={unique[arm]}" for arm in subset))
                print("    credit: " + ", ".join(f"{arm}={credit[arm]}" for arm in subset))
        if args.robustness:
            main_root = args.list.resolve().parents[4]
            report_robustness(legs, ids, ranked,
                              args.thermal_samples or main_root / "build_scratch/thermal/samples.tsv",
                              main_root)
        if chosen:
            output = args.smoke_output or HERE / "perarm-m1" / "smoke" / f"stratified-{args.smoke_sample}.list"
            make_sample(legs, ids, chosen, args.smoke_sample, output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
