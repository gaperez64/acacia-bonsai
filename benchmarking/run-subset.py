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
      --flags "-u automaton" --timeout 25 --csv out.csv
"""
import argparse
import csv
import os
import shlex
import sys

from benchlib import (
    parse_acacia_result,
    read_part,
    run_process_group,
    run_systemd_scope,
    write_csv,
)


def read_ltl_partition(inst_ltl):
    return read_part(os.path.splitext(inst_ltl)[0] + ".part")


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
    p.add_argument("--flags", default="", help="extra acacia flags, e.g. '-u automaton'")
    p.add_argument("--runner-prefix", default="",
                   help="optional external wrapper, e.g. systemd-run/cgexec/timeout")
    p.add_argument("--systemd-scope", action="store_true",
                   help="run each solver in a named, memory-limited user scope")
    p.add_argument("--memory-max", default="8G")
    p.add_argument("--memory-swap-max", default="0")
    p.add_argument("--timeout", type=float, default=25.0)
    p.add_argument("--csv", default=None)
    p.add_argument("--limit", type=int, default=0, help="cap number of instances (0=all)")
    args = p.parse_args()
    if args.systemd_scope and args.runner_prefix:
        p.error("--systemd-scope and --runner-prefix are mutually exclusive")

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

    extra = shlex.split(args.flags)
    runner_prefix = shlex.split(args.runner_prefix)
    rows = []
    solved = 0
    tot_time = 0.0
    print(f"# bin={args.bin}\n# flags={args.flags!r}  timeout={args.timeout}s  n={len(insts)}")
    for base in insts:
        ltl = os.path.join(args.instances_dir, base)
        if not os.path.exists(ltl):
            print(f"  {base:44s} MISSING")
            continue
        ins, outs = read_ltl_partition(ltl)
        cmd = runner_prefix + [args.bin, "-F", ltl, "-i", ins, "-o", outs] + extra
        if args.systemd_scope:
            run = run_systemd_scope(
                cmd,
                args.timeout,
                args.memory_max,
                args.memory_swap_max,
                unit_prefix="acacia-subset",
            )
        else:
            run = run_process_group(cmd, args.timeout)
        res = "TIMEOUT" if run.timed_out else parse_acacia_result(run.stdout + run.stderr)
        ok = res in ("REALIZABLE", "UNREALIZABLE")
        solved += ok
        tot_time += run.seconds
        rows.append({"instance": base, "result": res, "seconds": round(run.seconds, 3),
                     "exit": run.returncode})
        print(f"  {base:44s} {res:13s} {run.seconds:7.2f}s")

    print(f"\nsolved {solved}/{len(rows)}   total {tot_time:.1f}s")
    if args.csv:
        write_csv(args.csv, rows, ["instance", "result", "seconds", "exit"])
        print(f"wrote {args.csv}")


if __name__ == "__main__":
    main()
