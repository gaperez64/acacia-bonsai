#!/usr/bin/env python3
"""Run one acacia-bonsai binary over a subset of instances and record result+time.

Reusable validation workhorse for the optimize-vs-ltlsynt experiments: pick a
subset (e.g. all unrealizable loss instances) from the loss-set CSV, run a given
binary+flags on each, and emit a CSV of {instance, result, seconds, exit}.

Result is parsed from stdout (REALIZABLE / UNREALIZABLE / UNKNOWN); a wall-clock
timeout yields result=TIMEOUT.

Example:
  run-subset.py --bin ../acacia-bonsai/build_best_decomp_mona/src/acacia-bonsai \\
      --from-csv loss-set-2024_20s.csv --category acacia_slow --real unreal \\
      --flags "-U -u automaton" --timeout 25 --csv out.csv
"""
import argparse
import csv
import os
import signal
import subprocess
import sys
import time


def run_pg(cmd, timeout):
    """Run cmd in its own process group; on timeout kill the WHOLE group.

    acacia-bonsai forks real/unreal worker children; a plain timeout kills only
    the parent and orphans the workers (they keep burning CPU and corrupt later
    timings). start_new_session=True + killpg fixes that.
    """
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True, start_new_session=True)
    try:
        out, err = p.communicate(timeout=timeout)
        return out, err, p.returncode, time.time() - t0, False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            out, err = p.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            out, err = "", ""
        return out, err, 124, time.time() - t0, True


def read_part(inst_ltl):
    part = os.path.splitext(inst_ltl)[0] + ".part"
    ins = outs = ""
    for line in open(part):
        t = line.split()
        if t and t[0] == ".inputs":
            ins = ",".join(t[1:])
        elif t and t[0] == ".outputs":
            outs = ",".join(t[1:])
    return ins, outs


def parse_result(out):
    # UNREALIZABLE before REALIZABLE (substring); UNKNOWN otherwise.
    if "UNREALIZABLE" in out:
        return "UNREALIZABLE"
    if "REALIZABLE" in out:
        return "REALIZABLE"
    if "UNKNOWN" in out:
        return "UNKNOWN"
    return "?"


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True)
    p.add_argument("--instances-dir",
                   default="/home/gperez/GIT-repos/acacia-bonsai/tests/ltl/syntcomp24")
    p.add_argument("--from-csv", help="loss-set CSV to pick instances from")
    p.add_argument("--category", action="append", default=[],
                   help="filter: keep these categories (repeatable)")
    p.add_argument("--real", action="append", default=[],
                   help="filter: keep these realizability values (real/unreal)")
    p.add_argument("--list", help="alternatively, a file of instance basenames")
    p.add_argument("--flags", default="", help="extra acacia flags, e.g. '-U -u automaton'")
    p.add_argument("--mem", default="4", help="-l memory limit (GB)")
    p.add_argument("--timeout", type=float, default=25.0)
    p.add_argument("--csv", default=None)
    p.add_argument("--limit", type=int, default=0, help="cap number of instances (0=all)")
    args = p.parse_args()

    insts = []
    if args.from_csv:
        for row in csv.DictReader(open(args.from_csv)):
            if args.category and row["category"] not in args.category:
                continue
            if args.real and row["real"] not in args.real:
                continue
            insts.append(row["instance"])
    elif args.list:
        insts = [l.strip() for l in open(args.list) if l.strip()]
    else:
        sys.exit("need --from-csv or --list")
    if args.limit:
        insts = insts[:args.limit]

    extra = args.flags.split()
    rows = []
    solved = 0
    tot_time = 0.0
    print(f"# bin={args.bin}\n# flags={args.flags!r}  timeout={args.timeout}s  n={len(insts)}")
    for base in insts:
        ltl = os.path.join(args.instances_dir, base)
        if not os.path.exists(ltl):
            print(f"  {base:44s} MISSING")
            continue
        ins, outs = read_part(ltl)
        cmd = [args.bin, "-F", ltl, "-i", ins, "-o", outs, "-l", args.mem] + extra
        out, err, rc, dt, timed_out = run_pg(cmd, args.timeout)
        res = "TIMEOUT" if timed_out else parse_result(out + err)
        ok = res in ("REALIZABLE", "UNREALIZABLE")
        solved += ok
        tot_time += dt
        rows.append({"instance": base, "result": res, "seconds": round(dt, 3), "exit": rc})
        print(f"  {base:44s} {res:13s} {dt:7.2f}s")

    print(f"\nsolved {solved}/{len(rows)}   total {tot_time:.1f}s")
    if args.csv:
        with open(args.csv, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=["instance", "result", "seconds", "exit"])
            w.writeheader()
            w.writerows(rows)
        print(f"wrote {args.csv}")


if __name__ == "__main__":
    main()
