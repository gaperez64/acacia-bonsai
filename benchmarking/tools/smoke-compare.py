#!/usr/bin/env python3
"""Compare smoke against legs; fetch with scripts/acacia-evidence.py fetch --campaign ID --dest DIR."""
from __future__ import annotations
import argparse
import csv
import importlib.util
import math
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("perarm_select", HERE / "perarm-select.py")
assert spec and spec.loader
select = importlib.util.module_from_spec(spec)
spec.loader.exec_module(select)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", required=True, type=pathlib.Path, help="the written smoke sample")
    parser.add_argument("--arms", required=True, help="chosen comma-separated arm list")
    parser.add_argument("--portfolio", required=True, type=pathlib.Path, help="export-cactus smoke CSV")
    parser.add_argument("--all-list", required=True, type=pathlib.Path, help="original 1,524-ID list")
    parser.add_argument("--evidence-root", required=True, type=pathlib.Path,
                        help="root of fetched per-arm archive")
    args = parser.parse_args()
    chosen = tuple(args.arms.split(","))
    if len(chosen) not in (4, 5) or len(set(chosen)) != len(chosen) or any(a not in select.ARMS for a in chosen):
        parser.error("--arms must name four or five distinct known arms")
    try:
        ids = select.read_ids(args.all_list)
        sample = [line.strip() for line in args.list.read_text().splitlines() if line.strip()]
        if not sample or len(set(sample)) != len(sample) or not set(sample) <= set(ids):
            raise ValueError("smoke list has duplicate, missing, or foreign IDs")
        legs = {}
        for arm in chosen:
            i = select.ARMS.index(arm) + 1
            path = args.evidence_root / "benchmarking/gr1-par2-20260923/campaign/perarm-m1/legs" / f"arm-{i}" / f"arm-{i}-cap60.csv"
            legs[arm] = select.read_leg(path, ids)
        actual = {}
        with args.portfolio.open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["instance", "result", "seconds", "exit"]:
                raise ValueError(f"{args.portfolio}: expected four-column cactus CSV")
            for row in reader:
                name = row["instance"]
                seconds = float(row["seconds"])
                if name in actual or row["result"] not in select.RESULTS or not math.isfinite(seconds) or not 0 <= seconds <= 60:
                    raise ValueError(f"{args.portfolio}: invalid or duplicate row {name}")
                actual[name] = (row["result"], seconds)
        if set(actual) != set(sample):
            raise ValueError(f"portfolio CSV: missing {len(set(sample)-set(actual))}, extra {len(set(actual)-set(sample))} sample IDs")
        mismatches = []
        for name in sample:
            arm = select.winner(legs, name, chosen)
            predicted = legs[arm][name] if arm else None
            verdict, seconds = actual[name]
            if predicted is None:
                if verdict in select.SOLVED:
                    mismatches.append((name, "unexpected verdict", "none", verdict, seconds))
            elif verdict != predicted[0]:
                mismatches.append((name, "verdict mismatch", predicted[0], verdict, seconds))
            elif seconds > max(2.0, 1.5 * predicted[1]):
                mismatches.append((name, "slow", f"{predicted[1]:.6f}s ({arm})", verdict, seconds))
        predicted_solved = sum(select.winner(legs, name, chosen) is not None for name in sample)
        actual_solved = sum(actual[name][0] in select.SOLVED for name in sample)
        print(f"sample={len(sample)} predicted_solved={predicted_solved} actual_solved={actual_solved} mismatches={len(mismatches)}")
        for name, reason, expected, verdict, seconds in mismatches:
            print(f"{name}\t{reason}\tpredicted={expected}\tactual={verdict}@{seconds:.6f}s")
        return 1 if mismatches else 0
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
