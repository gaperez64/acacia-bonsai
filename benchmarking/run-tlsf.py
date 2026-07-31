#!/usr/bin/env python3
"""Run an acacia-bonsai binary over TLSF files via syfco translation.

The acacia binary consumes an LTL formula plus explicit input/output AP lists.
This runner translates each TLSF file with syfco, runs the binary in its own
process group, and records result/time in CSV form.
"""

import argparse
import os
import shlex
import subprocess
import sys
import tempfile

from benchlib import parse_acacia_result, read_part, run_process_group, write_csv


def parse_tlsf_part(path):
    ins, outs = read_part(path)
    if not ins or not outs:
        raise RuntimeError(f"missing .inputs/.outputs in {path}")
    return ins, outs


def translate_tlsf(syfco, tlsf, workdir):
    ltl = os.path.join(workdir, "formula.ltl")
    part = os.path.join(workdir, "formula.part")
    with open(ltl, "w") as out:
        subprocess.run([syfco, tlsf, "-f", "ltlxba", "-m", "fully", "-pf", part],
                       stdout=out, stderr=subprocess.PIPE, text=True, check=True)
    ins, outs = parse_tlsf_part(part)
    return ltl, ins, outs


def collect_instances(args):
    if args.list:
        roots = []
        for line in open(args.list):
            path = line.strip()
            if path:
                roots.append(path)
    else:
        roots = []
        for root, _, files in os.walk(args.root):
            for name in files:
                if name.endswith(".tlsf"):
                    roots.append(os.path.join(root, name))
        roots.sort()
    if args.pattern:
        roots = [p for p in roots if args.pattern.lower() in p.lower()]
    if args.limit:
        roots = roots[:args.limit]
    return roots


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bin", required=True)
    p.add_argument("--root", default="../benchmarks/tlsf")
    p.add_argument("--list", help="file containing TLSF paths")
    p.add_argument("--pattern", help="case-insensitive path substring filter")
    p.add_argument("--flags", default="", help="extra acacia flags")
    p.add_argument("--runner-prefix", default="",
                   help="optional external wrapper, e.g. systemd-run/cgexec/timeout")
    p.add_argument("--timeout", type=float, default=25.0)
    p.add_argument("--csv")
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--syfco", default="syfco")
    args = p.parse_args()

    insts = collect_instances(args)
    if not insts:
        sys.exit("no TLSF instances selected")

    extra = shlex.split(args.flags)
    runner_prefix = shlex.split(args.runner_prefix)
    rows = []
    solved = 0
    total = 0.0
    print(f"# bin={args.bin}")
    print(f"# root={args.root} pattern={args.pattern!r} flags={args.flags!r} "
          f"timeout={args.timeout}s n={len(insts)}")
    for tlsf in insts:
        rel = os.path.relpath(tlsf, args.root)
        try:
            with tempfile.TemporaryDirectory(prefix="ab-tlsf-") as td:
                ltl, ins, outs = translate_tlsf(args.syfco, tlsf, td)
                cmd = runner_prefix + [args.bin, "-F", ltl, "-i", ins, "-o", outs] + extra
                run = run_process_group(cmd, args.timeout)
                rc = run.returncode
                dt = run.seconds
                res = "TIMEOUT" if run.timed_out else parse_acacia_result(run.stdout + run.stderr)
        except subprocess.CalledProcessError as e:
            rc = e.returncode
            dt = 0.0
            res = "TRANSLATE_ERROR"
        except Exception as e:
            rc = 125
            dt = 0.0
            res = f"ERROR:{type(e).__name__}"

        solved += res in ("REALIZABLE", "UNREALIZABLE")
        total += dt
        rows.append({"instance": rel, "result": res, "seconds": round(dt, 3), "exit": rc})
        print(f"  {rel:70s} {res:16s} {dt:7.2f}s")

    print(f"\nsolved {solved}/{len(rows)}   total {total:.1f}s")
    if args.csv:
        write_csv(args.csv, rows, ["instance", "result", "seconds", "exit"])
        print(f"wrote {args.csv}")


if __name__ == "__main__":
    main()
