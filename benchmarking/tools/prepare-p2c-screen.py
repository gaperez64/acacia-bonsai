#!/usr/bin/env python3
"""Prepare existing screen-runner lists from explicit P2c and verified R evidence."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def write_list(out: Path, name: str, instances: set[str]) -> None:
    if not instances or any(not item.endswith(".ltl") for item in instances):
        raise ValueError(f"invalid or empty {name} inventory")
    (out / f"{name}.list").write_text("".join(f"{item}\n" for item in sorted(instances)))


def prepare(package: Path, historical: Path, routes: list[Path],
            phases: list[Path], out: Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    targets: dict[int, set[str]] = {}
    for row in rows(package):
        if row["package"] == "P2c":
            cap = int(row["cap_s"])
            if cap in targets:
                raise ValueError(f"duplicate P2c cap {cap}")
            targets[cap] = set(json.loads(row["cases_json"]))
    if not targets or not set(targets) <= {17, 60}:
        raise ValueError("P2c targets at 17 s or 60 s are required")
    # A fresh 17 s frontier is also screened at 60 s: this is a screening
    # list, not a fabricated 60 s obstruction observation.
    if 17 in targets and 60 not in targets:
        targets[60] = set(targets[17])
    historical_successes = {row["instance"] for row in rows(historical)
                            if row["result"] == "REALIZABLE" and row["exit_code"] == "0"}
    if len(historical_successes) != 19:
        raise ValueError("historical pure R success inventory must contain exactly 19 inputs")
    successes = set(historical_successes)
    observations = []
    for path in routes:
        accepted = {row["instance"] for row in rows(path)
                    if row["route"] == "R" and row["result"] == "REALIZABLE"
                    and row["target_checks"] == "1" and row["accepted_proof_bindings"] == "1"}
        successes |= accepted
        observations.append({"path": str(path), "verified_R": sorted(accepted)})
    for root in phases:
        accepted = set()
        for directory in root.iterdir():
            if not directory.is_dir():
                continue
            instance, separator, ordinal = directory.name.rpartition("-")
            if not separator or not ordinal.isdigit() or not instance.endswith(".ltl"):
                raise ValueError(f"unexpected phase identity: {directory}")
            for path in directory.glob("*.jsonl"):
                events = [json.loads(line) for line in path.read_text().splitlines()]
                checked = any(event.get("route") == "R" and
                              event.get("event") == "check_complete" for event in events)
                bound = any(event.get("route") == "R" and
                            event.get("event") == "verification_complete" for event in events)
                successful = any(event.get("route") == "R" and
                                 event.get("event") == "terminal_result" and
                                 event.get("exit_code") == 0 for event in events)
                if checked and bound and successful:
                    accepted.add(instance)
        successes |= accepted
        observations.append({"path": str(root), "verified_R": sorted(accepted)})
    for cap, members in targets.items():
        write_list(out, f"targets-{cap}", members)
    write_list(out, "historical-R19", historical_successes)
    write_list(out, "incumbent-R", successes)
    provenance = {"package_targets": str(package), "historical_rows": str(historical),
                  "historical_R_count": len(historical_successes),
                  "incumbent_R_count": len(successes), "route_evidence": observations,
                  "target_counts": {str(cap): len(members) for cap, members in targets.items()}}
    (out / "lists-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    return provenance


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-targets", type=Path, required=True)
    parser.add_argument("--historical-r-rows", type=Path, required=True)
    parser.add_argument("--route-rows", type=Path, action="append", default=[])
    parser.add_argument("--phase-root", type=Path, action="append", default=[])
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.package_targets, args.historical_r_rows,
                             args.route_rows, args.phase_root, args.out), indent=2))


if __name__ == "__main__":
    main()
