#!/usr/bin/env python3
"""Structural census of Acacia's LTL-to-automaton translation forms.

Runs ``acacia-automata-study --forms all`` over a suite list and collects one
row per (instance, orientation, translation form).  The question it answers is
whether preserving transition acceptance shrinks Acacia's *counting core* --
the number of automaton states that receive numeric rank coordinates, which is
``posets::vectors::bool_threshold`` and therefore the dimension of the solver's
downset vectors.

Acacia currently asks Spot for state-based Buchi (``postprocessor::BA``, which
Spot documents as implying ``SBAcc``).  The forms compared are:

    S-current  today's BA + SBAcc path
    B-native   transition-based Buchi, no SBAcc
    G-native   native generalized Buchi
    B-from-G   degeneralize(G-native)
    S-from-G   sbacc(B-from-G)

Example:

    benchmarking/translation-census.py \\
        --study build_res/src/acacia-automata-study \\
        --list tests/suites/benchmarks/syntcomp26/panel.list \\
        --source-map tests/suites/benchmarks/syntcomp26/sources.tsv \\
        --out _bm-logs/census-syntcomp26.tsv --timeout 60
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from benchlib import read_part  # noqa: E402
from suite_paths import load_source_map  # noqa: E402

ORIENTATIONS = ("real", "unreal-formula", "unreal-automaton")


def read_instance_list(path: pathlib.Path) -> list[str]:
    return [
        line
        for raw in path.read_text().splitlines()
        if (line := raw.strip()) and not line.startswith("#")
    ]


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--study", required=True, help="acacia-automata-study binary")
    p.add_argument("--list", required=True, help="suite .list of logical instance names")
    p.add_argument("--source-map", required=True, help="suite sources.tsv")
    p.add_argument("--out", required=True, help="output TSV")
    p.add_argument("--timeout", type=float, default=60.0,
                   help="per (instance, orientation) wall-clock cap, seconds")
    p.add_argument("--limit", type=int, default=0, help="cap instances (0 = all)")
    p.add_argument("--simulation-density", action="store_true",
                   help="also emit direct-simulation density columns (schema 3)")
    p.add_argument("--simulation-cap", type=int, default=400,
                   help="state cap for the simulation relation")
    args = p.parse_args()

    source_map = load_source_map(pathlib.Path(args.source_map))
    names = read_instance_list(pathlib.Path(args.list))
    if args.limit:
        names = names[: args.limit]

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    header_written = False
    timeouts = 0

    with out.open("w") as sink:
        for index, name in enumerate(names, 1):
            ltl = source_map.get(name)
            if ltl is None or not ltl.exists():
                print(f"# skip {name}: no source", file=sys.stderr)
                continue
            part = ltl.with_suffix(".part")
            if not part.exists():
                print(f"# skip {name}: no partition", file=sys.stderr)
                continue
            inputs, outputs = read_part(part)

            for orientation in ORIENTATIONS:
                cmd = [
                    args.study, "--formula", str(ltl), "--hoa", "-", "--forms", "all",
                    "--name", name, "--orientation", orientation,
                    "--realizability-simplify",
                    "--inputs", inputs, "--outputs", outputs,
                ]
                if args.simulation_density:
                    cmd += ["--simulation-density",
                            "--simulation-cap", str(args.simulation_cap)]
                if header_written:
                    cmd.append("--no-header")
                try:
                    done = subprocess.run(cmd, capture_output=True, text=True,
                                          timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    timeouts += 1
                    print(f"TIMEOUT\t{name}\t{orientation}", file=sink, flush=True)
                    continue
                if done.returncode != 0 or not done.stdout.strip():
                    print(f"ERROR\t{name}\t{orientation}", file=sink, flush=True)
                    continue
                sink.write(done.stdout)
                sink.flush()
                header_written = True
            if index % 20 == 0:
                print(f"# {index}/{len(names)}", file=sys.stderr, flush=True)

    print(f"wrote {out}  ({timeouts} timeouts)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
