#!/usr/bin/env python3
"""Run an acacia-bonsai binary over TLSF files via syfco translation.

The acacia binary consumes an LTL formula plus explicit input/output AP lists.
This runner translates each TLSF file with syfco, runs the binary in its own
process group, and records result/time in CSV form.
"""

import argparse
import csv
import os
import signal
import subprocess
import sys
import tempfile
import time


def run_pg(cmd, timeout):
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


def parse_result(out):
    if "UNREALIZABLE" in out:
        return "UNREALIZABLE"
    if "REALIZABLE" in out:
        return "REALIZABLE"
    if "UNKNOWN" in out:
        return "UNKNOWN"
    return "?"


def parse_part(path):
    ins = outs = ""
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == ".inputs":
                ins = ",".join(parts[1:])
            elif parts[0] == ".outputs":
                outs = ",".join(parts[1:])
    if not ins or not outs:
        raise RuntimeError(f"missing .inputs/.outputs in {path}")
    return ins, outs


def translate_tlsf(syfco, tlsf, workdir):
    ltl = os.path.join(workdir, "formula.ltl")
    part = os.path.join(workdir, "formula.part")
    with open(ltl, "w") as out:
        subprocess.run([syfco, tlsf, "-f", "ltlxba", "-m", "fully", "-pf", part],
                       stdout=out, stderr=subprocess.PIPE, text=True, check=True)
    ins, outs = parse_part(part)
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
    p.add_argument("--mem", default="4", help="-l memory limit in GiB")
    p.add_argument("--timeout", type=float, default=25.0)
    p.add_argument("--csv")
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--syfco", default="syfco")
    args = p.parse_args()

    insts = collect_instances(args)
    if not insts:
        sys.exit("no TLSF instances selected")

    extra = args.flags.split()
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
                cmd = [args.bin, "-F", ltl, "-i", ins, "-o", outs, "-l", args.mem] + extra
                out, err, rc, dt, timed_out = run_pg(cmd, args.timeout)
                res = "TIMEOUT" if timed_out else parse_result(out + err)
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
        with open(args.csv, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=["instance", "result", "seconds", "exit"])
            w.writeheader()
            w.writerows(rows)
        print(f"wrote {args.csv}")


if __name__ == "__main__":
    main()
