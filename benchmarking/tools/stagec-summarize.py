#!/usr/bin/env python3.13
"""Summarize the frozen informal 20+20 serial sample."""
from __future__ import annotations

import collections
import csv
import json
import pathlib
import statistics

ROOT = pathlib.Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "build_scratch/stageC/informal"
rows = json.loads((SAMPLE / "results.json").read_text())
manifest = json.loads((SAMPLE / "manifest.json").read_text())
assert len(rows) == 40
# Preserve the original frozen rows. Publish a corrected reporting view in
# which an UNKNOWN verdict cannot carry a certified route label.
for row in rows:
    if (row["verdict"] not in {"REALIZABLE", "UNREALIZABLE"} or
            row.get("winner") != "lifting"):
        row["route"] = "attempted-declined"
    if "move_source" not in row:
        row["move_source"] = (
            "target_transition" if "lifted-certified" == row["route"] and
            any(value == "exact_target_transition" for value in
                row.get("predicate_arities", {}).values()) else "")
with (SAMPLE / "results.review-fixed.tsv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
    writer.writeheader()
    writer.writerows(rows)
stages = ("target_reduction", "seed_discovery", "seed_solves", "schema",
          "certificate_export", "policy_export", "policy_check", "region_check",
          "target_proof", "exact_reduction", "target_solve", "target_check",
          "direct")
lines = ["# Stage C informal serial sample", "",
         "The route ran one input at a time with a 60 s outer cap and a 59 s lift slice.",
         "The fallback was a fixed UNKNOWN stub, so these are route outcomes, not",
         "portfolio gains. Selection was the first 20 `eligible` rows of the old 72",
         "in eligibility-v3.tsv, followed by the first 20 lexically sorted parametric",
         "TLSF corpus sources outside those 72. All source bytes came from the actual",
         "TLSF files; no sibling lookup was used by production code.", "",
         f"Frozen tlsf-tools build: `{manifest['tool_build']}`. Exact code and binary",
         "SHA-256 values and the input list are in `informal/manifest.json`.", "",
         "| Group | Lifted REAL | Direct REAL | UNREAL | UNKNOWN | Median wall |",
         "|---|---:|---:|---:|---:|---:|"]
for group in ("old72", "other"):
    subset = [row for row in rows if row["group"] == group]
    decisive = collections.Counter((row["verdict"], row["route"])
                                   for row in subset if row["verdict"] in
                                   {"REALIZABLE", "UNREALIZABLE"})
    lifted = decisive[("REALIZABLE", "lifted-certified")]
    direct = decisive[("REALIZABLE", "direct-certified")]
    unreal = sum(count for (verdict, _route), count in decisive.items()
                 if verdict == "UNREALIZABLE")
    unknown = sum(row["verdict"] not in {"REALIZABLE", "UNREALIZABLE"}
                  for row in subset)
    median = statistics.median(row["elapsed_s"] for row in subset)
    lines.append(f"| {group} | {lifted} | {direct} | {unreal} | {unknown} | {median:.3f} s |")
lines.extend(("", "Completed-stage timing medians (seconds; sample count in parentheses):", "",
              "| Stage | old72 | other |", "|---|---:|---:|"))
for stage in stages:
    values = []
    for group in ("old72", "other"):
        sample = [row[f"{stage}_s"] for row in rows if row["group"] == group
                  and isinstance(row[f"{stage}_s"], (int, float))]
        values.append(f"{statistics.median(sample):.3f} ({len(sample)})" if sample else "—")
    if values != ["—", "—"]:
        lines.append(f"| {stage} | {values[0]} | {values[1]} |")
lines.extend(("", "UNKNOWN rows by last active/completed stage:", ""))
for group in ("old72", "other"):
    counter = collections.Counter(row["last_stage"] or "unreported"
                                  for row in rows if row["group"] == group and
                                  row["verdict"] not in {"REALIZABLE", "UNREALIZABLE"})
    lines.append(f"- {group}: " + ", ".join(f"{key} {count}" for key, count in
                                     sorted(counter.items())))
lines.extend(("", "The corrected `informal/results.review-fixed.tsv` labels every UNKNOWN",
              "row `attempted-declined`; the frozen raw TSV/JSON are retained.",
              "The per-row TSV and JSON contain verdict, route, seed override vectors,",
              "predicate arities, source eligibility failure, stage times, elapsed wall",
              "time, and timeout stage. A timeout can censor the active stage, so its",
              "completed-stage medians exclude that partial cost. This is development",
              "data, not a timed campaign or a holdout evaluation.", ""))
(SAMPLE.parent / "informal-summary.md").write_text("\n".join(lines))
