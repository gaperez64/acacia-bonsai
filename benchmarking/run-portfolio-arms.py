#!/usr/bin/env python3
"""Run each portfolio arm of §7.3 in isolation over the official selection.

An "arm" is one polarity/translation-preference/unreal-transform combination that
the normal portfolio would fork as a separate child.  Running each in isolation,
one process per instance, attributes every answer to a specific arm instead of
only to "the portfolio decided this" -- which is what P5A needs before any
four-arm subset can be selected, and what the recorded B/S/F campaigns cannot
answer, since they measure the full forked portfolio rather than one child.

This is a thin driver over run-syntcomp26-coverage.py, which already implements
the staged-cap protocol (1s/5s/17s), the outer cgroup, resumability and conflict
collection.  Isolating one arm is a matter of two things that script already
supports: --bin selects the backend (B, S or F build), and --flags carries the
CLI options that select exactly one child -- `-r small`, `-u formula`, and so on
(see arg_parser.hh; -T supplies inputs/outputs from the TLSF file, so no -i/-o is
needed here).  A single-arm invocation still forks exactly one child, and its
exit-code/stdout contract (0/REALIZABLE, 1/UNREALIZABLE, 2/UNKNOWN) is identical
to the full portfolio's, so classify_run in benchlib.py needs no changes.

Arms run strictly sequentially, one full staged-cap campaign at a time, per the
measurement protocol: no concurrent CPU work while a timing campaign is running.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER = ROOT / "benchmarking" / "run-syntcomp26-coverage.py"

# name -> (backend tag, extra CLI flags selecting exactly one child)
ARMS: dict[str, tuple[str, str]] = {
    "B-real-small": ("B", "-r small"),
    "B-real-any": ("B", "-r any"),
    "B-unreal-formula-small": ("B", "-u formula"),
    "B-unreal-automaton-small": ("B", "-u automaton"),
    "S-real-small": ("S", "-r small"),
    "S-real-any": ("S", "-r any"),
    "S-unreal-formula-small": ("S", "-u formula"),
    "S-unreal-automaton-small": ("S", "-u automaton"),
    "F-real-small": ("F", "-r small"),
    "F-real-any": ("F", "-r any"),
    "F-unreal-formula-small": ("F", "-u formula"),
    "F-unreal-automaton-small": ("F", "-u automaton"),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=pathlib.Path,
                        help="directory holding build_<tag>/src/acacia-bonsai for tag in B,S,F")
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-corpus", required=True, type=pathlib.Path)
    parser.add_argument("--status-exceptions", type=pathlib.Path)
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--limit", type=int, help="cap instances per arm, for a validation subset")
    parser.add_argument("--arms", nargs="+", choices=sorted(ARMS), help="run only these arms")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    selected = args.arms or sorted(ARMS)

    for name in selected:
        tag, flags = ARMS[name]
        binary = args.build_dir / f"build_p5_{tag}" / "src" / "acacia-bonsai"
        if not binary.exists():
            print(f"FATAL: missing binary for arm {name}: {binary}", file=sys.stderr)
            return 1
        output = args.output_dir / f"{name}.tsv"
        marker = args.output_dir / f"{name}.done"
        if marker.exists() and args.resume:
            print(f"skip {name}: already completed")
            continue

        cmd = [
            sys.executable, str(RUNNER),
            "--bin", str(binary),
            "--solver-label", name,
            "--list", str(args.list),
            "--tlsf-map", str(args.tlsf_map),
            "--tlsf-corpus", str(args.tlsf_corpus),
            "--caps", "1,5,17",
            "--memory-max", args.memory_max,
            "--memory-swap-max", args.memory_swap_max,
            "--conflict-policy", "collect",
            "--flags", flags,
            "--preset", name,
            "--output", str(output),
        ]
        if args.status_exceptions:
            cmd += ["--status-exceptions", str(args.status_exceptions)]
        if args.limit:
            cmd += ["--limit", str(args.limit)]
        if args.resume and output.exists():
            cmd += ["--resume"]

        print(f"=== arm {name} ({tag}, flags={flags!r}) ===", flush=True)
        result = subprocess.run(cmd)
        if result.returncode not in (0, 1):
            # 1 = conflicts collected, which is expected under --conflict-policy
            # collect; anything else is a real failure and stops the census here
            # rather than silently continuing past a broken arm.
            print(f"FATAL: arm {name} exited {result.returncode}", file=sys.stderr)
            return result.returncode
        marker.write_text("done\n")

    print("ALL-ARMS-DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
