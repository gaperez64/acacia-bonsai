#!/usr/bin/env python3
"""Run Acacia or ltlsynt over a subset of instances and record result+time.

Reusable validation workhorse for the optimize-vs-ltlsynt experiments: pick a
subset (e.g. all unrealizable loss instances) from the loss-set CSV, run a given
tool binary+flags on each, and emit a CSV of {instance, result, seconds, exit}.

Instances can be passed as converted .ltl/.part pairs.  With --tlsf-map, Acacia
uses its native TLSF frontend while ltlsynt receives syfco's unadapted formula
plus its own --semantics flag.  This is both the entrant-realistic route and the
stronger one for ltlsynt.

Results use each tool's output and exit-code conventions (REALIZABLE /
UNREALIZABLE / UNKNOWN); wall-clock and cgroup failures are classified before
solver output.

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
    campaign_scope_guard,
    classify_run,
    read_part,
    run_process_group,
    run_systemd_scope,
    write_csv,
)
from suite_paths import (
    TLSF_SOURCE_MAP_HEADER,
    load_source_map,
    read_tlsf_source_entries,
)


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


def read_adhoc_tlsf_map(path, tlsf_corpus=None):
    """Read a headerless ad-hoc TLSF map."""
    # One-off campaign maps intentionally retain their historical, unvalidated
    # format; maintained suite maps use read_tlsf_source_entries instead.
    tlsf_map = {}
    for raw in pathlib.Path(path).read_text().splitlines():
        if not raw.strip():
            continue
        name, tlsf = raw.split("\t")
        if tlsf_corpus is not None:
            tlsf = str(pathlib.Path(tlsf_corpus) / tlsf)
        tlsf_map[name] = tlsf
    return tlsf_map


def read_tlsf_map(path, tlsf_corpus=None):
    """Read a validated suite map or an explicitly headerless ad-hoc map."""
    path = pathlib.Path(path)
    lines = path.read_text().splitlines()
    if not lines or lines[0] != TLSF_SOURCE_MAP_HEADER:
        return read_adhoc_tlsf_map(path, tlsf_corpus)

    tlsf_map = read_tlsf_source_entries(path)
    if tlsf_corpus is not None:
        return {
            name: str(pathlib.Path(tlsf_corpus) / source)
            for name, source in tlsf_map.items()
        }
    return tlsf_map


def build_command(tool, binary, ltl, ins, outs, semantics=None):
    """Return the argv for one tool on one .ltl/.part pair."""
    # This is the single place where each tool's CLI shape is defined.
    if tool == "acacia":
        return [binary, "-F", ltl, "-i", ins, "-o", outs]
    if tool == "acacia1x":
        return [
            binary,
            "-c",
            "BOTH",
            "-F",
            ltl,
            "--ins",
            ins,
            "--outs",
            outs,
        ]
    if tool == "ltlsynt":
        cmd = [
            binary,
            "--realizability",
            "-F",
            ltl,
            f"--ins={ins}",
            f"--outs={outs}",
        ]
        if semantics is not None:
            cmd.append(f"--semantics={semantics}")
        return cmd
    raise ValueError(f"unknown tool: {tool}")


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
    p.add_argument(
        "--tool", choices=("acacia", "acacia1x", "ltlsynt"), default="acacia"
    )
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
        help="TSV of 'instance<TAB>path.tlsf'.  Both suite tlsf-sources.tsv "
             "files with an 'instance<TAB>tlsf' header and headerless ad-hoc "
             "maps are accepted.  Relative paths are resolved from the "
             "current working directory unless --tlsf-corpus names the "
             "corpus directory produced by benchmarking/syntcomp-corpus.py "
             "materialize.  With --tool acacia, feed the "
             "TLSF source with -T instead of the converted .ltl/.part pair; only "
             "this native TLSF route carries TLSF's indexed-family metadata, "
             "which the equivariant solver consumes as symmetry hints, so the "
             "two Acacia routes are not equivalent inputs.  With --tool "
             "ltlsynt, feed syfco's unadapted formula from a cached or "
             "temporary .ltl/.part pair together with ltlsynt's explicit "
             "--semantics flag.  This is both entrant-realistic and stronger "
             "than adapting the formula.",
    )
    p.add_argument(
        "--tlsf-corpus",
        metavar="DIR",
        help="resolve --tlsf-map paths relative to the corpus directory produced "
             "by benchmarking/syntcomp-corpus.py materialize",
    )
    p.add_argument("--syfco", default="syfco")
    p.add_argument("--syfco-cache", metavar="DIR",
                   help="directory for cached syfco-derived .ltl/.part pairs")
    args = p.parse_args()
    if args.systemd_scope and args.runner_prefix:
        p.error("--systemd-scope and --runner-prefix are mutually exclusive")
    if args.tool == "acacia1x" and args.tlsf_map:
        p.error(
            "Acacia v1 predates the TLSF frontend and must be fed converted "
            ".ltl/.part pairs"
        )

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
        tlsf_map = read_tlsf_map(args.tlsf_map, args.tlsf_corpus)
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
                # Acacia's native TLSF route does not use an .ltl/.part pair.
                cmd = [args.bin, "-T", tlsf]
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
                cmd = build_command(
                    args.tool, args.bin, ltl, ins, outs, machine_model
                )
        else:
            ltl_path = (pathlib.Path(args.instances_dir) / base
                        if args.instances_dir else source_map.get(base))
            if ltl_path is None or not ltl_path.exists():
                print(f"  {base:44s} MISSING")
                continue
            ltl = str(ltl_path)
            ins, outs = read_ltl_partition(ltl)
            # Plain .ltl inputs carry no semantics, so only the TLSF route adds
            # ltlsynt's --semantics flag.
            cmd = build_command(args.tool, args.bin, ltl, ins, outs)
        cmd = runner_prefix + cmd + extra
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
    with campaign_scope_guard("run-subset"):
        main()
