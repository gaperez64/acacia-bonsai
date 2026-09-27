#!/usr/bin/env python3
"""Annotate thermal legs; fetch inputs with scripts/acacia-evidence.py fetch --campaign ID --dest DIR."""
from __future__ import annotations
import argparse
import csv
import datetime as dt
import math
import pathlib
import runpy
import shlex
import statistics

HERE = pathlib.Path(__file__).resolve().parent
ARM_NAMES = ("real:small:backward", "real:small:forward",
             "unreal:formula:spot-guarded-sparse", "unreal:automaton:forward",
             "real:gr1:oxidd", "unreal:gr1:oxidd", "real:param-lift:oxidd")
CAP = 60
TOTAL = 1524
SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def metrics(rows: list[dict]) -> tuple[float, float, int]:
    temperatures = [float(r["pkg_c"]) for r in rows if r["pkg_c"]]
    memory = [int(r["mem_avail_mib"]) for r in rows if r["mem_avail_mib"]]
    events, seconds = 0, 0.0
    for prev, curr in zip(rows, rows[1:]):
        gap = (dt.datetime.fromisoformat(curr["time"]) - dt.datetime.fromisoformat(prev["time"])).total_seconds()
        delta = int(curr["pkg_throttle"]) - int(prev["pkg_throttle"])
        if 0 < gap <= 120 and delta >= 0:
            events += delta
            seconds += gap
    return (statistics.median(temperatures) if temperatures else float("nan"),
            events/seconds if seconds else float("nan"),
            min(memory) if memory else -1)


def read_results(path: pathlib.Path, cap: int) -> dict[str, dict[str, str]]:
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if (len(rows) != TOTAL or len({row["instance"] for row in rows}) != TOTAL or
            any(row["result"] in SOLVED and not 0 <= float(row["seconds"]) <= cap for row in rows)):
        raise ValueError(f"{path}: expected {TOTAL} distinct rows with solved times within {cap} s")
    return {row["instance"]: row for row in rows}


def near_cap(rows: dict[str, dict[str, str]], cap: int) -> list[str]:
    """TIMEOUT or a solve in the closed [0.8*cap, cap] window."""
    return [name for name, row in rows.items()
            if row["result"] == "TIMEOUT" or
            (row["result"] in SOLVED and 4 * cap / 5 <= float(row["seconds"]) <= cap)]


def coverage(rows: list[dict]) -> float:
    """Fraction of corpus row progress spanned by monitor samples."""
    positions = [int(row["rows"]) for row in rows if row["rows"].isdigit()]
    return (max(positions) - min(positions) + 1) / TOTAL if positions else 0.0


def noise_by_instance(first: pathlib.Path, second: pathlib.Path, ids: list[str]) -> dict[str, float]:
    """Absolute difference in each instance's 60 s PAR-2 contribution between B epochs."""
    a, b = read_results(first, CAP), read_results(second, CAP)
    if set(a) != set(ids) or set(b) != set(ids):
        raise ValueError("B epochs must contain exactly the selected original instance IDs")
    def contribution(row: dict[str, str]) -> float:
        return float(row["seconds"]) if row["result"] in SOLVED else 2 * CAP
    return {name: abs(contribution(a[name]) - contribution(b[name])) for name in ids}


def selection_relevant(legs: dict, ids: list[str], ranked: dict,
                       noise: dict[str, float]) -> dict[str, dict[str, set[str]]]:
    """Union candidate reasons over the three leading subsets of each size."""
    selected = {arm: {} for arm in ARM_NAMES}
    for size in (4, 5):
        for _, _, subset, _, _ in ranked[size][:3]:
            for name in ids:
                successes = [(arm, legs[arm][name][1]) for arm in subset
                             if legs[arm][name][0] in SOLVED]
                fastest = min(successes, key=lambda item: (item[1], subset.index(item[0])))[0] if successes else None
                for arm in subset:
                    result, seconds = legs[arm][name]
                    reasons = set()
                    if result in {"TIMEOUT", "RESOURCE_LIMIT", "ERROR"} and not successes:
                        reasons.add("unsolved-subset-failure")
                    if result in SOLVED:
                        if .8 * CAP <= seconds <= CAP:
                            reasons.add("near-cap")
                        if len(successes) == 1:
                            reasons.add("unique-solve")
                        elif arm == fastest:
                            next_best = min(time for other, time in successes if other != arm)
                            if next_best - seconds > noise[name]:
                                reasons.add("par2-credit")
                    if reasons:
                        selected[arm].setdefault(name, set()).update(reasons)
    return selected


def estimated_seconds(names: list[str], rows: dict[str, dict[str, str]], cap: int) -> float:
    return sum(float(rows[name]["seconds"]) if rows[name]["result"] in SOLVED else cap
               for name in names)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--main-root", required=True, type=pathlib.Path,
                        help="repository containing the frozen binaries and corpus")
    parser.add_argument("--evidence-root", required=True, type=pathlib.Path,
                        help="root of fetched per-arm and campaign archives")
    parser.add_argument("--chosen-arms", help="comma-separated chosen arms for final-run reruns")
    parser.add_argument("--final60", type=pathlib.Path)
    parser.add_argument("--final17", type=pathlib.Path)
    parser.add_argument("--obfuscated-map", type=pathlib.Path,
                        help="private TLSF source map produced by prepare-obfuscated-corpus.py")
    parser.add_argument("--obfuscated-corpus", type=pathlib.Path,
                        help="private obfuscated TLSF corpus directory")
    parser.add_argument("--leg", action="append", default=[], metavar="ARM=CSV",
                        help="override an isolated leg export (dry-run fixtures only)")
    args = parser.parse_args()
    try:
        with args.samples.open(newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        grouped = {}
        for row in rows:
            grouped.setdefault(row["leg"], []).append(row)
        measures = {leg: metrics(values) for leg, values in grouped.items() if len(values) >= 2}
        campaign = args.evidence_root / "benchmarking/gr1-par2-20260923/campaign"
        paths = {arm: campaign / "perarm-m1" / "legs" / f"arm-{i}" / f"arm-{i}-cap60.csv"
                 for i, arm in enumerate(ARM_NAMES, 1)}
        overridden = set()
        for spec in args.leg:
            arm, sep, path = spec.partition("=")
            if not sep or arm not in paths or arm in overridden or not path:
                raise ValueError(f"invalid or duplicate --leg {spec!r}")
            paths[arm] = pathlib.Path(path)
            overridden.add(arm)
        if args.chosen_arms:
            chosen = args.chosen_arms.split(",")
            if len(chosen) not in (4, 5) or len(set(chosen)) != len(chosen) or any(a not in ARM_NAMES for a in chosen):
                raise ValueError("--chosen-arms must contain four or five distinct known arms")
        clean_rates = [measures[f"arm-{i}"][1] for i, arm in enumerate(ARM_NAMES, 1)
                       if paths[arm].is_file() and f"arm-{i}" in measures and
                       coverage(grouped[f"arm-{i}"]) >= .8 and
                       math.isfinite(measures[f"arm-{i}"][1])]
        clean_median = statistics.median(clean_rates) if clean_rates else float("nan")
        threshold = 1.25 * clean_median
        args.out.mkdir(parents=True, exist_ok=True)
        # These names were emitted by the previous all-near-cap policy; leaving
        # them behind would present stale lists as current recommendations.
        for stale in args.out.glob("*-near-cap.list"):
            stale.unlink()
        for stale in args.out.glob("arm-*-selection-relevant.list"):
            stale.unlink()
        summary = ["# Thermal annotation", "",
                   f"Clean-leg median package throttle rate: {clean_median:.3f} events/s "
                   f"({len(clean_rates)} completed legs with >=80% row-progress coverage); "
                   f"anomaly threshold: {threshold:.3f} events/s.",
                   "Coverage is (last sampled row - first sampled row + 1)/1,524; it measures "
                   "the span observed, not the fraction of rows sampled individually.",
                   "", "| Run | Samples | Coverage | Median package °C | Package throttle events/s | Minimum available MiB | Thermal label |",
                   "|---|---:|---:|---:|---:|---:|---|"]
        notes = []
        commands = ["#!/bin/bash", "set -euo pipefail",
                    f"ROOT={shlex.quote(str(args.main_root))}", f"CAMPAIGN={shlex.quote(str(campaign))}",
                    'LIST="$ROOT/tests/suites/benchmarks/syntcomp26/all.list"',
                    'MAP="$ROOT/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"',
                    'CORPUS="$ROOT/tlsf-corpus"',
                    "# Commands below are prepared for review. Run one at a time only."]
        if overridden:
            notes.append("Fixture only: --leg overrides supplied. The subset counts below are artificial; no rerun commands are emitted.")
            commands.append("# FIXTURE ONLY: --leg overrides were supplied; no rerun commands emitted.")
        complete = all(path.is_file() for path in paths.values())
        candidates = None
        original_ids = None
        if complete:
            selector = runpy.run_path(str(HERE / "perarm-select.py"))
            original_ids = selector["read_ids"](args.main_root / "tests/suites/benchmarks/syntcomp26/all.list")
            legs = {arm: selector["read_leg"](path, original_ids) for arm, path in paths.items()}
            ranked = selector["rank"](legs, original_ids)
            first = campaign / "derived-60s/epoch-1/B-cap60-epoch1.csv"
            second = campaign / "derived-60s/epoch-2/B-cap60-epoch2.csv"
            noise = noise_by_instance(first, second, original_ids)
            candidates = selection_relevant(legs, original_ids, ranked, noise)
            sorted_noise = sorted(noise.values())
            notes.append(f"B epoch noise: per original instance, absolute difference between "
                         f"the two 60 s PAR-2 contributions (solve time or 120 s). "
                         f"Median={statistics.median(sorted_noise):.6f} s; "
                         f"95th percentile={sorted_noise[math.ceil(.95 * len(noise)) - 1]:.6f} s. "
                         "A non-unique fastest credit qualifies when removing that arm "
                         "increases the subset contribution by more than that instance's noise.")
            for size in (4, 5):
                notes.append(f"Top-3 {size}-arm subsets: " + "; ".join(
                    ",".join(entry[2]) for entry in ranked[size][:3]))
        else:
            missing = [f"arm-{i}" for i, arm in enumerate(ARM_NAMES, 1) if not paths[arm].is_file()]
            notes.append("Selection-relevant rerun lists deferred until all seven leg exports exist; missing " + ", ".join(missing) + ".")
        for number, arm in enumerate(ARM_NAMES, 1):
            leg = f"arm-{number}"
            temp, rate, memory = measures.get(leg, (float("nan"), float("nan"), -1))
            cov = coverage(grouped.get(leg, []))
            label = ("thermal unknown" if cov < .8 or not math.isfinite(rate) or not clean_rates
                     else "anomalous" if rate > threshold else "clean")
            summary.append(f"| {leg} | {len(grouped.get(leg, []))} | {cov:.1%} | {temp:.1f} | {rate:.3f} | {memory} | {label} |")
            print(f"{leg}: {rate:.3f} events/s, coverage={cov:.1%}, {label}")
            if candidates is None or label == "clean":
                continue
            names = [name for name in original_ids if name in candidates[arm]]
            leg_rows = read_results(paths[arm], CAP)
            estimate = estimated_seconds(names, leg_rows, CAP)
            notes.append(f"{leg}: selection-relevant set={len(names)}, estimated {estimate/3600:.2f} h "
                         f"from prior capped runtimes (upper bound {len(names)*CAP/3600:.2f} h); "
                         f"thermal label={label}.")
            print(f"{leg}: rerun set={len(names)}, estimated={estimate/3600:.2f} h, upper-bound={len(names)*CAP/3600:.2f} h")
            if not names:
                continue
            subset = args.out / f"{leg}-selection-relevant.list"
            subset.write_text("\n".join(names) + "\n")
            binary = f"build_perarm_m{2 if number == 7 else 1}/src/acacia-bonsai"
            binary_sha = ("2c29d91ae2c5f987958f47d70c8a0bc3ba5edeeae0778adf8a4f863e752e68c6"
                          if number == 7 else "22455c945576d28d86ac98a3b512327740b12f0cc3a3a6006ad1c59dfe9c9584")
            revision = ("cd69aaa46dd10ecb91a3644755454dcefe1c4037"
                        if number == 7 else "3dc9fdf38b30dbc8fc85609a32ca7280ae206529")
            out = args.out / "rerun" / leg
            if not overridden:
                commands += [f'# {leg}: {len(names)} instances; estimated {estimate/3600:.2f} h',
                         f'mkdir -p {shlex.quote(str(out))}',
                         f'printf "%s  %s\\n" "{binary_sha}" "$ROOT/{binary}" | sha256sum -c -',
                         f'python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" --bin "$ROOT/{binary}" --flags "--arms {arm}" --solver-label "{leg}-rerun" --preset otf_sparse_formula --acacia-sha {revision} --list {shlex.quote(str(subset))} --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" --caps 60 --memory-max 8G --memory-swap-max 0 --collect-rusage --output {shlex.quote(str(out / f"{leg}-rerun.tsv"))}']
        for cap, source in ((60, args.final60), (17, args.final17)):
            leg = f"obf-{cap}"
            if not source or not source.is_file():
                notes.append(f"{leg}: near-cap rerun assessment pending complete CSV.")
                continue
            final_rows = read_results(source, cap)
            temp, rate, memory = measures.get(leg, (float("nan"), float("nan"), -1))
            cov = coverage(grouped.get(leg, []))
            label = ("thermal unknown" if cov < .8 or not math.isfinite(rate) or not clean_rates
                     else "anomalous" if rate > threshold else "clean")
            summary.append(f"| {leg} | {len(grouped.get(leg, []))} | {cov:.1%} | {temp:.1f} | {rate:.3f} | {memory} | {label} |")
            print(f"{leg}: {rate:.3f} events/s, coverage={cov:.1%}, {label}")
            if label == "clean":
                continue
            if not args.chosen_arms:
                notes.append(f"{leg}: near-cap rerun command pending --chosen-arms.")
                continue
            names = near_cap(final_rows, cap)
            estimate = estimated_seconds(names, final_rows, cap)
            notes.append(f"{leg}: near-cap [0.8×{cap}, {cap}] s or TIMEOUT set={len(names)}, "
                         f"estimated {estimate/3600:.2f} h (upper bound {len(names)*cap/3600:.2f} h); "
                         f"thermal label={label}.")
            print(f"{leg}: rerun set={len(names)}, estimated={estimate/3600:.2f} h, upper-bound={len(names)*cap/3600:.2f} h")
            if not names:
                continue
            subset = args.out / f"{leg}-near-cap.list"
            subset.write_text("\n".join(names) + "\n")
            if (not args.obfuscated_map or not args.obfuscated_map.is_file() or
                    not args.obfuscated_corpus or not args.obfuscated_corpus.is_dir()):
                notes.append(f"{leg}: rerun command pending --obfuscated-map and --obfuscated-corpus.")
                continue
            out = args.out / "rerun" / leg
            if not overridden:
                commands += [f'# {leg}: {len(names)} instances; estimated {estimate/3600:.2f} h',
                         f'mkdir -p {shlex.quote(str(out))}',
                         'printf "%s  %s\\n" "65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96" "$ROOT/build_final_f74ad9d1/src/acacia-bonsai" | sha256sum -c -',
                         f'python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" --bin "$ROOT/build_final_f74ad9d1/src/acacia-bonsai" --flags "--arms {args.chosen_arms}" --solver-label "{leg}-rerun" --preset otf_sparse_formula --acacia-sha f74ad9d14bb8879550d1af0126d371ebfd4e6253 --list {shlex.quote(str(subset))} --tlsf-map {shlex.quote(str(args.obfuscated_map))} --tlsf-corpus {shlex.quote(str(args.obfuscated_corpus))} --caps {cap} --memory-max 8G --memory-swap-max 0 --collect-rusage --output {shlex.quote(str(out / f"{leg}-rerun.tsv"))}']
        (args.out / "thermal.md").write_text("\n".join(summary + [""] + notes) + "\n")
        (args.out / "rerun-commands.sh").write_text("\n".join(commands) + "\n")
        print(f"thermal groups={len(measures)} clean-leg median={clean_median:.3f} events/s, threshold={threshold:.3f} events/s")
        print(f"wrote {args.out/'thermal.md'} and {args.out/'rerun-commands.sh'} (not executed)")
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
