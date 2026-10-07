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

Campaign legs add --raw-tsv, which appends one fsync'd observation per instance
in run-syntcomp26-coverage.py's column schema (scope memory peak, binary SHA-256,
regime) and supports --resume.  SyFCo conversion stays outside the timed run, as
it always has for converted-pair routes; a conversion failure is recorded as a
SYFCO-FAIL row, and with --instances-dir only pairs named by --syfco-failures
may be absent.
"""
import argparse
import csv
import datetime
import hashlib
import importlib.util
import os
import pathlib
import signal
import shlex
import subprocess
import sys
from types import SimpleNamespace
import tempfile

from scope_memory import append_memory_sidecar, memory_fields, write_memory_sidecar

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


def read_semantics_map(path):
    """Frozen SyFCo machine model for pre-converted ltlsynt inputs."""
    with open(path, newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["instance", "semantics"]:
            raise ValueError(f"{path}: expected instance and semantics columns")
        mapping = {}
        for row in reader:
            name, model = row["instance"], row["semantics"]
            if not name or name in mapping or model not in {"Mealy", "Moore"}:
                raise ValueError(f"{path}: invalid or duplicate semantics for {name!r}")
            mapping[name] = model
    return mapping


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


def coverage_module():
    """The coverage runner, for its observation schema and TSV helpers."""
    path = pathlib.Path(__file__).with_name("run-syntcomp26-coverage.py")
    spec = importlib.util.spec_from_file_location("syntcomp26_coverage_schema", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RawObservations:
    """Append-only, resumable observations in the coverage runner's schema.

    A resumed file must have been written by the same tool, binary bytes,
    flags, revision label, cap and memory regime; anything else is a
    different measurement and is refused.
    """

    IDENTITY = ("acacia_sha", "binary_sha256", "preset", "flags", "cap_s",
                "memory_max", "memory_swap_max")

    def __init__(self, path, *, label, tool, binary, revision, flags, cap,
                 memory_max, memory_swap_max, resume):
        self.coverage = coverage_module()
        self.path = pathlib.Path(path)
        self.label = label
        digest = hashlib.sha256()
        with pathlib.Path(binary).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        self.identity = {
            "acacia_sha": revision, "binary_sha256": digest.hexdigest(),
            "preset": tool, "flags": flags, "cap_s": str(cap),
            "memory_max": memory_max, "memory_swap_max": memory_swap_max,
        }
        self.tool = tool
        if resume and self.path.exists():
            rows = self.coverage.load_output(self.path)
            for row in rows:
                if row["solver_label"] == label and any(
                        row[key] != self.identity[key] for key in self.IDENTITY):
                    raise SystemExit(f"{self.path}: resume identity differs from the recorded leg")
        else:
            if self.path.exists():
                raise SystemExit(f"{self.path} exists; pass --resume to continue it")
            rows = []
            self.coverage.atomic_write_tsv(self.path, self.coverage.OUTPUT_COLUMNS, [])
        if resume and self.path.exists():
            with self.path.open(newline="") as stream:
                columns = next(csv.reader(stream, delimiter="\t"))
            if columns != self.coverage.OUTPUT_COLUMNS:
                original = self.path.with_name(f"{self.path.stem}-legacy.tsv")
                if original.exists():
                    raise SystemExit(f"refusing to overwrite original observations {original}")
                original.write_bytes(self.path.read_bytes())
                self.coverage.atomic_write_tsv(self.path, self.coverage.OUTPUT_COLUMNS, rows)
        write_memory_sidecar(self.path, rows)
        self.rows = rows
        self.done = {row["instance"] for row in rows if row["solver_label"] == label}

    def _base(self, instance, source_file):
        return {
            **dict.fromkeys(self.coverage.OUTPUT_COLUMNS, ""),
            **self.identity,
            "solver_label": self.label, "instance": instance,
            "tlsf_file": pathlib.Path(source_file).name if source_file else "",
            "expectation_source": "none", "run_index": str(len(self.rows)),
            "timestamp_utc": datetime.datetime.now(datetime.timezone.utc)
            .isoformat().replace("+00:00", "Z"),
            "collect_rusage": "false",
        }

    def _append(self, row):
        self.coverage.append_tsv_row(self.path, self.coverage.OUTPUT_COLUMNS, row)
        append_memory_sidecar(self.path, row)
        self.rows.append(row)
        self.done.add(row["instance"])

    def record_run(self, instance, source_file, run):
        result, reason = self.coverage.normalize_result(run, self.tool)
        row = self._base(instance, source_file)
        row.update({
            "result": result, "seconds": str(run.seconds), "exit_code": str(run.returncode),
            "timed_out": str(run.timed_out).lower(), "resource_reason": reason,
            "stdout_bytes": str(run.stdout_bytes), "stderr_bytes": str(run.stderr_bytes),
            "scope_memory_peak_bytes": ("" if run.memory_peak_bytes is None
                                        else str(run.memory_peak_bytes)),
            "scope_unit": run.scope_unit,
            **memory_fields(run),
        })
        self._append(row)

    def record_syfco_failure(self, instance, source_file):
        row = self._base(instance, source_file)
        row.update({"result": "SYFCO-FAIL", "seconds": "0", "exit_code": "-1",
                    "timed_out": "false", "resource_reason": "syfco",
                    "stdout_bytes": "0", "stderr_bytes": "0"})
        row.update(scope_memory_peak_missing_reason="not executed: SyFCo conversion failed",
                   scope_memory_events_missing_reason="not executed: SyFCo conversion failed",
                   max_process_rss_missing_reason="not executed: SyFCo conversion failed")
        self._append(row)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True)
    p.add_argument("--binary-identity", type=pathlib.Path,
                   help="underlying executable when --bin is a frozen-runtime wrapper")
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
    p.add_argument("--raw-tsv", metavar="PATH",
                   help="append coverage-schema observations here as they finish "
                        "(requires --systemd-scope, --solver-label and an integral --timeout)")
    p.add_argument("--solver-label", help="series label recorded with --raw-tsv")
    p.add_argument("--revision", default="",
                   help="tool revision/version label recorded with --raw-tsv")
    p.add_argument("--resume", action="store_true",
                   help="with --raw-tsv: skip instances already recorded for this label")
    p.add_argument("--syfco-failures", metavar="PATH",
                   help="with --instances-dir: instances whose converted pair is known to be "
                        "absent because SyFCo failed; they are recorded as SYFCO-FAIL")
    p.add_argument("--semantics-map", metavar="PATH",
                   help="with pre-converted ltlsynt pairs: frozen instance to Mealy/Moore map")
    args = p.parse_args()
    if args.systemd_scope and args.runner_prefix:
        p.error("--systemd-scope and --runner-prefix are mutually exclusive")
    if args.raw_tsv and (not args.systemd_scope or not args.solver_label
                         or args.timeout != int(args.timeout)):
        p.error("--raw-tsv needs --systemd-scope, --solver-label and an integral --timeout")
    if args.resume and not args.raw_tsv:
        p.error("--resume applies to --raw-tsv")
    if args.resume and args.csv:
        # The CSV holds only this invocation's rows; beside a resumed raw file
        # it would silently present a partial leg as complete.
        p.error("--csv cannot be combined with --resume; export the raw TSV instead")
    if args.tool == "acacia1x" and args.tlsf_map:
        p.error(
            "Acacia v1 predates the TLSF frontend and must be fed converted "
            ".ltl/.part pairs"
        )
    if args.semantics_map and (args.tool != "ltlsynt" or not args.instances_dir):
        p.error("--semantics-map requires ltlsynt with --instances-dir")

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
    known_syfco_failures = set()
    if args.syfco_failures:
        if not args.instances_dir:
            p.error("--syfco-failures applies to --instances-dir")
        known_syfco_failures = set(read_instance_list(args.syfco_failures))
    semantics_map = read_semantics_map(args.semantics_map) if args.semantics_map else {}
    if args.semantics_map and set(insts) - known_syfco_failures - set(semantics_map):
        p.error("--semantics-map lacks a listed converted input")
    memory_rows = []
    raw = None
    if args.raw_tsv:
        raw = RawObservations(
            args.raw_tsv, label=args.solver_label, tool=args.tool,
            binary=args.binary_identity or args.bin,
            revision=args.revision, flags=args.flags, cap=int(args.timeout),
            memory_max=args.memory_max, memory_swap_max=args.memory_swap_max,
            resume=args.resume)
    print(f"# bin={args.bin}\n# flags={args.flags!r}  timeout={args.timeout}s  n={len(insts)}")
    for base in insts:
        if raw is not None and base in raw.done:
            continue
        if tlsf_map:
            tlsf = tlsf_map.get(base)
            if tlsf is None or not pathlib.Path(tlsf).exists():
                print(f"  {base:44s} MISSING-TLSF")
                if raw is not None:
                    sys.exit(f"{base}: TLSF source missing; a campaign leg cannot skip it")
                continue
            if args.tool == "acacia":
                # Acacia's native TLSF route does not use an .ltl/.part pair.
                cmd = [args.bin, "-T", tlsf]
            else:
                converted = convert_tlsf(args.syfco, tlsf, syfco_cache)
                if converted is None:
                    print(f"  {base:44s} SYFCO-FAIL")
                    memory_rows.append({"instance": base, **memory_fields(SimpleNamespace(
                        scope_memory_peak_missing_reason="not executed: SyFCo conversion failed",
                        scope_memory_events_missing_reason="not executed: SyFCo conversion failed",
                        max_process_rss_missing_reason="not executed: SyFCo conversion failed"))})
                    rows.append({"instance": base, "result": "SYFCO-FAIL",
                                 "seconds": 0.0, "exit": -1})
                    if raw is not None:
                        raw.record_syfco_failure(base, tlsf)
                    continue
                ltl_path, machine_model = converted
                ltl = str(ltl_path)
                ins, outs = read_ltl_partition(ltl)
                cmd = build_command(
                    args.tool, args.bin, ltl, ins, outs, machine_model
                )
            source_file = tlsf
        else:
            ltl_path = (pathlib.Path(args.instances_dir) / base
                        if args.instances_dir else source_map.get(base))
            if ltl_path is None or not ltl_path.exists():
                if base in known_syfco_failures:
                    print(f"  {base:44s} SYFCO-FAIL")
                    memory_rows.append({"instance": base, **memory_fields(SimpleNamespace(
                        scope_memory_peak_missing_reason="not executed: SyFCo conversion failed",
                        scope_memory_events_missing_reason="not executed: SyFCo conversion failed",
                        max_process_rss_missing_reason="not executed: SyFCo conversion failed"))})
                    rows.append({"instance": base, "result": "SYFCO-FAIL",
                                 "seconds": 0.0, "exit": -1})
                    if raw is not None:
                        raw.record_syfco_failure(base, base)
                    continue
                print(f"  {base:44s} MISSING")
                if raw is not None:
                    sys.exit(f"{base}: converted pair missing and not a declared SyFCo failure")
                continue
            if base in known_syfco_failures:
                sys.exit(f"{base}: declared a SyFCo failure but its pair exists")
            source_file = ltl_path
            ltl = str(ltl_path)
            ins, outs = read_ltl_partition(ltl)
            # Plain .ltl inputs carry no semantics, so only the TLSF route adds
            # ltlsynt's --semantics flag.
            cmd = build_command(args.tool, args.bin, ltl, ins, outs,
                                semantics_map.get(base))
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
        observation = {"instance": base, **memory_fields(run)}
        memory_rows.append(observation)
        print(f"# memory {base}: {observation}")
        res = classify_run(run, args.tool)
        if raw is not None:
            raw.record_run(base, source_file, run)
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
        if memory_rows:
            memory_path = str(args.csv) + ".memory.tsv"
            with open(memory_path, "w", newline="") as stream:
                writer = csv.DictWriter(stream, list(memory_rows[0]), delimiter="\t")
                writer.writeheader()
                writer.writerows(memory_rows)
            print(f"wrote {memory_path}")
    if temporary_cache is not None:
        temporary_cache.cleanup()


if __name__ == "__main__":
    def exit_on_signal(signum, _frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, exit_on_signal)
    signal.signal(signal.SIGHUP, exit_on_signal)
    with campaign_scope_guard("run-subset"):
        main()
