#!/usr/bin/env python3
"""Extract and characterize the acacia-vs-ltlsynt "loss set" from meson logs.

Reads the per-configuration meson testlogs in a _bm-logs directory (one
JSON-lines file per configuration, as produced by self-benchmark.sh) and
compares one acacia configuration against ltlsynt on the shared instances.

For every instance it records:
  - solved? (result == OK) and wall-clock duration for each tool,
  - realizability (parsed from whichever tool's stdout reported it),
  - #inputs / #outputs (from the sibling .part file named in the command),
and buckets it into one of:
  ltlsynt_only  ltlsynt solves, acacia does not      <- the loss set
  acacia_only   acacia solves, ltlsynt does not
  acacia_slow   both solve but acacia > factor x slower (and > min s)
  both_ok       both solve, comparable
  neither       neither solves

Prints a category breakdown (split by realizable/unrealizable) and, with
--csv, writes the full per-instance table.

Example:
  loss-set.py --logs ../_bm-logs-top4-on-2024_20s --acacia best_decomp_mona \\
              --csv loss-set.csv
"""

import argparse
import json
import os
import sys


def instance_from_name(name):
    """The meson test name ends with '... - Acacia_Bonsai:<backend>/<base>.ltl'."""
    tail = name.split("Acacia_Bonsai:", 1)[-1]
    return tail.split("/", 1)[1] if "/" in tail else tail


def path_from_command(cmd):
    """Return the instance file path following the -F flag in the command list."""
    if not isinstance(cmd, list):
        return None
    for i, tok in enumerate(cmd):
        if tok == "-F" and i + 1 < len(cmd):
            return os.path.normpath(cmd[i + 1])
    return None


def realizability_from_stdout(stdout):
    if not stdout:
        return None
    # UNREALIZABLE contains REALIZABLE as a substring: test it first.
    if "UNREALIZABLE" in stdout:
        return "unreal"
    if "REALIZABLE" in stdout:
        return "real"
    return None


def part_counts(inst_path):
    """(#inputs, #outputs) from the sibling .part file, or (None, None)."""
    if not inst_path:
        return None, None
    part = os.path.splitext(inst_path)[0] + ".part"
    if not os.path.exists(part):
        return None, None
    nin = nout = None
    for line in open(part):
        toks = line.split()
        if toks and toks[0] == ".inputs":
            nin = len(toks) - 1
        elif toks and toks[0] == ".outputs":
            nout = len(toks) - 1
    return nin, nout


def load_config(path):
    """basename -> {result, duration, real, inst_path} for one config log."""
    out = {}
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        o = json.loads(line)
        base = instance_from_name(o.get("name", ""))
        out[base] = {
            "result": o.get("result"),
            "duration": o.get("duration", 0.0),
            "real": realizability_from_stdout(o.get("stdout", "")),
            "inst_path": path_from_command(o.get("command")),
        }
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--logs", required=True, help="_bm-logs dir with <config>.json files")
    p.add_argument("--acacia", default="best_decomp_mona", help="acacia config name")
    p.add_argument("--ltlsynt", default="ltlsynt", help="ltlsynt config name")
    p.add_argument("--slow-factor", type=float, default=2.0)
    p.add_argument("--slow-min", type=float, default=0.3,
                   help="min acacia duration (s) to count as 'slow'")
    p.add_argument("--csv", default=None, help="write full per-instance table here")
    args = p.parse_args()

    ab_path = os.path.join(args.logs, args.acacia + ".json")
    syn_path = os.path.join(args.logs, args.ltlsynt + ".json")
    for pth in (ab_path, syn_path):
        if not os.path.exists(pth):
            sys.exit(f"missing log: {pth}")

    ab = load_config(ab_path)
    syn = load_config(syn_path)
    keys = sorted(set(ab) & set(syn))

    rows = []
    for k in keys:
        a, s = ab[k], syn[k]
        a_ok = a["result"] == "OK"
        s_ok = s["result"] == "OK"
        if s_ok and not a_ok:
            cat = "ltlsynt_only"
        elif a_ok and not s_ok:
            cat = "acacia_only"
        elif a_ok and s_ok:
            if a["duration"] > args.slow_factor * max(s["duration"], 1e-9) \
               and a["duration"] > args.slow_min:
                cat = "acacia_slow"
            else:
                cat = "both_ok"
        else:
            cat = "neither"
        real = s["real"] or a["real"] or "?"
        nin, nout = part_counts(s["inst_path"] or a["inst_path"])
        rows.append({
            "instance": k, "category": cat, "real": real,
            "n_ins": nin, "n_outs": nout,
            "acacia_result": a["result"], "acacia_time": round(a["duration"], 3),
            "ltlsynt_result": s["result"], "ltlsynt_time": round(s["duration"], 3),
            "slowdown": round(a["duration"] / s["duration"], 1)
                        if (a_ok and s_ok and s["duration"] > 0) else "",
        })

    # ---- summary ----
    print(f"acacia config : {args.acacia}")
    print(f"ltlsynt config: {args.ltlsynt}")
    print(f"common instances: {len(rows)}\n")
    order = ["both_ok", "ltlsynt_only", "acacia_only", "acacia_slow", "neither"]
    print(f"{'category':<14}{'total':>7}{'real':>7}{'unreal':>8}{'?':>4}")
    for c in order:
        sub = [r for r in rows if r["category"] == c]
        rc = sum(1 for r in sub if r["real"] == "real")
        uc = sum(1 for r in sub if r["real"] == "unreal")
        qc = sum(1 for r in sub if r["real"] == "?")
        print(f"{c:<14}{len(sub):>7}{rc:>7}{uc:>8}{qc:>4}")

    loss = [r for r in rows if r["category"] == "ltlsynt_only"]
    print(f"\nLOSS SET (ltlsynt solves, acacia does not): {len(loss)}")
    print(f"  realizable: {sum(1 for r in loss if r['real']=='real')}"
          f"  unrealizable: {sum(1 for r in loss if r['real']=='unreal')}"
          f"  unknown: {sum(1 for r in loss if r['real']=='?')}")

    slow = sorted([r for r in rows if r["category"] == "acacia_slow"],
                  key=lambda r: -(r["acacia_time"] - r["ltlsynt_time"]))
    print(f"\nBoth-solved but acacia >{args.slow_factor:g}x slower: {len(slow)}. Worst 15:")
    for r in slow[:15]:
        print(f"  {r['instance'][:44]:44s} {r['real']:6s} "
              f"acacia={r['acacia_time']:7.2f}s ltlsynt={r['ltlsynt_time']:6.2f}s "
              f"({r['slowdown']}x)")

    if args.csv:
        import csv
        cols = ["instance", "category", "real", "n_ins", "n_outs",
                "acacia_result", "acacia_time", "ltlsynt_result", "ltlsynt_time",
                "slowdown"]
        with open(args.csv, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=cols)
            w.writeheader()
            w.writerows(rows)
        print(f"\nwrote {len(rows)} rows to {args.csv}")


if __name__ == "__main__":
    main()
