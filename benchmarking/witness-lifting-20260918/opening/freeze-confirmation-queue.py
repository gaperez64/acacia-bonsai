#!/usr/bin/env python3
"""Freeze the W2 confirmation queue from the completed W1 discovery data.

This is selection and analysis only.  It reads the two discovery epochs and
writes one row per (cap, instance); it never launches a solver.
"""

from __future__ import annotations

import collections
import csv
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[3]
OPENING = pathlib.Path(__file__).resolve().parent
GAINS_LOSSES = OPENING / "gains-losses.tsv"
P4_LIST = ROOT / "benchmarking" / "symbolic-rows-20260917" / "targets" / "p4.list"
FAMILY_INSTANCES = ROOT / "benchmarking" / "syntcomp26-family-instances.tsv"
OUTPUT = OPENING / "confirmations-queue.tsv"

CAPS = (120, 17)
EPOCHS = (1, 2)
MIB = 1024 * 1024
MEMORY_PERCENT = 15
MEMORY_FLOOR_BYTES = 16 * MIB

CHANGED_STATUS_KINDS = {
    "gain",
    "loss",
    "verdict-conflict",
    "unsolved-kind-change",
}
TIMING_KINDS = {"faster", "slower"}
KNOWN_KINDS = CHANGED_STATUS_KINDS | TIMING_KINDS
REASON_ORDER = (
    "changed-status",
    "timing",
    "memory-increase",
    "declared-benefit",
    "near-cap-control",
)

# Roles are taken from selector.md's B1 table, not inferred from all ten lines
# in p4.list: the other four P4 entries are unaffected controls.
DECLARED_BENEFITS = {
    "SPIPureNext.ltl",
    "heim-double-x-real.ltl",
    "ordered-visits-choice-real.ltl",
    "robot_grid_pb_5_5_pe_.ltl",
    "thermostat-F-real.ltl",
    "workstation_resupply_pb_3_pe_.ltl",
}

# selector.md's original workstation holdout is pb_{1,2,4}; pb_3 is the
# known near-cap regression.  Later family points are not promoted to
# original controls merely because the full-corpus campaign covered them.
NEAR_CAP_CONTROLS = {
    "workstation_resupply_pb_1_pe_.ltl",
    "workstation_resupply_pb_2_pe_.ltl",
    "workstation_resupply_pb_3_pe_.ltl",
    "workstation_resupply_pb_4_pe_.ltl",
}

OUTPUT_COLUMNS = (
    "cap",
    "instance",
    "reason",
    "epoch_1_kind",
    "epoch_2_kind",
    "b_max_rss_epoch1_bytes",
    "s_max_rss_epoch1_bytes",
    "rss_delta_bytes",
    "rss_delta_pct",
)


def index_unique(rows, keys, source):
    """Index rows by ``keys`` and reject missing or duplicate identities."""
    indexed = {}
    for row in rows:
        identity = tuple(row.get(key, "") for key in keys)
        if any(value == "" for value in identity):
            raise ValueError(f"{source}: row has incomplete key {keys!r}: {identity!r}")
        if identity in indexed:
            raise ValueError(f"{source}: duplicate key {identity!r}")
        indexed[identity] = row
    return indexed


def parse_rss(value, source):
    """Parse an optional byte count, retaining zero as unavailable evidence."""
    if value in (None, ""):
        return None
    try:
        parsed = int(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{source}: invalid max_process_rss_bytes {value!r}") from error
    if parsed < 0:
        raise ValueError(f"{source}: negative max_process_rss_bytes {parsed}")
    return parsed or None


def format_percentage(delta, baseline):
    """Render a numeric percentage with enough precision to expose byte edges."""
    return f"{100 * delta / baseline:.9f}".rstrip("0").rstrip(".")


def rss_comparison(baseline_row, candidate_row):
    """Return displayed RSS cells, delta cells, and the inclusive increase flag."""
    baseline_cell = baseline_row.get("max_process_rss_bytes", "")
    candidate_cell = candidate_row.get("max_process_rss_bytes", "")
    baseline = parse_rss(baseline_cell, f"B {baseline_row.get('instance', '<unknown>')}")
    candidate = parse_rss(candidate_cell, f"S {candidate_row.get('instance', '<unknown>')}")
    if baseline is None or candidate is None:
        return str(baseline_cell or ""), str(candidate_cell or ""), "", "", False

    delta = candidate - baseline
    # Meet or exceed max(15% of B, 16 MiB).  Integer products avoid a float
    # rounding decision at the exact 15% boundary.
    increased = delta >= MEMORY_FLOOR_BYTES and delta * 100 >= MEMORY_PERCENT * baseline
    return (
        str(baseline_cell),
        str(candidate_cell),
        str(delta),
        format_percentage(delta, baseline),
        increased,
    )


def build_confirmation_queue(
    gains_loss_rows,
    baseline_epoch1_rows,
    candidate_epoch1_rows,
    declared_benefit_instances,
    near_cap_controls,
):
    """Build the W2 queue with no I/O and no solver execution.

    The anomaly base is the union of both gains/losses epochs at each cap.
    Epoch-1 raw rows add RSS comparisons for every instance.  Memory
    increases, declared benefit cases, and near-cap controls add queue rows;
    they never filter the anomaly union.
    """
    baseline = index_unique(
        baseline_epoch1_rows, ("cap_s", "instance"), "epoch-1 B rows"
    )
    candidate = index_unique(
        candidate_epoch1_rows, ("cap_s", "instance"), "epoch-1 S rows"
    )
    if set(baseline) != set(candidate):
        missing = sorted(set(baseline) - set(candidate))
        extra = sorted(set(candidate) - set(baseline))
        raise ValueError(
            "epoch-1 B/S instance sets differ "
            f"(missing from S={missing[:5]!r}, extra in S={extra[:5]!r})"
        )

    cap_order = tuple(dict.fromkeys(row["cap_s"] for row in baseline_epoch1_rows))
    if not cap_order:
        return []

    differences = index_unique(
        gains_loss_rows, ("cap", "epoch", "instance"), "gains-losses rows"
    )
    for (cap, epoch, instance), row in differences.items():
        if epoch not in {str(value) for value in EPOCHS}:
            raise ValueError(f"gains-losses rows: unsupported epoch {epoch!r}")
        if row.get("kind") not in KNOWN_KINDS:
            raise ValueError(
                f"gains-losses rows: unsupported kind {row.get('kind')!r} for {instance!r}"
            )
        if (cap, instance) not in baseline:
            raise ValueError(
                f"gains-losses rows: {(cap, instance)!r} absent from epoch-1 raw rows"
            )

    memory = {
        key: rss_comparison(baseline_row, candidate[key])
        for key, baseline_row in baseline.items()
    }
    queue_keys = {(cap, instance) for cap, _epoch, instance in differences}
    queue_keys.update(key for key, comparison in memory.items() if comparison[-1])

    required_instances = set(declared_benefit_instances) | set(near_cap_controls)
    for cap in cap_order:
        for instance in required_instances:
            if (cap, instance) not in baseline:
                raise ValueError(
                    f"required confirmation case {(cap, instance)!r} absent from raw rows"
                )
            queue_keys.add((cap, instance))

    cap_rank = {cap: rank for rank, cap in enumerate(cap_order)}
    rows = []
    for cap, instance in sorted(queue_keys, key=lambda key: (cap_rank[key[0]], key[1])):
        epoch_kinds = {
            epoch: differences.get((cap, str(epoch), instance), {}).get("kind", "")
            for epoch in EPOCHS
        }
        kinds = set(epoch_kinds.values()) - {""}
        reasons = set()
        if kinds & CHANGED_STATUS_KINDS:
            reasons.add("changed-status")
        if kinds & TIMING_KINDS:
            reasons.add("timing")
        b_rss, s_rss, delta, percentage, increased = memory[(cap, instance)]
        if increased:
            reasons.add("memory-increase")
        if instance in declared_benefit_instances:
            reasons.add("declared-benefit")
        if instance in near_cap_controls:
            reasons.add("near-cap-control")
        if not reasons:
            continue

        rows.append(
            {
                "cap": cap,
                "instance": instance,
                "reason": "|".join(reason for reason in REASON_ORDER if reason in reasons),
                "epoch_1_kind": epoch_kinds[1],
                "epoch_2_kind": epoch_kinds[2],
                "b_max_rss_epoch1_bytes": b_rss,
                "s_max_rss_epoch1_bytes": s_rss,
                "rss_delta_bytes": delta,
                "rss_delta_pct": percentage,
            }
        )
    return rows


def read_tsv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def raw_path(candidate, cap):
    label = f"{candidate}-cap{cap}-epoch1"
    return OPENING / f"{cap}s" / "epoch-1" / f"{label}.tsv"


def validate_documented_cases(p4_instances, family_rows):
    """Ensure the manually classified B1/holdout identities still exist."""
    missing_p4 = DECLARED_BENEFITS - p4_instances
    if missing_p4:
        raise ValueError(f"declared benefit cases absent from p4.list: {sorted(missing_p4)!r}")
    family_instances = {row["logical_instance"] for row in family_rows}
    missing_controls = NEAR_CAP_CONTROLS - family_instances
    if missing_controls:
        raise ValueError(
            "near-cap controls absent from family metadata: " f"{sorted(missing_controls)!r}"
        )


def print_summary(rows):
    for cap in CAPS:
        cap_rows = [row for row in rows if row["cap"] == str(cap)]
        counts = collections.Counter(
            reason for row in cap_rows for reason in row["reason"].split("|")
        )
        breakdown = ", ".join(
            f"{reason}={counts[reason]}" for reason in REASON_ORDER
        )
        print(f"cap={cap}: {len(cap_rows)} rows ({breakdown})")

    queued = {(row["cap"], row["instance"]) for row in rows}
    missing_controls = [
        (str(cap), instance)
        for cap in CAPS
        for instance in sorted(NEAR_CAP_CONTROLS)
        if (str(cap), instance) not in queued
    ]
    print(
        "near-cap controls: "
        + ("all four present at both caps" if not missing_controls else f"missing {missing_controls!r}")
    )
    memory_rows = [row for row in rows if "memory-increase" in row["reason"].split("|")]
    print(f"memory-increase rows: {len(memory_rows)}")


def main():
    gains_loss_rows = read_tsv(GAINS_LOSSES)
    baseline_rows = [row for cap in CAPS for row in read_tsv(raw_path("B", cap))]
    candidate_rows = [row for cap in CAPS for row in read_tsv(raw_path("S", cap))]
    p4_instances = set(P4_LIST.read_text(encoding="utf-8").split())
    family_rows = read_tsv(FAMILY_INSTANCES)
    validate_documented_cases(p4_instances, family_rows)

    rows = build_confirmation_queue(
        gains_loss_rows,
        baseline_rows,
        candidate_rows,
        DECLARED_BENEFITS,
        NEAR_CAP_CONTROLS,
    )
    with OUTPUT.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=OUTPUT_COLUMNS, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {OUTPUT}")
    print_summary(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
