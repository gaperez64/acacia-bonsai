#!/usr/bin/env python3
"""Rank Acacia-Bonsai benchmark configurations by PAR-2 score.

Reads the per-configuration meson testlogs in a _bm-logs directory
(one JSON-lines file per configuration) and prints a table sorted by
PAR-2 (lower is better). PAR-2 charges each OK its wall-clock duration
and each TIMEOUT twice the timeout cap.
"""

import argparse
import glob
import os
import sys

from benchlib import load_meson_jsonl


def load_config(path):
    ok = timeouts = 0
    t_ok = 0.0
    max_to = 0.0
    for obj in load_meson_jsonl(path):
        dur = obj.get("duration", 0.0)
        if obj.get("result") == "OK":
            ok += 1
            t_ok += dur
        else:
            timeouts += 1
            if dur > max_to:
                max_to = dur
    return ok, timeouts, t_ok, max_to


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "logs_dir",
        nargs="?",
        default=os.path.join(os.path.dirname(__file__), "..", "_bm-logs"),
        help="directory containing <config>.json meson testlogs "
             "(default: ../_bm-logs relative to this script)",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=None,
        help="per-instance timeout (s) used for PAR-2; "
             "if omitted, inferred as the max duration across TIMEOUT entries",
    )
    args = p.parse_args()

    files = sorted(glob.glob(os.path.join(args.logs_dir, "*.json")))
    if not files:
        sys.exit(f"no *.json found under {args.logs_dir}")

    rows = [(os.path.splitext(os.path.basename(f))[0], *load_config(f)) for f in files]

    if args.timeout is not None:
        timeout = args.timeout
    else:
        observed = [r[4] for r in rows if r[4] > 0]
        if not observed:
            sys.exit("no TIMEOUT entries found; pass --timeout explicitly")
        timeout = max(observed)

    ranked = []
    for name, ok, to, t_ok, _ in rows:
        par2 = t_ok + 2 * timeout * to
        ranked.append((name, ok, to, t_ok, par2))
    ranked.sort(key=lambda r: r[4])

    width = max(len(r[0]) for r in ranked)
    print(f"PAR-2 timeout cap: {timeout:.2f}s  ({len(ranked)} configurations)")
    print(f"{'rank':>4}  {'config':<{width}}  {'solved':>8}  {'timeouts':>8}  "
          f"{'T_OK(s)':>9}  {'PAR2(s)':>9}")
    total = ranked[0][1] + ranked[0][2]
    for i, (name, ok, to, t_ok, par2) in enumerate(ranked, start=1):
        print(f"{i:>4}  {name:<{width}}  {ok:>4}/{total:<3}  {to:>8}  "
              f"{t_ok:>9.1f}  {par2:>9.1f}")


if __name__ == "__main__":
    main()
