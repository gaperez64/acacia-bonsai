#!/usr/bin/env python3
"""Phase M0: how often does MP cube extraction succeed on the components the solver sees?

Runs ``acacia-mp-census`` over a suite list.  Each instance yields one row per
decomposed component plus a component_index=-1 row for the whole undecomposed
formula, so the census shows directly whether decomposition changes reach --
an earlier measurement on undecomposed formulas accepted 2 of 300.
"""
from __future__ import annotations
import argparse, pathlib, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from benchlib import read_part            # noqa: E402
from suite_paths import load_source_map   # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--census", required=True, help="acacia-mp-census binary")
    p.add_argument("--list", required=True)
    p.add_argument("--source-map", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--timeout", type=float, default=60.0)
    p.add_argument("--node-cap", type=int, default=200000)
    args = p.parse_args()

    smap = load_source_map(pathlib.Path(args.source_map))
    names = [l for raw in pathlib.Path(args.list).read_text().splitlines()
             if (l := raw.strip()) and not l.startswith("#")]
    out = pathlib.Path(args.out); out.parent.mkdir(parents=True, exist_ok=True)
    header = False
    with out.open("w") as sink:
        for i, name in enumerate(names, 1):
            ltl = smap.get(name)
            if ltl is None or not ltl.exists():
                continue
            part = ltl.with_suffix(".part")
            if not part.exists():
                continue
            ins, outs = read_part(part)
            cmd = [args.census, "--formula", str(ltl), "--inputs", ins,
                   "--outputs", outs, "--name", name,
                   "--node-cap", str(args.node_cap)]
            if header:
                cmd.append("--no-header")
            try:
                done = subprocess.run(cmd, capture_output=True, text=True,
                                      timeout=args.timeout)
            except subprocess.TimeoutExpired:
                print(f"TIMEOUT\t{name}", file=sink, flush=True); continue
            if done.returncode != 0 or not done.stdout.strip():
                print(f"ERROR\t{name}", file=sink, flush=True); continue
            sink.write(done.stdout); sink.flush(); header = True
            if i % 40 == 0:
                print(f"# {i}/{len(names)}", file=sys.stderr, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
