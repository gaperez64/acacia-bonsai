#!/usr/bin/env python3
"""Summarize one S0 raw capture without reading bulky run workspaces."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import statistics
from collections import Counter
from dataclasses import dataclass
from typing import Any


@dataclass
class Target:
    name: str
    exit_code: str
    diagnostic: dict[str, Any]
    source: pathlib.Path
    error: str | None = None

    @property
    def generalizer(self) -> dict[str, Any]:
        child = self.diagnostic.get("generalizer")
        if isinstance(child, dict):
            return child
        if self.diagnostic.get("tool") == "generalize_gr1":
            return self.diagnostic
        return {}

    @property
    def phases(self) -> dict[str, dict[str, Any]]:
        phases = self.generalizer.get("phases")
        if not isinstance(phases, dict):
            phases = self.diagnostic.get("phases", {})
        return phases if isinstance(phases, dict) else {}


def _load_object(path: pathlib.Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("top-level JSON value is not an object")
    return payload


def _manifest_path(raw: pathlib.Path, value: str, subdir: str = "") -> pathlib.Path:
    requested = pathlib.Path(value)
    candidates = [requested, raw / subdir / requested.name]
    return next((item for item in candidates if item.is_file()), candidates[-1])


def _read_targets(raw: pathlib.Path) -> list[Target]:
    manifest = raw / "invocations.tsv"
    targets: list[Target] = []
    if manifest.is_file():
        with manifest.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        for row in rows:
            name = f"{row.get('family', '?')}-n{row.get('n', '?')}"
            path = _manifest_path(raw, row.get("diagnostics", f"{name}.json"))
            try:
                targets.append(Target(
                    name, row.get("exit_code", "?"), _load_object(path), path))
            except (OSError, ValueError, json.JSONDecodeError) as error:
                targets.append(Target(
                    name, row.get("exit_code", "?"), {}, path,
                    f"{type(error).__name__}: {error}"))
        return targets

    for path in sorted(raw.glob("*.json")):
        try:
            payload = _load_object(path)
            outcome = payload.get("outcome", {})
            family = outcome.get("family") if isinstance(outcome, dict) else None
            target = outcome.get("target") if isinstance(outcome, dict) else None
            name = f"{family}-n{target}" if family and target is not None else path.stem
            targets.append(Target(name, "?", payload, path))
        except (OSError, ValueError, json.JSONDecodeError) as error:
            targets.append(Target(
                path.stem, "?", {}, path,
                f"{type(error).__name__}: {error}"))
    return targets


def _number(value: Any) -> float | None:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return float(value)
    return None


def _seconds(value: Any) -> str:
    number = _number(value)
    return "n/a" if number is None else f"{number:.3f} s"


def _bytes(value: Any) -> str:
    number = _number(value)
    if number is None:
        return "n/a"
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    for unit in units:
        if abs(number) < 1024 or unit == units[-1]:
            return f"{number:.2f} {unit}"
        number /= 1024
    return "n/a"


def _histogram(value: Any) -> str:
    if not isinstance(value, dict) or not value:
        return "none"

    def key(item: tuple[str, Any]) -> tuple[int, int | str]:
        try:
            return (0, int(item[0]))
        except (TypeError, ValueError):
            return (1, str(item[0]))

    return ", ".join(f"{name}: {count}" for name, count in sorted(value.items(), key=key))


def _triple(values: list[float]) -> str:
    if not values:
        return "n/a"
    return f"{min(values):g} / {statistics.median(values):g} / {max(values):g}"


def _phase_rows(target: Target) -> list[tuple[str, int, float]]:
    rows = []
    for name, record in target.phases.items():
        if not isinstance(record, dict):
            continue
        wall = _number(record.get("wall_s"))
        calls = record.get("calls", 0)
        rows.append((str(name), int(calls) if isinstance(calls, int) else 0,
                     wall if wall is not None else 0.0))
    return sorted(rows, key=lambda row: (-row[2], row[0]))


def _censored(target: Target) -> list[dict[str, Any]]:
    result = []
    documents = [target.diagnostic]
    child = target.generalizer
    if child is not target.diagnostic:
        documents.append(child)
    for document in documents:
        records = document.get("censored", [])
        if isinstance(records, list):
            result.extend(item for item in records if isinstance(item, dict))
    return result


def _censored_by_stage(target: Target) -> dict[str, tuple[float, set[str]]]:
    result: dict[str, tuple[float, set[str]]] = {}
    for item in _censored(target):
        stage = str(item.get("stage", "?"))
        elapsed = _number(item.get("elapsed_lower_bound_s")) or 0.0
        previous, reasons = result.get(stage, (0.0, set()))
        reasons.add(str(item.get("reason", "unknown")))
        result[stage] = (max(previous, elapsed), reasons)
    return result


def _dominant_phase_rows(target: Target) -> list[tuple[str, float, bool]]:
    values = {name: (seconds, False)
              for name, _calls, seconds in _phase_rows(target)}
    stage_bounds: dict[str, float] = {}
    for item in _censored(target):
        stage = str(item.get("stage", "?"))
        lower_bound = _number(item.get("elapsed_lower_bound_s")) or 0.0
        if item.get("kind") == "phase":
            measured, _was_censored = values.get(stage, (0.0, False))
            values[stage] = (measured + lower_bound, True)
        else:
            stage_bounds[stage] = max(
                stage_bounds.get(stage, 0.0), lower_bound)
    for stage, lower_bound in stage_bounds.items():
        measured, _was_censored = values.get(stage, (0.0, False))
        values[stage] = (max(measured, lower_bound), True)
    return sorted(
        ((name, seconds, censored)
         for name, (seconds, censored) in values.items() if seconds > 0),
        key=lambda row: (-row[1], row[0]),
    )


def _verdict(target: Target) -> str:
    outcome = target.diagnostic.get("outcome", {})
    if not isinstance(outcome, dict):
        outcome = {}
    value = outcome.get("verdict")
    if value is None:
        child_outcome = target.generalizer.get("outcome", {})
        if isinstance(child_outcome, dict):
            value = child_outcome.get("answer") or child_outcome.get("verdict")
    return str(value or target.diagnostic.get("status") or "unknown")


def _peak(target: Target) -> str:
    peak = target.diagnostic.get("cgroup_memory_peak", {})
    if not isinstance(peak, dict) or not peak.get("available"):
        return "unavailable"
    return _bytes(peak.get("bytes"))


def _distribution_lines(target: Target) -> list[str]:
    distributions = target.generalizer.get("distributions", {})
    if not isinstance(distributions, dict):
        distributions = {}
    supports = distributions.get("projected_root_support_widths", [])
    widths = [
        float(item["width"])
        for item in supports
        if isinstance(item, dict) and _number(item.get("width")) is not None
    ] if isinstance(supports, list) else []
    subsets = distributions.get("subset_reuse", [])
    subset_uses = [
        float(item["uses"])
        for item in subsets
        if isinstance(item, dict) and _number(item.get("uses")) is not None
    ] if isinstance(subsets, list) else []
    operations: Counter[str] = Counter()
    if isinstance(subsets, list):
        for item in subsets:
            by_operation = item.get("uses_by_operation", {}) if isinstance(item, dict) else {}
            if isinstance(by_operation, dict):
                for operation, count in by_operation.items():
                    if isinstance(count, int):
                        operations[str(operation)] += count
    repeated = sum(value > 1 for value in subset_uses)
    basis = distributions.get("mode_count_basis")
    mode_label = (
        "actual checker mode counts"
        if basis == "actual_checker_attempts"
        else "legacy recorded mode counts (known inaccurate)"
    )
    return [
        f"- Support width min/median/max: {_triple(widths)}",
        "- Owner-tuple arity histogram: "
        + _histogram(distributions.get("ownership_tuple_arity_histogram")),
        "- Subset reuse: "
        f"{distributions.get('distinct_subset_count', len(subset_uses))} distinct; "
        f"{repeated} reused; uses min/median/max {_triple(subset_uses)}; "
        f"operations {_histogram(dict(operations))}",
        "- Mask word-length histogram: "
        + _histogram(distributions.get(
            "uint64_variable_mask_word_length_histogram")),
        f"- {mode_label.capitalize()}: "
        + _histogram(distributions.get("mode_count_histogram")),
    ]


def _checker_lines(target: Target) -> list[str]:
    checker = target.generalizer.get("checker_stats", {})
    if not isinstance(checker, dict):
        return ["- Checker `--stats`: not recorded"]
    attempts = checker.get("attempts", [])
    present = [item for item in attempts
               if isinstance(item, dict) and item.get("stats") is not None]
    if not present:
        supported = checker.get("supported")
        reason = "unsupported" if supported is False else "no payload captured"
        return [f"- Checker `--stats`: {reason}"]
    lines = ["- Checker `--stats` payloads:"]
    for item in present:
        label = item.get("label", "attempt")
        node_cap = item.get("node_cap", "?")
        payload = json.dumps(item.get("stats"), sort_keys=True, separators=(",", ":"))
        lines.append(f"  - `{label}` (node cap {node_cap}): `{payload}`")
    return lines


def render(raw: pathlib.Path, targets: list[Target], notes: list[str]) -> str:
    legacy_modes = any(
        target.generalizer.get("distributions", {}).get("mode_count_histogram")
        and target.generalizer.get("distributions", {}).get("mode_count_basis")
        != "actual_checker_attempts"
        for target in targets
        if isinstance(target.generalizer.get("distributions", {}), dict)
    )
    lines = [
        "# S0 diagnostic summary",
        "",
        f"Source: `{raw}`",
        "",
    ]
    all_notes = list(notes)
    if legacy_modes:
        all_notes.append(
            "This capture predates the checker-boundary mode-count fix; its legacy mode "
            "histograms may include seeds and use system goal counts for environment certificates."
        )
    if all_notes:
        lines.extend(("## Notes", ""))
        lines.extend(f"- {note}" for note in all_notes)
        lines.append("")

    lines.extend((
        "## Targets",
        "",
        "| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |",
        "|---|---:|---:|---:|---:|---|",
    ))
    for target in targets:
        censored = _censored_by_stage(target)
        censor_text = "; ".join(
            f"{stage} ≥ {_seconds(value[0])}"
            for stage, value in sorted(censored.items())
        ) or "none"
        lines.append(
            f"| {target.name} | {_verdict(target)} | {target.exit_code} | "
            f"{_seconds(target.diagnostic.get('elapsed_s'))} | {_peak(target)} | "
            f"{censor_text} |"
        )
    lines.append("")

    lines.extend(("## Cross-target phase dominance", ""))
    lines.extend((
        "| Target | Dominant phase | Second | Third |",
        "|---|---|---|---|",
    ))
    for target in targets:
        wall = _number(target.diagnostic.get("elapsed_s")) or 0.0
        phases = _dominant_phase_rows(target)[:3]
        cells = []
        for name, seconds, censored in phases:
            share = seconds / wall * 100 if wall > 0 else 0.0
            bound = "≥ " if censored else ""
            cells.append(
                f"{name} — {bound}{seconds:.3f} s ({bound}{share:.1f}%)")
        cells.extend(["n/a"] * (3 - len(cells)))
        lines.append(f"| {target.name} | {' | '.join(cells)} |")
    lines.append("")

    lines.extend(("## Per-target detail", ""))
    for target in targets:
        lines.extend((f"### {target.name}", ""))
        if target.error:
            lines.extend((f"Could not read `{target.source}`: {target.error}", ""))
            continue
        lines.extend((
            f"- Verdict/exit: {_verdict(target)} / {target.exit_code}",
            f"- Total wall: {_seconds(target.diagnostic.get('elapsed_s'))}",
            f"- cgroup `memory.peak`: {_peak(target)}",
        ))
        censored = _censored_by_stage(target)
        if censored:
            lines.append("- Censored stages: " + "; ".join(
                f"{stage} ≥ {_seconds(value[0])} "
                f"({', '.join(sorted(value[1]))})"
                for stage, value in sorted(censored.items())))
        else:
            lines.append("- Censored stages: none")
        lines.extend(_checker_lines(target))
        lines.extend(_distribution_lines(target))
        lines.extend(("", "| Phase | Calls | Time | Share of total wall |",
                      "|---|---:|---:|---:|"))
        wall = _number(target.diagnostic.get("elapsed_s")) or 0.0
        for name, calls, seconds in _phase_rows(target):
            share = seconds / wall * 100 if wall > 0 else 0.0
            lines.append(f"| {name} | {calls} | {seconds:.6f} s | {share:.1f}% |")
        if not target.phases:
            lines.append("| n/a | 0 | 0.000000 s | 0.0% |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=pathlib.Path, help="S0 raw capture directory")
    parser.add_argument(
        "--output", type=pathlib.Path,
        help="report path (default: s0-summary.md beside the raw directory)",
    )
    parser.add_argument(
        "--note", action="append", default=[],
        help="add a capture-specific note to the report (repeatable)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    raw = args.raw.resolve()
    if not raw.is_dir():
        raise SystemExit(f"raw directory not found: {raw}")
    output = (args.output.resolve() if args.output is not None
              else raw.parent / "s0-summary.md")
    report = render(raw, _read_targets(raw), args.note)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(report, encoding="utf-8")
    print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
