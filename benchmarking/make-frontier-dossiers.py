#!/usr/bin/env python3
"""Generate one theory dossier per frozen frontier family.

A dossier collects, for one family, everything already measured: the parameter
series with each solver's verdict, the solved/unsolved boundary, the worker
mechanism at that boundary, and what the forward solver did.  Sections 1-10 are
generated from committed TSVs so they cannot drift from the data; the closing
conjecture and next-theorem sections are left for a human to write, marked TODO
rather than auto-filled, because a generated conjecture would be a guess wearing
the costume of a result.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib
import re
import sys

SOLVED = {"REALIZABLE", "UNREALIZABLE"}


def read_tsv(path: pathlib.Path) -> list[dict]:
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def read_csv(path: pathlib.Path) -> list[dict]:
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


def sanitize(key: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", key).strip("-").lower()


def sort_key(row: dict):
    try:
        values = json.loads(row.get("parameter_values_json") or "{}")
    except json.JSONDecodeError:
        values = {}
    numeric = [v for v in values.values() if isinstance(v, (int, float))]
    return (numeric or [0], row["instance"])


def main(argv=None) -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    bench = root / "benchmarking"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", type=pathlib.Path,
                        default=bench / "syntcomp26-frontier-targets.tsv")
    parser.add_argument("--summary", type=pathlib.Path,
                        default=bench / "syntcomp26-coverage-summary.tsv")
    parser.add_argument("--forward", type=pathlib.Path,
                        default=bench / "_coverage26" / "F26-runs.tsv")
    parser.add_argument("--diagnostics", type=pathlib.Path,
                        default=bench / "syntcomp26-frontier-diagnostics.tsv")
    parser.add_argument("--out-dir", type=pathlib.Path, default=bench / "frontiers")
    args = parser.parse_args(argv)

    targets = read_tsv(args.targets)
    summary = {r["instance"]: r for r in read_tsv(args.summary)}
    forward = {}
    if args.forward.is_file():
        for row in read_tsv(args.forward):
            forward[row["instance"]] = (row["result"], row["seconds"])
    diags: dict[str, list[dict]] = collections.defaultdict(list)
    if args.diagnostics.is_file():
        for row in read_csv(args.diagnostics):
            diags[row.get("instance", "")].append(row)

    by_family: dict[str, list[dict]] = collections.defaultdict(list)
    for row in targets:
        by_family[row["family_key"]].append(row)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for family_key, rows in sorted(by_family.items()):
        display = rows[0]["family_display"]
        members = [r for r in summary.values() if r["family_key"] == family_key]
        members.sort(key=sort_key)

        out = [f"# Frontier dossier: `{display}`", ""]
        out.append(f"- **family key**: `{family_key}`")
        out.append(f"- **failure kind at the boundary**: {rows[0].get('failure_kind', '-')}")
        out.append(f"- **points observed in 2026**: {len(members)}")
        out.append(f"- **frozen targets from this family**: "
                   + ", ".join(f"`{r['instance']}`" for r in rows))
        out.append("")

        out.append("## Parameter series")
        out.append("")
        out.append("| parameters | B | S | F | B time |")
        out.append("|---|---|---|---|---:|")
        for m in members:
            try:
                params = json.loads(m.get("parameter_values_json") or "{}")
            except json.JSONDecodeError:
                params = {}
            label = ", ".join(f"{k}={v}" for k, v in params.items()) or m["instance"]
            f = forward.get(m["instance"], ("-", ""))
            seconds = m.get("B_seconds", "")
            seconds = f"{float(seconds):.2f}" if seconds else "-"
            out.append(f"| {label} | {m['B_result']} | {m['S_result']} | {f[0]} | {seconds} |")
        out.append("")

        solved = [m for m in members if m["P_result"] in SOLVED]
        unsolved = [m for m in members if m["P_result"] not in SOLVED]
        out.append("## Boundary")
        out.append("")
        if solved and unsolved:
            last = solved[-1]
            first = unsolved[0]
            out.append(f"- largest solved: `{last['instance']}` "
                       f"({float(last['P_seconds']):.2f} s)" if last.get("P_seconds")
                       else f"- largest solved: `{last['instance']}`")
            out.append(f"- first unsolved: `{first['instance']}` ({first['P_result']})")
        else:
            out.append("- no solved/unsolved boundary within the observed points")
        out.append("")

        out.append("## Worker mechanism at the boundary")
        out.append("")
        out.append("| target | aut states | rank coords | actions/pass | workers |")
        out.append("|---|---:|---:|---:|---:|")
        for r in rows:
            out.append(f"| `{r['instance']}` | {r['aut_states_max'] or '-'} | "
                       f"{r['numeric_rank_coordinates'] or '-'} | "
                       f"{r['actions_seen_sum'] or '-'} | {r['worker_count'] or '-'} |")
        out.append("")
        out.append("`actions/pass` is the cumulative action count the backward fixed point "
                   "processed, not the size of an action table.")
        out.append("")

        out.append("## Forward solver")
        out.append("")
        fw = [(m['instance'], forward.get(m['instance'])) for m in members]
        gained = [i for i, f in fw if f and f[0] in SOLVED
                  and summary[i]['P_result'] not in SOLVED]
        loststuff = [i for i, f in fw if (not f or f[0] not in SOLVED)
                     and summary[i]['P_result'] in SOLVED]
        out.append(f"- solves that B and S do not: {len(gained)}"
                   + (": " + ", ".join(f"`{i}`" for i in gained) if gained else ""))
        out.append(f"- fails where B or S succeed: {len(loststuff)}"
                   + (": " + ", ".join(f"`{i}`" for i in loststuff) if loststuff else ""))
        out.append("")

        out.append("## Structural conjecture")
        out.append("")
        out.append("TODO — not auto-generated. A conjecture produced from the same table it "
                   "is meant to explain would be a restatement, not a hypothesis.")
        out.append("")
        out.append("## Next theorem")
        out.append("")
        out.append("TODO.")
        out.append("")

        path = args.out_dir / f"{sanitize(family_key)}.md"
        path.write_text("\n".join(out))
        written.append(path)

    print(f"wrote {len(written)} dossiers to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
