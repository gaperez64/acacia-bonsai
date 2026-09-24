#!/usr/bin/env python3
"""Unseal completed obfuscated N observations and check name invariance."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
from collections import Counter

HERE = pathlib.Path(__file__).resolve().parent
SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def mapping_rows(path: pathlib.Path) -> list[dict]:
    expected = path.with_suffix(".sha256")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if expected.read_text(encoding="utf-8").split()[0] != digest:
        raise ValueError("sealed mapping hash mismatch")
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if (len(rows) != 1524 or len({r["original_id"] for r in rows}) != 1524 or
            len({r["obfuscated_id"] for r in rows}) != 1524):
        raise ValueError("sealed mapping must be a one-to-one 1,524-row mapping")
    return rows


def read_csv(path: pathlib.Path, *, delimiter: str = ",") -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter=delimiter))


def keyed(rows: list[dict[str, str]], field: str = "instance") -> dict[str, dict[str, str]]:
    result = {row[field]: row for row in rows}
    if len(result) != len(rows):
        raise ValueError(f"duplicate {field}")
    return result


def write_csv(path: pathlib.Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def route_label(directory: pathlib.Path, obfuscated_id: str, digest: str,
                result: str) -> str:
    matches = list(directory.glob(f"{obfuscated_id}.*.json"))
    matches = [p for p in matches if not p.name.endswith(".lift-evidence.json")]
    if len(matches) != 1:
        raise ValueError(f"expected one route record for {obfuscated_id}, got {len(matches)}")
    record = json.loads(matches[0].read_text(encoding="utf-8"))
    if record.get("input_sha256") != digest:
        raise ValueError(f"route hash mismatch for {obfuscated_id}")
    route = record.get("route")
    if route not in {"direct-certified", "lifted-certified", "attempted-declined"}:
        raise ValueError(f"invalid route for {obfuscated_id}: {route}")
    if route == "attempted-declined" and record.get("winner") != "fallback-pending":
        raise ValueError(f"declined route missing fallback for {obfuscated_id}")
    if route != "attempted-declined" and (record.get("winner") != "lifting" or
                                           result not in SOLVED):
        raise ValueError(f"invalid certified route for {obfuscated_id}")
    return route


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mapping", type=pathlib.Path,
                        default=HERE / "generic-selection/sealed-mapping.jsonl")
    parser.add_argument("--obfuscated-csv", required=True, type=pathlib.Path)
    parser.add_argument("--final-tsv", required=True, type=pathlib.Path)
    parser.add_argument("--route-records", required=True, type=pathlib.Path)
    parser.add_argument("--selection-tsv", required=True, type=pathlib.Path)
    parser.add_argument("--selection-list", required=True, type=pathlib.Path)
    parser.add_argument("--b-csv", required=True, type=pathlib.Path)
    parser.add_argument("--cap", required=True, type=int, choices=(17, 60))
    parser.add_argument("--variant", required=True, choices=("N-a", "N-b"))
    parser.add_argument("--out-prefix", required=True, type=pathlib.Path)
    args = parser.parse_args()
    mappings = mapping_rows(args.mapping)
    obfuscated = keyed(read_csv(args.obfuscated_csv))
    if set(obfuscated) != {r["obfuscated_id"] for r in mappings}:
        raise ValueError("final N CSV does not cover exactly the obfuscated corpus")
    final_raw = keyed(read_csv(args.final_tsv, delimiter="\t"))
    if set(final_raw) != set(obfuscated):
        raise ValueError("final raw observations differ from the obfuscated CSV")
    if any(row["solver_label"] != args.variant or float(row["cap_s"]) != args.cap
           for row in final_raw.values()):
        raise ValueError("final raw observations have the wrong variant or cap")
    if any(obfuscated[identifier]["result"] !=
           {"MEMOUT": "RESOURCE_LIMIT", "CRASH": "ERROR"}.get(row["result"], row["result"])
           for identifier, row in final_raw.items()):
        raise ValueError("final CSV results disagree with raw observations")
    b = keyed(read_csv(args.b_csv))
    if set(b) != {r["original_id"] for r in mappings}:
        raise ValueError("B CSV does not cover exactly the original corpus")
    selection = keyed(read_csv(args.selection_tsv, delimiter="\t"))
    selected = [line.strip() for line in args.selection_list.read_text().splitlines()
                if line.strip() and not line.startswith("#")]
    if set(selection) != set(selected) or len(selection) != len(selected):
        raise ValueError("selection observations differ from the admitted list")
    if any(row["solver_label"] != args.variant or float(row["cap_s"]) != args.cap
           for row in selection.values()):
        raise ValueError("selection observations have the wrong variant or cap")
    joined = []
    invariant = []
    for item in mappings:
        old, new = item["original_id"], item["obfuscated_id"]
        row = obfuscated[new]
        if row["result"] not in SOLVED | {"TIMEOUT", "ERROR", "RESOURCE_LIMIT", "UNKNOWN"}:
            raise ValueError(f"unrecognized final result: {row['result']}")
        route = route_label(args.route_records, new, item["output_sha256"], row["result"])
        joined.append({"instance": old, "result": row["result"],
                       "seconds": row["seconds"], "exit": row["exit"]})
        if old in selection:
            old_result = selection[old]["result"]
            same = (old_result == row["result"] if old_result in SOLVED or row["result"] in SOLVED
                    else True)
            invariant.append({"instance": old, "selection_result": old_result,
                              "final_result": row["result"],
                              "same_decisive_verdict": "yes" if same else "no"})
        item["final_route"] = route
    if len(invariant) != len(selected):
        raise ValueError("invariance comparison did not cover selection")
    prefix = args.out_prefix
    write_csv(prefix.with_name(prefix.name + "-N-original.csv"), joined)
    write_csv(prefix.with_name(prefix.name + "-invariance.csv"), invariant)
    conflict = [r for r in invariant if r["same_decisive_verdict"] == "no"]
    both_decisive = sum(r["selection_result"] in SOLVED and r["final_result"] in SOLVED
                        for r in invariant)
    by_original = {r["original_id"]: r for r in mappings}
    comparison = []
    for n in joined:
        old = n["instance"]
        baseline = b[old]
        bs, ns = baseline["result"] in SOLVED, n["result"] in SOLVED
        if bs and ns and baseline["result"] != n["result"]:
            change = "verdict-conflict"
        elif ns and not bs:
            change = "gain"
        elif bs and not ns:
            change = "loss"
        else:
            change = "same"
        comparison.append({"instance": old, "route": by_original[old]["final_route"],
                           "n_result": n["result"], "n_seconds": n["seconds"],
                           "b_result": baseline["result"], "b_seconds": baseline["seconds"],
                           "change": change})
    write_csv(prefix.with_name(prefix.name + "-attribution.csv"), comparison)
    changes = Counter(r["change"] for r in comparison)
    routes = Counter(r["route"] for r in comparison)
    route_solved = Counter(r["route"] for r in comparison if r["n_result"] in SOLVED)
    def score(result_column: str, seconds_column: str) -> tuple[int, float]:
        solved = [float(row[seconds_column]) for row in comparison
                  if row[result_column] in SOLVED]
        return len(solved), sum(solved) + 2 * args.cap * (len(comparison) - len(solved))
    n_score = score("n_result", "n_seconds")
    b_score = score("b_result", "b_seconds")
    lines = ["# Generic N versus B", "",
             f"Selection/final decisive compatibility: {len(invariant) - len(conflict)}/{len(invariant)} "
             f"({both_decisive} decisive in both runs)", "",
             "| Series | Solved | PAR-2 total (s) | PAR-2 mean (s) |",
             "|---|---:|---:|---:|",
             f"| N | {n_score[0]} | {n_score[1]:.6f} | {n_score[1]/len(comparison):.6f} |",
             f"| B{' (derived from 120 s)' if args.cap == 60 else ''} | "
             f"{b_score[0]} | {b_score[1]:.6f} | {b_score[1]/len(comparison):.6f} |", "",
             "| Change vs B | Inputs |", "|---|---:|"]
    lines += [f"| {label} | {changes[label]} |" for label in
              ("gain", "loss", "same", "verdict-conflict")]
    lines += ["", "| N route | Inputs | N solved | B solved on route | Gains | Losses |",
              "|---|---:|---:|---:|---:|---:|"]
    lines += [f"| {label + (' (B)' if label == 'attempted-declined' else '')} | "
              f"{routes[label]} | {route_solved[label]} | "
              f"{sum(r['route'] == label and r['b_result'] in SOLVED for r in comparison)} | "
              f"{sum(r['route'] == label and r['change'] == 'gain' for r in comparison)} | "
              f"{sum(r['route'] == label and r['change'] == 'loss' for r in comparison)} |"
              for label in ("lifted-certified", "direct-certified", "attempted-declined")]
    lines += ["", "Attempted-declined rows receive B's result. "
              "B 60 s rows, when used, are derived from 120 s observations.", ""]
    prefix.with_name(prefix.name + "-comparison.md").write_text(
        "\n".join(lines), encoding="utf-8")
    if conflict or changes["verdict-conflict"]:
        raise ValueError(f"{len(conflict)} selection/final decisive mismatches and "
                         f"{changes['verdict-conflict']} N/B verdict conflicts; inspect tables")
    print(f"joined {len(joined)} N rows; decisive compatibility "
          f"{len(invariant) - len(conflict)}/{len(invariant)}; "
          f"decisive in both {both_decisive}")


if __name__ == "__main__":
    main()
