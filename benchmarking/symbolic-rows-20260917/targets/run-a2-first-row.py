#!/usr/bin/env python3
"""A2: replay the frozen P2 jobs through clean-enumerative and A1 symbolic-boolean,
   first with --stop-after-first-row, then (for newly-completing targets) full replay."""
import json
import os
import subprocess
import sys
import tempfile
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "build_check/src/acacia-spot-provider-replay"
MANIFEST = "/tmp/claude-1000/-home-gperez-GIT-repos-acacia-bonsai/c399aa67-519d-4661-830e-81f7a221320b/scratchpad/a2/manifest.json"
OUT = "/tmp/claude-1000/-home-gperez-GIT-repos-acacia-bonsai/c399aa67-519d-4661-830e-81f7a221320b/scratchpad/a2/first_row_results.tsv"
FORMULA_DIR = "/tmp/claude-1000/-home-gperez-GIT-repos-acacia-bonsai/c399aa67-519d-4661-830e-81f7a221320b/scratchpad/a2/formulas"
os.makedirs(FORMULA_DIR, exist_ok=True)

manifest = json.load(open(MANIFEST))


def run(job, mode, extra=()):
    if len(job["worker_formula"]) > 100000:  # kernel MAX_ARG_STRLEN is 128 KiB
        path = os.path.join(FORMULA_DIR, f"{job['target']}-{job['subjob']}.ltl")
        if not os.path.exists(path):
            open(path, "w").write(job["worker_formula"])
        formula_args = ["--formula-file", path]
    else:
        formula_args = ["--formula", job["worker_formula"]]
    cmd = [BIN, "--arm", "c5", *formula_args, "--k", "2",
           "--provider", "closure-buchi", "--closure-row-expansion", mode,
           "--partition", job["partition"], "--timeout-seconds", "60",
           *extra]
    started = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.time() - started
    lines = p.stdout.splitlines()
    if len(lines) < 2:
        return {"status": "DRIVER_ERROR", "reason": p.stderr[:200], "wall_s": wall}
    header = lines[0].split("\t")
    row = lines[1].split("\t")
    d = dict(zip(header, row))
    d["wall_s"] = wall
    d["exit_code"] = p.returncode
    return d


rows = []
for job in manifest:
    name = f"{job['target']}[{job['subjob']}/{job['nfiles']}]"
    for mode in ("enumerative", "symbolic-boolean"):
        d = run(job, mode, extra=["--stop-after-first-row"])
        rows.append({
            "target": name, "mode": mode,
            "status": d.get("status"), "reason": d.get("reason"),
            "row_generation_ms": d.get("row_generation_ms"),
            "closure_branches_considered": d.get("closure_branches_considered"),
            "closure_guards_generated": d.get("closure_guards_generated"),
            "boolean_conversion_calls": d.get("boolean_conversion_calls"),
            "boolean_cache_hits": d.get("boolean_cache_hits"),
            "wall_s": round(d.get("wall_s", -1), 3),
        })
        print(f"{name:55s} {mode:17s} status={d.get('status')!s:14s} "
              f"reason={d.get('reason')!s:30s} branches={d.get('closure_branches_considered')!s:>10s} "
              f"wall={d.get('wall_s',-1):.2f}s")

cols = ["target", "mode", "status", "reason", "row_generation_ms",
        "closure_branches_considered", "closure_guards_generated",
        "boolean_conversion_calls", "boolean_cache_hits", "wall_s"]
with open(OUT, "w") as f:
    f.write("\t".join(cols) + "\n")
    for r in rows:
        f.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")
print(f"\nwrote {len(rows)} rows to {OUT}")
