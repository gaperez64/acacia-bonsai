#!/usr/bin/env python3
"""Bucket compact Acacia diagnostic rows by the phase reached before failure."""

from __future__ import annotations

import argparse
import collections
import csv
import pathlib


FIXPOINT_CHECKPOINTS = {
    "after-action-construction",
    "solve-loop",
    "after-symmetry-diagnostics",
    "classic-after-picker",
    "classic-after-cpre",
    "cpre-after-action",
    "cpre-before-intersection",
    "equivariant-after-picker",
    "equivariant-after-cpre",
    "equivariant-after-closure",
    "equivariant-after-k-bump",
}
ACTION_CHECKPOINTS = {
    "after-translation",
    "after-input-push",
    "after-spot-fast",
    "after-preprocessing",
    # solve_game() constructs I/O actions and the input picker before its
    # first loop.  A child whose last checkpoint is here is stuck in that
    # construction, not in the fixed point.
    "before-solve",
}


def integer(row: dict[str, str], field: str) -> int:
    try:
        return int(row.get(field, "") or 0)
    except ValueError:
        return 0


def number(row: dict[str, str], field: str) -> float:
    try:
        return float(row.get(field, "") or 0.0)
    except ValueError:
        return 0.0


def fixpoint_bucket(apply_ms: float, downset_ms: float) -> str:
    largest = max(apply_ms, downset_ms)
    smallest = min(apply_ms, downset_ms)
    if largest == 0.0 or (smallest > 0.0 and largest <= 1.2 * smallest):
        return "mixed"
    if apply_ms > downset_ms:
        return "letter-loop-bound"
    return "downset-bound"


def checkpoint_phase(checkpoint: str) -> str:
    if checkpoint in FIXPOINT_CHECKPOINTS or checkpoint.startswith("equivariant-"):
        return "fixpoint-bound"
    if checkpoint in ACTION_CHECKPOINTS:
        return "action-construction-bound"
    return "translation-bound"


PHASE_RANK = {"translation-bound": 0, "action-construction-bound": 1, "fixpoint-bound": 2}


def classify_child(rows: list[dict[str, str]]) -> tuple[str, dict[str, str]]:
    """Classify one forked solver child by the deepest checkpoint it reached."""
    ranked = {"translation-bound": 0, "action-construction-bound": 1, "fixpoint-bound": 2}
    candidates = []
    for row in rows:
        phase = checkpoint_phase(row.get("checkpoint", ""))
        candidates.append(
            (
                ranked[phase],
                integer(row, "loops"),
                integer(row, "total_ms"),
                phase,
                row,
            )
        )
    if not candidates:
        return "translation-bound", {}
    _, _, _, phase, representative = max(candidates, key=lambda item: item[:3])
    return phase, representative


def child_classifications(
    rows: list[dict[str, str]],
) -> list[tuple[str, dict[str, str]]]:
    grouped: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    for row in rows:
        # Real diagnostics always include a PID.  Keeping absent PIDs together
        # makes hand-written/test progress sequences describe a single child.
        grouped[row.get("pid", "")].append(row)
    return [classify_child(child_rows) for child_rows in grouped.values()]


def classify_target(rows: list[dict[str, str]]) -> tuple[str, dict[str, str]]:
    """Use the modal child phase; break equal splits toward the deeper phase."""
    children = child_classifications(rows)
    if not children:
        return "translation-bound", {}
    counts = collections.Counter(phase for phase, _ in children)
    phase = max(PHASE_RANK, key=lambda candidate: (counts[candidate], PHASE_RANK[candidate]))
    representatives = [row for child_phase, row in children if child_phase == phase]
    representative = max(
        representatives,
        key=lambda row: (integer(row, "total_ms"), integer(row, "loops")),
    )
    return phase, representative


def summarize_fixpoint_children(
    rows: list[dict[str, str]],
) -> tuple[str, dict[str, float | int]]:
    representatives = [
        row
        for phase, row in child_classifications(rows)
        if phase == "fixpoint-bound"
    ]
    apply_ms = sum(number(row, "apply_ms") for row in representatives)
    downset_ms = sum(number(row, "downset_ms") for row in representatives)
    values: dict[str, float | int] = {
        "cpre_ms": sum(number(row, "cpre_ms") for row in representatives),
        "picker_ms": sum(number(row, "picker_ms") for row in representatives),
        "apply_ms": apply_ms,
        "downset_ms": downset_ms,
        "actions_seen": sum(integer(row, "actions_seen") for row in representatives),
        "meets_computed": sum(integer(row, "meets_computed") for row in representatives),
        "max_f_size": max(
            (integer(row, "max_f_size") for row in representatives), default=0
        ),
    }
    # Weight workers by the work they actually performed.  A modal vote would
    # let two children that finish after a few milliseconds outvote the one
    # child that spends the entire target budget in the fixed point.  A live
    # intersection has not yet charged its scoped downset timer, so its
    # checkpoint is direct evidence of a downset stall.
    unfinished_intersection = any(
        row.get("checkpoint") == "cpre-before-intersection"
        for row in representatives
    )
    bucket = (
        "downset-bound"
        if unfinished_intersection
        else fixpoint_bucket(apply_ms, downset_ms)
    )
    return bucket, values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    grouped: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    with args.csv.open(newline="") as handle:
        for row in csv.DictReader(handle):
            grouped[row["target"]].append(row)

    output_rows = []
    counts = collections.Counter()
    fixpoint_counts = collections.Counter()
    for target, rows in sorted(grouped.items()):
        phase, representative = classify_target(rows)
        subphase = ""
        detail: dict[str, float | int] = {}
        if phase == "fixpoint-bound":
            subphase, detail = summarize_fixpoint_children(rows)
            fixpoint_counts[subphase] += 1
        child_counts = collections.Counter(
            child_phase for child_phase, _ in child_classifications(rows)
        )
        counts[phase] += 1
        output_rows.append(
            {
                "target": target,
                "phase": phase,
                "fixpoint_bucket": subphase,
                "children": sum(child_counts.values()),
                "child_phases": ";".join(
                    f"{name}={child_counts[name]}"
                    for name in (
                        "translation-bound",
                        "action-construction-bound",
                        "fixpoint-bound",
                    )
                ),
                "timed_out": rows[0].get("wrapper_timed_out", ""),
                "returncode": rows[0].get("wrapper_returncode", ""),
                "representative_path": representative.get("path", ""),
                "deepest_checkpoint": representative.get("checkpoint", ""),
                "translation_pref": representative.get("translation_pref", ""),
                "translation_ms": representative.get("translation_ms", ""),
                "aut_states": representative.get("aut_states", ""),
                "aut_edges": representative.get("aut_edges", ""),
                "loops": representative.get("loops", ""),
                "cpre_ms": detail.get("cpre_ms", ""),
                "picker_ms": detail.get("picker_ms", ""),
                "apply_ms": detail.get("apply_ms", ""),
                "downset_ms": detail.get("downset_ms", ""),
                "actions_seen": detail.get("actions_seen", ""),
                "meets_computed": detail.get("meets_computed", ""),
                "max_f_size": detail.get("max_f_size", ""),
                "solve_ms": representative.get("solve_ms", ""),
                "total_ms": representative.get("total_ms", ""),
            }
        )

    print(f"targets={len(output_rows)}")
    for phase in ("translation-bound", "action-construction-bound", "fixpoint-bound"):
        print(f"{phase}={counts[phase]}")
    for bucket in ("letter-loop-bound", "downset-bound", "mixed"):
        print(f"{bucket}={fixpoint_counts[bucket]}")

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="") as handle:
            writer = csv.DictWriter(
                handle,
                fieldnames=list(output_rows[0]) if output_rows else ["target", "phase"],
                delimiter="\t",
                lineterminator="\n",
            )
            writer.writeheader()
            writer.writerows(output_rows)
        print(f"wrote {len(output_rows)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
