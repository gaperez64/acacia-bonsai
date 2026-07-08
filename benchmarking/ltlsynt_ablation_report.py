#!/usr/bin/env python3
"""Classify Acacia loss/slow instances with ltlsynt ablation results."""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import Counter

from benchlib import load_meson_jsonl, realizability_from_output


DEFAULT_ABLATIONS = (
    "ltlsynt_no_decompose",
    "ltlsynt_no_bypass",
    "ltlsynt_no_obligation",
    "ltlsynt_no_specials",
)


def instance_from_name(name: str) -> str:
    tail = name.split("Acacia_Bonsai:", 1)[-1]
    return tail.split("/", 1)[1] if "/" in tail else tail


def load_config(logs_dir: pathlib.Path, config: str) -> dict[str, dict]:
    path = logs_dir / f"{config}.json"
    rows = {}
    for row in load_meson_jsonl(path):
        rows[instance_from_name(row.get("name", ""))] = row
    return rows


def solved(row: dict | None) -> bool:
    return bool(row and row.get("result") == "OK")


def duration(row: dict | None) -> float:
    if not row:
        return 0.0
    return float(row.get("duration", 0) or 0)


def feature_labels(base_ok: bool, ablation_rows: dict[str, dict | None]) -> list[str]:
    if not base_ok:
        return []
    labels = []
    for config, row in ablation_rows.items():
        if not solved(row):
            labels.append(config.removeprefix("ltlsynt_no_"))
    if not labels:
        labels.append("none_removed_feature_needed")
    return labels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", required=True, help="directory with aggregated <config>.json logs")
    parser.add_argument("--acacia", default="best_decomp_mona")
    parser.add_argument("--ltlsynt", default="ltlsynt")
    parser.add_argument("--ablation", action="append", default=[], help="ltlsynt ablation config")
    parser.add_argument("--slow-factor", type=float, default=2.0)
    parser.add_argument("--slow-min", type=float, default=0.3)
    parser.add_argument("--csv", default=None)
    args = parser.parse_args()

    logs_dir = pathlib.Path(args.logs)
    ablations = args.ablation or list(DEFAULT_ABLATIONS)
    configs = {
        args.acacia: load_config(logs_dir, args.acacia),
        args.ltlsynt: load_config(logs_dir, args.ltlsynt),
    }
    for ablation in ablations:
        configs[ablation] = load_config(logs_dir, ablation)

    rows = []
    feature_counter = Counter()
    category_counter = Counter()
    keys = sorted(set(configs[args.acacia]) & set(configs[args.ltlsynt]))
    for instance in keys:
        acacia = configs[args.acacia].get(instance)
        ltlsynt = configs[args.ltlsynt].get(instance)
        a_ok = solved(acacia)
        l_ok = solved(ltlsynt)
        a_time = duration(acacia)
        l_time = duration(ltlsynt)
        if l_ok and not a_ok:
            category = "ltlsynt_only"
        elif a_ok and not l_ok:
            category = "acacia_only"
        elif a_ok and l_ok and a_time > args.slow_factor * max(l_time, 1e-9) and a_time > args.slow_min:
            category = "acacia_slow"
        elif a_ok and l_ok:
            category = "both_ok"
        else:
            category = "neither"

        ablation_rows = {name: configs[name].get(instance) for name in ablations}
        labels = feature_labels(l_ok, ablation_rows)
        real = (
            realizability_from_output((ltlsynt or {}).get("stdout"))
            or realizability_from_output((acacia or {}).get("stdout"))
            or "UNKNOWN"
        )
        row = {
            "instance": instance,
            "category": category,
            "realizability": real,
            "acacia_result": (acacia or {}).get("result", ""),
            "acacia_time": f"{a_time:.6f}",
            "ltlsynt_result": (ltlsynt or {}).get("result", ""),
            "ltlsynt_time": f"{l_time:.6f}",
            "ltlsynt_feature_labels": ";".join(labels),
        }
        for name, ablation_row in ablation_rows.items():
            row[f"{name}_result"] = (ablation_row or {}).get("result", "")
            row[f"{name}_time"] = f"{duration(ablation_row):.6f}"
        rows.append(row)
        category_counter[category] += 1
        if category in ("ltlsynt_only", "acacia_slow"):
            for label in labels:
                feature_counter[(category, label)] += 1

    print(f"acacia={args.acacia} ltlsynt={args.ltlsynt} instances={len(rows)}")
    for category, count in category_counter.most_common():
        print(f"{category}: {count}")
    print()
    for (category, label), count in sorted(feature_counter.items()):
        print(f"{category:13s} {label:28s} {count}")

    if args.csv:
        fieldnames = list(rows[0]) if rows else []
        with pathlib.Path(args.csv).open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nwrote {len(rows)} rows to {args.csv}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
