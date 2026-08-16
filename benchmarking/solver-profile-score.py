#!/usr/bin/env python3
"""Score a G2s median-cycle summary with per-target and geometric-mean gates."""

from __future__ import annotations

import argparse
import csv
import math
import pathlib


def load_summary(path: pathlib.Path) -> dict[tuple[str, str, str], float]:
    medians: dict[tuple[str, str, str], float] = {}
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            key = (row["label"], row["suite"], row["instance"])
            if key in medians:
                raise ValueError(f"duplicate summary row: {'/'.join(key)}")
            value = float(row["median_cycles"])
            if value <= 0:
                raise ValueError(f"non-positive cycle count: {'/'.join(key)}")
            medians[key] = value
    if not medians:
        raise ValueError(f"{path}: no summary rows")
    return medians


def score(
    medians: dict[tuple[str, str, str], float],
    min_improvement: float,
    max_regression: float,
) -> tuple[bool, list[str]]:
    targets = sorted({(suite, instance) for _, suite, instance in medians})
    messages: list[str] = []
    failures: list[str] = []
    ratios: list[float] = []
    improvements: list[float] = []
    for suite, instance in targets:
        try:
            baseline = medians[("baseline", suite, instance)]
            candidate = medians[("candidate", suite, instance)]
        except KeyError as exc:
            raise ValueError(f"missing baseline/candidate pair for {suite}/{instance}") from exc
        ratio = baseline / candidate
        improvement = 100.0 * (baseline - candidate) / baseline
        ratios.append(ratio)
        improvements.append(improvement)
        messages.append(f"{suite}/{instance}: {improvement:+.2f}% cycles ratio={ratio:.5f}")
        if improvement < -max_regression:
            failures.append(
                f"{suite}/{instance} regressed {-improvement:.2f}% "
                f"(> {max_regression:.2f}%)"
            )

    geometric_ratio = math.exp(sum(math.log(ratio) for ratio in ratios) / len(ratios))
    geometric_improvement = 100.0 * (geometric_ratio - 1.0)
    best_improvement = max(improvements)
    messages.append(
        f"geometric mean ratio={geometric_ratio:.5f} "
        f"improvement={geometric_improvement:.2f}%"
    )

    if failures:
        messages.extend(f"- {failure}" for failure in failures)
        return False, messages
    if geometric_ratio >= 1.0 + min_improvement / 100.0:
        messages.append(
            f"decision=adoption-candidate: geometric improvement meets "
            f"{min_improvement:.2f}%"
        )
        return True, messages
    if best_improvement > max_regression:
        messages.append(
            "decision=proxy-pass-to-G3: no target exceeds the regression ceiling "
            f"and best target improves {best_improvement:.2f}%"
        )
        return True, messages
    messages.append(
        f"- geometric improvement {geometric_improvement:.2f}% is below "
        f"{min_improvement:.2f}% and no target improves beyond {max_regression:.2f}%"
    )
    return False, messages


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=pathlib.Path)
    parser.add_argument("--min-improvement", type=float, default=5.0)
    parser.add_argument("--max-regression", type=float, default=6.0)
    args = parser.parse_args(argv)
    try:
        passed, messages = score(
            load_summary(args.summary), args.min_improvement, args.max_regression
        )
    except (OSError, ValueError) as exc:
        print(f"GATE FAIL: {exc}")
        return 1
    for message in messages:
        print(message)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
