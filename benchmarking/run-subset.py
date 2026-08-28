#!/usr/bin/env python3
"""Run Acacia or ltlsynt over a subset of instances and record result+time.

Reusable validation workhorse for the optimize-vs-ltlsynt experiments: pick a
subset (e.g. all unrealizable loss instances) from the loss-set CSV, run a given
tool binary+flags on each, and emit a CSV of {instance, result, seconds, exit}.

Instances can be passed as converted .ltl/.part pairs.  With --tlsf-map, Acacia
uses its native TLSF frontend while ltlsynt receives syfco's unadapted formula
plus its own --semantics flag.  This is both the entrant-realistic route and the
stronger one for ltlsynt.

Results require stdout and exit-code agreement (REALIZABLE / UNREALIZABLE /
UNKNOWN); wall-clock and cgroup failures are classified before solver output.

Example:
  run-subset.py --bin ../acacia-bonsai/build_best_decomp_mona/src/acacia-bonsai \\
      --from-csv loss-set-2024_20s.csv --category acacia_slow --real unreal \\
      --flags "-u automaton" --timeout 25 --csv out.csv
"""
import argparse
import csv
import os
import pathlib
import signal
import shlex
import subprocess
import sys
import tempfile

from benchlib import (
    classify_acacia_run,
    classify_ltlsynt_run,
    read_part,
    run_process_group,
    run_systemd_scope,
    write_csv,
)
from suite_paths import load_source_map


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_MAP = ROOT / "tests/suites/benchmarks/syntcomp24/sources.tsv"


def read_ltl_partition(inst_ltl):
    return read_part(os.path.splitext(inst_ltl)[0] + ".part")


def read_instance_list(path):
    """Read a benchmark list, ignoring blank lines and manifest comments."""
    return [
        line
        for raw in open(path)
        if (line := raw.strip()) and not line.startswith("#")
    ]


def classify_run(run, tool="acacia"):
    if tool == "ltlsynt":
        return classify_ltlsynt_run(run)
    return classify_acacia_run(run)


def _parse_tlsf_semantics(semantics):
    parts = [part.strip() for part in semantics.split(",")]
    machine_models = [part for part in parts if part in ("Mealy", "Moore")]
    if len(machine_models) != 1:
        return None
    if any(part not in ("Mealy", "Moore", "Strict") for part in parts):
        return None
    return machine_models[0], "Strict" in parts


def convert_tlsf(syfco, tlsf, output_dir):
    tlsf = pathlib.Path(tlsf)
    ltl = output_dir / f"{tlsf.stem}.ltl"
    part = output_dir / f"{tlsf.stem}.part"
    try:
        semantics_run = subprocess.run(
            [syfco, "--print-semantics", str(tlsf)],
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    if semantics_run.returncode != 0:
        return None

    semantics = _parse_tlsf_semantics(semantics_run.stdout.strip())
    if semantics is None:
        return None
    machine_model, _is_strict = semantics
    # IMPORTANT: TLSF Strict semantics has no ltlsynt counterpart.  syfco's
    # ltlxba printer emits the plain assumption-implies-guarantee reading, not
    # the strict one, so ltlsynt solves a genuinely different specification
    # from Acacia on Strict instances.
    if ltl.exists() and part.exists():
        return ltl, machine_model

    ltl.unlink(missing_ok=True)
    part.unlink(missing_ok=True)

    cmd = [
        syfco,
        "--format",
        "ltlxba",
        "--mode",
        "fully",
        "--part-file",
        str(part),
        str(tlsf),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except OSError:
        return None
    if result.returncode != 0 or not part.exists():
        ltl.unlink(missing_ok=True)
        part.unlink(missing_ok=True)
        return None
    ltl.write_text(result.stdout.rstrip() + "\n")
    return ltl, machine_model


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True)
    p.add_argument("--tool", choices=("acacia", "ltlsynt"), default="acacia")
    p.add_argument("--instances-dir",
                   help="flat corpus override (disables the default source map)")
    p.add_argument(
        "--source-map",
        default=str(DEFAULT_SOURCE_MAP),
        help="suite sources.tsv used when --instances-dir is omitted",
    )
    p.add_argument("--from-csv", help="loss-set CSV to pick instances from")
    p.add_argument("--category", action="append", default=[],
                   help="filter: keep these categories (repeatable)")
    p.add_argument("--real", action="append", default=[],
                   help="filter: keep these realizability values (real/unreal)")
    p.add_argument("--list", help="alternatively, a file of instance basenames")
    p.add_argument("--flags", default="", help="extra tool flags, e.g. '-u automaton'")
    p.add_argument("--runner-prefix", default="",
                   help="optional external wrapper, e.g. systemd-run/cgexec/timeout")
    p.add_argument("--systemd-scope", action="store_true",
                   help="run each solver in a named, memory-limited user scope")
    p.add_argument("--memory-max", default="8G")
    p.add_argument("--memory-swap-max", default="0")
    p.add_argument("--timeout", type=float, default=25.0)
    p.add_argument("--csv", default=None)
    p.add_argument("--limit", type=int, default=0, help="cap number of instances (0=all)")
    p.add_argument(
        "--tlsf-map",
        help="TSV of 'instance<TAB>path.tlsf'.  With --tool acacia, feed the "
             "TLSF source with -T instead of the converted .ltl/.part pair; only "
             "this native TLSF route carries TLSF's indexed-family metadata, "
             "which the equivariant solver consumes as symmetry hints, so the "
             "two Acacia routes are not equivalent inputs.  With --tool "
             "ltlsynt, feed syfco's unadapted formula from a cached or "
             "temporary .ltl/.part pair together with ltlsynt's explicit "
             "--semantics flag.  This is both entrant-realistic and stronger "
             "than adapting the formula.",
    )
    p.add_argument("--syfco", default="syfco")
    p.add_argument("--syfco-cache", metavar="DIR",
                   help="directory for cached syfco-derived .ltl/.part pairs")
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
        insts = read_instance_list(args.list)
    else:
        sys.exit("need --from-csv or --list")
    if args.limit:
        insts = insts[:args.limit]

    extra = shlex.split(args.flags)
    runner_prefix = shlex.split(args.runner_prefix)
    source_map = None if args.instances_dir else load_source_map(pathlib.Path(args.source_map))
    tlsf_map = {}
    if args.tlsf_map:
        for raw in pathlib.Path(args.tlsf_map).read_text().splitlines():
            if not raw.strip():
                continue
            name, path = raw.split("\t")
            tlsf_map[name] = path
    temporary_cache = None
    syfco_cache = None
    if args.tool == "ltlsynt" and tlsf_map:
        if args.syfco_cache:
            syfco_cache = pathlib.Path(args.syfco_cache)
            syfco_cache.mkdir(parents=True, exist_ok=True)
        else:
            temporary_cache = tempfile.TemporaryDirectory()
            syfco_cache = pathlib.Path(temporary_cache.name)
    rows = []
    solved = 0
    tot_time = 0.0
    print(f"# bin={args.bin}\n# flags={args.flags!r}  timeout={args.timeout}s  n={len(insts)}")
    for base in insts:
        if tlsf_map:
            tlsf = tlsf_map.get(base)
            if tlsf is None or not pathlib.Path(tlsf).exists():
                print(f"  {base:44s} MISSING-TLSF")
                continue
            if args.tool == "acacia":
                cmd = runner_prefix + [args.bin, "-T", tlsf] + extra
            else:
                converted = convert_tlsf(args.syfco, tlsf, syfco_cache)
                if converted is None:
                    print(f"  {base:44s} SYFCO-FAIL")
                    rows.append({"instance": base, "result": "SYFCO-FAIL",
                                 "seconds": 0.0, "exit": -1})
                    continue
                ltl_path, machine_model = converted
                ltl = str(ltl_path)
                ins, outs = read_ltl_partition(ltl)
                cmd = runner_prefix + [
                    args.bin,
                    "--realizability",
                    "-F",
                    ltl,
                    f"--ins={ins}",
                    f"--outs={outs}",
                    f"--semantics={machine_model}",
                ] + extra
        else:
            ltl_path = (pathlib.Path(args.instances_dir) / base
                        if args.instances_dir else source_map.get(base))
            if ltl_path is None or not ltl_path.exists():
                print(f"  {base:44s} MISSING")
                continue
            ltl = str(ltl_path)
            ins, outs = read_ltl_partition(ltl)
            if args.tool == "acacia":
                cmd = runner_prefix + [args.bin, "-F", ltl, "-i", ins, "-o", outs] + extra
            else:
                cmd = runner_prefix + [
                    args.bin,
                    "--realizability",
                    "-F",
                    ltl,
                    f"--ins={ins}",
                    f"--outs={outs}",
                ] + extra
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
        res = classify_run(run, args.tool)
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
    if temporary_cache is not None:
        temporary_cache.cleanup()


if __name__ == "__main__":
    def exit_on_signal(signum, _frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, exit_on_signal)
    signal.signal(signal.SIGHUP, exit_on_signal)
    main()
