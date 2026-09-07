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
import json
import pathlib
import subprocess
import sys

from benchlib import campaign_scope_guard

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


@campaign_scope_guard("run-portfolio-arms")
def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=pathlib.Path,
                        help="directory holding build_<tag>/src/acacia-bonsai for tag in B,S,F")
    parser.add_argument("--manifest", type=pathlib.Path,
                        help="JSON object mapping labels to binary/flags/preset/acacia_sha records")
    parser.add_argument("--caps", default="1,5,17", help="use 17 alone for closing comparisons")
    parser.add_argument("--collect-rusage", action="store_true")
    parser.add_argument("--worker-records-dir", type=pathlib.Path)
    parser.add_argument("--list", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-map", required=True, type=pathlib.Path)
    parser.add_argument("--tlsf-corpus", required=True, type=pathlib.Path)
    parser.add_argument("--status-exceptions", type=pathlib.Path)
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--limit", type=int, help="cap instances per arm, for a validation subset")
    parser.add_argument("--arms", nargs="+", help="run only these manifest/legacy arms")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.manifest:
        manifest = json.loads(args.manifest.read_text())
        if not isinstance(manifest, dict) or any(
            not isinstance(v, dict) or not {"binary", "flags", "preset", "acacia_sha"} <= v.keys()
            for v in manifest.values()
        ):
            parser.error("manifest records require binary, flags, preset, acacia_sha")
    else:
        if args.build_dir is None:
            parser.error("--build-dir or --manifest is required")
        manifest = {name: dict(binary=str(args.build_dir / f"build_p5_{tag}" / "src" / "acacia-bonsai"),
                               flags=flags, preset=name, acacia_sha=None)
                    for name, (tag, flags) in ARMS.items()}
    selected = args.arms or sorted(manifest)
    if any(name not in manifest for name in selected):
        parser.error("unknown arm selected")

    for name in selected:
        record = manifest[name]
        flags = record["flags"]
        binary = pathlib.Path(record["binary"])
        if not binary.exists():
            print(f"FATAL: missing binary for arm {name}: {binary}", file=sys.stderr)
            return 1
        output = args.output_dir / f"{name}.tsv"
        marker = args.output_dir / f"{name}.done"
        # Always let the coverage runner validate the binary/options on resume.
        # A .done marker alone cannot establish that the current build matches.

        cmd = [
            sys.executable, str(RUNNER),
            "--bin", str(binary),
            "--solver-label", name,
            "--list", str(args.list),
            "--tlsf-map", str(args.tlsf_map),
            "--tlsf-corpus", str(args.tlsf_corpus),
            "--caps", args.caps,
            "--memory-max", args.memory_max,
            "--memory-swap-max", args.memory_swap_max,
            "--conflict-policy", "collect",
            "--flags", flags,
            "--preset", record["preset"],
            "--output", str(output),
        ]
        if record["acacia_sha"] is not None:
            cmd += ["--acacia-sha", record["acacia_sha"]]
        if args.collect_rusage:
            cmd += ["--collect-rusage"]
        if args.worker_records_dir:
            cmd += ["--worker-records-dir", str(args.worker_records_dir)]
        if args.status_exceptions:
            cmd += ["--status-exceptions", str(args.status_exceptions)]
        if args.limit:
            cmd += ["--limit", str(args.limit)]
        if args.resume and output.exists():
            cmd += ["--resume"]

        print(f"=== arm {name} ({record['preset']}, flags={flags!r}) ===", flush=True)
        result = subprocess.run(cmd)
        if result.returncode != 0:
            # The coverage runner uses 3 for collected conflicts. Stop for
            # adjudication before admitting or timing further candidates.
            print(f"FATAL: arm {name} exited {result.returncode}", file=sys.stderr)
            return result.returncode
        marker.write_text("done\n")

    print("ALL-ARMS-DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
