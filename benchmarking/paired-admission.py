#!/usr/bin/env python3
"""Evaluate saved paired screens against demand/sparse sprint §8; never run solvers."""

from __future__ import annotations

import argparse
from collections import Counter
import importlib.util
import json
import math
from pathlib import Path
import re
from statistics import fmean, median


SPEC = importlib.util.spec_from_file_location(
    "paired_coverage", Path(__file__).with_name("run-syntcomp26-coverage.py")
)
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)
FAILURES = coverage.NORMALIZED_RESULTS - coverage.DECISIVE_RESULTS - {"TIMEOUT", "UNKNOWN"}
MIB = 1024 * 1024
CONFIRMATION_ROUNDS = 5
UNAVAILABLE = "unavailable"
IDENTITY = ("acacia_sha", "binary_sha256", "preset", "flags", "worker_records_dir")
REGIME = ("memory_max", "memory_swap_max", "allowed_cpus", "cpu_quota", "collect_rusage")


def load_screen(args):
    """Require exactly the declared rounds, complete runner sidecars and matched IDs."""
    if args.cap != 17 or args.rounds < 1:
        raise ValueError("--cap must be 17 and --rounds must be positive (§8 uses five)")
    labels = (args.control, args.treatment)
    if len(set(labels)) != 2 or any(not re.fullmatch(r"[\w.-]+", label) for label in labels):
        raise ValueError("control and treatment must be distinct filename labels")
    targets_path = args.screen_dir / "targets.list"
    targets = coverage.read_instance_list(targets_path)
    if not targets or len(set(targets)) != len(targets):
        raise ValueError("targets.list: empty or duplicate instance IDs")
    benefits = targets if args.benefit_targets is None else coverage.read_instance_list(
        args.benefit_targets
    )
    if not benefits or len(set(benefits)) != len(benefits) or not set(benefits) <= set(targets):
        raise ValueError("benefit targets must be a nonempty, unique subset of targets.list")
    expected = {
        f"r{round_number}-{label}{suffix}"
        for round_number in range(1, args.rounds + 1) for label in labels
        for suffix in (".tsv", "-summary.tsv", "-conflicts.tsv")
    }
    actual = {p.name for p in args.screen_dir.glob("r*-*.tsv")}
    if actual != expected:
        raise ValueError(f"round files differ; missing: {sorted(expected - actual)}; "
                         f"extra: {sorted(actual - expected)}")
    inputs = [targets_path]
    if args.benefit_targets is not None:
        inputs.append(args.benefit_targets)
    manifest = args.screen_dir / "screen-manifest.tsv"
    if manifest.exists():
        inputs.append(manifest)
    sides = {label: [] for label in labels}
    screen_tag = None
    identities, regimes, sources = {}, set(), {instance: set() for instance in targets}
    for round_number in range(1, args.rounds + 1):
        for label in labels:
            raw = args.screen_dir / f"r{round_number}-{label}.tsv"
            summary = raw.with_name(f"{raw.stem}-summary.tsv")
            conflicts = raw.with_name(f"{raw.stem}-conflicts.tsv")
            observations = coverage.load_uniform_observations(
                summary, raw, targets_path, args.cap, allow_missing_memory=True,
            )
            # screen.sh records <tag>-<side>-r<N>; bind saved observations to
            # their pairing, so copied or swapped files cannot count as new runs.
            solver_label = next(iter(observations.values()))["solver_label"]
            suffix = f"-{label}-r{round_number}"
            if not solver_label.endswith(suffix) or len(solver_label) == len(suffix):
                raise ValueError(f"{raw}: solver_label {solver_label!r} must be <tag>{suffix}")
            tag = solver_label[:-len(suffix)]
            if screen_tag is not None and tag != screen_tag:
                raise ValueError(f"{raw}: solver_label {solver_label!r} has a different screen tag")
            screen_tag = tag
            inputs.extend((raw, summary, conflicts))
            done = raw.with_suffix(".tsv.done")
            if done.exists():
                if done.read_text().strip() != "0":
                    raise ValueError(f"{done}: runner did not complete successfully")
                inputs.append(done)
            for instance, row in observations.items():
                identity = tuple(row[field] for field in IDENTITY)
                if label in identities and identities[label] != identity:
                    raise ValueError(f"{raw}: changed control/treatment provenance")
                identities[label] = identity
                regimes.add(tuple(row[field] for field in REGIME))
                sources[instance].add((row["tlsf_file"], row["expectation_source"]))
                if row["timed_out"] not in {"true", "false"}:
                    raise ValueError(f"{raw}: invalid timed_out")
                if row["result"] in coverage.DECISIVE_RESULTS and row["timed_out"] == "true":
                    raise ValueError(f"{raw}: solved observation marked timed_out")
            sides[label].append(observations)
    if len(regimes) != 1 or any(len(values) != 1 for values in sources.values()):
        raise ValueError("rounds have different resource regimes or source mappings")
    return targets, set(benefits), sides, inputs


def mad(values):
    """Unscaled median absolute deviation about the control median."""
    center = median(values)
    return median(abs(value - center) for value in values)


def measured_bytes(row, column):
    value = row[column]
    if not value:
        return None
    try:
        number = int(value)
    except ValueError:
        raise ValueError(f"{row['instance']}: invalid {column}: {value!r}") from None
    if number < 0:
        raise ValueError(f"{row['instance']}: negative {column}")
    return number


def add_measurements(result, name, control, treatment):
    """Keep missing samples visible; never take a median over a partial round set."""
    complete = all(value is not None for value in control + treatment)
    result[f"control_{name}"] = control
    result[f"treatment_{name}"] = treatment
    result[f"paired_delta_{name}"] = [
        None if a is None or b is None else b - a for a, b in zip(control, treatment)
    ]
    for side, values in (("control", control), ("treatment", treatment)):
        result[f"{side}_median_{name}"] = (
            median(values) if all(value is not None for value in values) else None
        )
    result[f"control_noise_{name}"] = mad(control) if None not in control else None
    result[f"lower_rounds_{name}"] = sum(b < a for a, b in zip(control, treatment)) if complete else None
    result[f"higher_rounds_{name}"] = sum(b > a for a, b in zip(control, treatment)) if complete else None


def measure_instance(instance, control, treatment, benefit, cap):
    rounds = len(control)
    result = {"instance": instance, "benefit_target": benefit}
    for side, rows in (("control", control), ("treatment", treatment)):
        statuses = [row["result"] for row in rows]
        verdicts = sorted(set(statuses) & coverage.DECISIVE_RESULTS)
        solved = sum(status in coverage.DECISIVE_RESULTS for status in statuses)
        result.update({f"{side}_statuses": statuses, f"{side}_verdicts": verdicts,
                       f"{side}_solved_count": solved, f"{side}_solved": f"{solved}/{rounds}"})
    add_measurements(result, "seconds", *[
        [float(row["seconds"]) if row["result"] in coverage.DECISIVE_RESULTS else cap for row in rows]
        for rows in (control, treatment)
    ])
    result["faster_rounds"] = [i for i, delta in enumerate(result["paired_delta_seconds"], 1) if delta < 0]
    result["slower_rounds"] = [i for i, delta in enumerate(result["paired_delta_seconds"], 1) if delta > 0]
    for column in ("scope_memory_peak_bytes", "max_process_rss_bytes"):
        add_measurements(result, column, *[
            [measured_bytes(row, column) for row in rows] for rows in (control, treatment)
        ])
    return result


def screen_regressions(row, rounds):
    """§8.1/8.3: set losses, new failures, paired time and scope-memory regressions."""
    flags, failures, unresolved = [], [], []
    a, b = row["control_solved_count"], row["treatment_solved_count"]
    row["correctness_fail"] = len(set(row["control_verdicts"] + row["treatment_verdicts"])) > 1
    if row["correctness_fail"]:
        flags.append("REAL/UNREAL disagreement within or across sides")
        failures.append("changed verdict")
    lost_rounds = [i for i, (before, after) in enumerate(
        zip(row["control_statuses"], row["treatment_statuses"]), 1
    ) if before in coverage.DECISIVE_RESULTS and after not in coverage.DECISIVE_RESULTS]
    row["lost_verdict_rounds"] = lost_rounds
    if lost_rounds:
        flags.append(f"lost verdict in paired rounds {lost_rounds}")
    if a == rounds and b == 0:
        failures.append("unequivocal regression")
    elif a > b:
        unresolved.append("lower success rate — unresolved, blocks admission")
    elif lost_rounds:
        unresolved.append("mixed paired verdict losses — unresolved, blocks admission")
    before, after = Counter(row["control_statuses"]), Counter(row["treatment_statuses"])
    row["new_failures"] = sorted(status for status in FAILURES if after[status] > before[status])
    if row["new_failures"]:
        failures.append("new/increased failure: " + ", ".join(row["new_failures"]))
    if row["control_statuses"] != row["treatment_statuses"]:
        flags.append("changed round statuses")
    for name, enabled, floor in (
        ("seconds", a == b == rounds, 0.050),
        ("scope_memory_peak_bytes", True, 16 * MIB),
    ):
        control = row[f"control_median_{name}"]
        treatment = row[f"treatment_median_{name}"]
        noise = row[f"control_noise_{name}"]
        threshold = max(0.05 * control, floor, noise) if control is not None else None
        flag = (enabled and treatment is not None and threshold is not None
                and beyond(treatment - control, threshold))
        validated = flag and row[f"higher_rounds_{name}"] >= 4
        row[f"regression_threshold_{name}"] = threshold
        row[f"regression_flag_{name}"] = flag
        row[f"validated_regression_{name}"] = validated
        if flag:
            message = "slowdown" if name == "seconds" else "peak memory growth"
            (failures if validated else unresolved).append(
                message + (" — validated" if validated else " — flagged, not validated")
            )
    row["regression_failures"] = failures
    row["regression_unresolved"] = unresolved
    row["flags"] = flags + failures + unresolved


def at_least(value, threshold):
    # Inclusive decimal thresholds must survive float/geometric-mean roundoff.
    return value >= threshold or math.isclose(value, threshold, rel_tol=1e-12, abs_tol=1e-12)


def beyond(value, threshold):
    return value > threshold and not math.isclose(value, threshold, rel_tol=1e-12)


def runtime_benefit(row, rounds):
    """§8.2 instance: >=5%, >=50 ms, strictly beyond MAD, >=4 paired wins."""
    control, treatment = row["control_median_seconds"], row["treatment_median_seconds"]
    reduction = control - treatment
    return (row["benefit_target"] and row["control_solved_count"] == row["treatment_solved_count"] == rounds
            and at_least(reduction, 0.05 * control) and at_least(reduction, 0.050)
            and beyond(reduction, row["control_noise_seconds"]) and row["lower_rounds_seconds"] >= 4)


def geometric_mean(values):
    return math.exp(fmean(math.log(value) for value in values))


def cohort_benefit(rows, rounds):
    """§8.2 cohort: ratio <=0.95, >=4 paired wins and beyond control-cohort MAD."""
    cohort = [row for row in rows if row["benefit_target"]
              and row["control_solved_count"] == row["treatment_solved_count"] == rounds]
    result = {"targets": [row["instance"] for row in cohort], "ratio": None,
              "paired_ratios": None, "control_noise_seconds": None,
              "median_reduction_seconds": None, "faster_rounds": 0, "benefit": False}
    if not cohort or any(value <= 0 for row in cohort for side in ("control", "treatment")
                         for value in row[f"{side}_seconds"]):
        return result  # No invented epsilon for zero-duration measurements.
    ratio = geometric_mean([row["treatment_median_seconds"] / row["control_median_seconds"]
                            for row in cohort])
    control, treatment = [[geometric_mean([row[f"{side}_seconds"][i] for row in cohort])
                           for i in range(rounds)] for side in ("control", "treatment")]
    paired = [b / a for a, b in zip(control, treatment)]
    faster = sum(b < a for a, b in zip(control, treatment))
    noise = mad(control)
    reduction = median(control) - median(treatment)
    result.update(ratio=ratio, paired_ratios=paired, control_noise_seconds=noise,
                  median_reduction_seconds=reduction, faster_rounds=faster,
                  benefit=at_least(0.95, ratio) and faster >= 4 and beyond(reduction, noise))
    return result


def memory_benefit(row, unchanged_coverage, no_time_regression):
    """§8.2 memory: >=15%, >=16 MiB, beyond MAD; scope peaks only."""
    control = row["control_median_scope_memory_peak_bytes"]
    treatment = row["treatment_median_scope_memory_peak_bytes"]
    return (row["benefit_target"] and unchanged_coverage and no_time_regression
            and control is not None and treatment is not None
            and at_least(control - treatment, 0.15 * control)
            and at_least(control - treatment, 16 * MIB)
            and beyond(control - treatment, row["control_noise_scope_memory_peak_bytes"]))


def evaluate(rows, rounds):
    """§8.1 conjunction; definite failures take precedence over unresolved evidence."""
    for row in rows:
        screen_regressions(row, rounds)
    correctness = "FAIL" if any(row["correctness_fail"] for row in rows) else "PASS"
    regression = ("FAIL" if any(row["regression_failures"] for row in rows) else
                  "UNRESOLVED" if any(row["regression_unresolved"] for row in rows) else "PASS")
    unchanged = all(row["control_solved_count"] == row["treatment_solved_count"]
                    and row["control_verdicts"] == row["treatment_verdicts"] for row in rows)
    no_time_regression = not any(row["validated_regression_seconds"] for row in rows)
    benefits = []
    for row in rows:
        row["coverage_benefit"] = (row["control_solved_count"] == 0 and row["treatment_solved_count"] == rounds
                                   and regression != "FAIL" and correctness == "PASS")
        row["runtime_benefit"] = runtime_benefit(row, rounds)
        row["memory_benefit"] = memory_benefit(row, unchanged, no_time_regression)
        benefits.extend(f"{name}: {row['instance']}" for name in
                        ("coverage_benefit", "runtime_benefit", "memory_benefit") if row[name])
    cohort = cohort_benefit(rows, rounds)
    if cohort["benefit"]:
        benefits.append("runtime_cohort_benefit")
    confirmation_complete = rounds >= CONFIRMATION_ROUNDS
    improvement = ("UNRESOLVED" if not confirmation_complete else "PASS" if benefits else "FAIL")
    decision = ("REJECT" if "FAIL" in (correctness, regression) else
                "UNRESOLVED" if "UNRESOLVED" in (regression, improvement) else
                "ADMIT" if improvement == "PASS" else "REJECT")
    return {"correctness": correctness, "no_regression": regression, "improvement": improvement,
            "decision": decision, "benefits": benefits, "cohort": cohort,
            "confirmation_complete": confirmation_complete}


def display(value):
    if value is None:
        return UNAVAILABLE
    if isinstance(value, list):
        return json.dumps([UNAVAILABLE if item is None else item for item in value], ensure_ascii=False)
    if isinstance(value, bool):
        return str(value).lower()
    return str(value)


def write_reports(args, rows, result, inputs):
    """Write the complete audit, measurement definitions and all input hashes."""
    if {path.resolve() for path in inputs} & {
        (args.screen_dir / name).resolve() for name in ("admission.md", "admission.tsv")
    }:
        raise ValueError("admission outputs must be distinct from the inputs")
    hashes = [(str(path.resolve()), coverage.sha256_file(path)) for path in dict.fromkeys(inputs)]
    lines = ["# Paired admission (§8)", "", f"Decision: **{result['decision']}**", "",
             f"Screen: `{args.screen_dir.resolve()}`; control: `{args.control}`; "
             f"treatment: `{args.treatment}`; rounds: {args.rounds}; cap: {args.cap:g} s.",
             f"Benefit targets: `{args.benefit_targets or 'all targets.list IDs'}`.", "",
             "| Input | SHA-256 |", "| --- | --- |"]
    lines.extend(f"| `{path}` | `{digest}` |" for path, digest in hashes)
    lines.extend(["", "| Gate | Result |", "| --- | --- |",
                  f"| Correctness (observed verdicts) | {result['correctness']} |",
                  f"| No regression | {result['no_regression']} |",
                  f"| Improvement | {result['improvement']} |", "",
                  "Correctness here checks recorded verdict consistency; independent G0–G5 "
                  "tests and certificate checks remain external prerequisites.", "",
                  "Wall samples use decisive seconds for solved runs and the cap for every "
                  "unsolved run. Paired deltas are treatment minus control in round order. "
                  "Noise is unscaled MAD = median(abs(control sample - median(control samples))). "
                  "The same definition applies to memory and per-round control cohort geometric means.", "",
                  "Time screening uses > max(5% of control median, 0.050 s, MAD); memory screening "
                  "uses > max(5% of control median, 16 MiB, MAD). Flags supported by at least "
                  f"4/{args.rounds} paired increases are validated; other flags block as unresolved. "
                  "The prescribed §8 confirmation uses five alternating pairs. Admission requires "
                  "at least five complete pairs; benefits from shorter screens remain unconfirmed. "
                  "Execution order "
                  "is supplied by screen.sh; TSVs alone do not certify serialization.", "",
                  "Memory gates use scope_memory_peak_bytes (whole invocation). "
                  "max_process_rss_bytes is reported separately and cannot establish whole-invocation "
                  "memory improvement. Missing samples and incomplete memory medians are unavailable; "
                  "memory unavailability does not prevent a runtime/coverage benefit.", "",
                  "Runtime instance benefit requires >=5% and >=0.050 s reduction, beyond MAD, "
                  f"with >=4/{args.rounds} paired wins. Cohort benefit requires geometric-mean ratio "
                  f"of medians <=0.95, >=4/{args.rounds} paired wins and median cohort reduction beyond "
                  "control-cohort MAD. Only declared targets solved in every run on both sides enter "
                  "the cohort; a zero time makes its ratio unavailable (no pseudocount).", "",
                  "Memory benefit requires >=15% and >=16 MiB reduction beyond MAD on a declared "
                  "target, identical per-instance solve counts/verdict sets across the checked set, "
                  "and no validated time regression. Coverage benefit requires treatment N/N, "
                  "control 0/N, and zero validated losses anywhere. Paired verdict losses remain "
                  "unresolved even with equal or higher treatment solve counts. "
                  "Gains never compensate for losses.", "",
                  "Reasons:"])
    reasons = [f"{row['instance']}: {reason}" for row in rows
               for reason in row["regression_failures"] + row["regression_unresolved"]]
    reasons.extend(result["benefits"])
    if not result["confirmation_complete"]:
        reasons.append(f"Incomplete confirmation: {args.rounds}/{CONFIRMATION_ROUNDS} paired rounds; "
                       "admission requires five alternating pairs.")
    if result["improvement"] == "FAIL":
        reasons.append("No qualifying improvement.")
    if result["no_regression"] == "PASS":
        reasons.append("No validated regression under the stated tests and measurement resolution.")
    lines.extend(f"- {reason}" for reason in reasons)
    lines.extend(["", "Cohort: `" + json.dumps(result["cohort"], ensure_ascii=False) + "`", "",
                  "| Flagged instance | Control solved | Treatment solved | Flags |",
                  "| --- | --- | --- | --- |"])
    for row in rows:
        if row["flags"]:
            lines.append(f"| {row['instance']} | {row['control_solved']} | {row['treatment_solved']} "
                         f"| {'; '.join(row['flags'])} |")
    if not any(row["flags"] for row in rows):
        lines.append("| None | | | |")
    coverage.atomic_write_tsv(args.screen_dir / "admission.tsv", list(rows[0]),
                              [{key: display(value) for key, value in row.items()} for row in rows])
    (args.screen_dir / "admission.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screen-dir", required=True, type=Path)
    parser.add_argument("--control", required=True)
    parser.add_argument("--treatment", required=True)
    parser.add_argument("--rounds", required=True, type=int,
                        help="complete pairs to report; admission requires at least five")
    parser.add_argument("--benefit-targets", type=Path, help="frozen ID list (default: targets.list)")
    parser.add_argument("--cap", type=float, default=17, help="must be 17 (default: 17)")
    args = parser.parse_args(argv)
    try:
        targets, benefits, sides, inputs = load_screen(args)
        rows = [measure_instance(instance, [round_[instance] for round_ in sides[args.control]],
                                 [round_[instance] for round_ in sides[args.treatment]],
                                 instance in benefits, args.cap) for instance in targets]
        result = evaluate(rows, args.rounds)
        inputs.extend((Path(__file__), Path(coverage.__file__), Path(__file__).with_name("benchlib.py")))
        write_reports(args, rows, result, inputs)
    except (coverage.CoverageError, OSError, ValueError) as error:
        parser.error(str(error))
    print(f"{result['decision']}: wrote {args.screen_dir / 'admission.tsv'} and "
          f"{args.screen_dir / 'admission.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
