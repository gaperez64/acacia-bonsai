#!/usr/bin/env python3
"""Census of reduced searches inside Acacia's exact approximations.

For each worker of each instance, dump the complete input/action table and a
checkpoint at every crossing of a frontier size, then probe each checkpoint in
one of three modes.  Core measures the support-closed subset left by peeling
the checkpoint maxima.  Kernel measures a bounded search grown from the
initial vector inside the checkpoint envelope.  Width measures exact
continuation from progressively wider prefixes of the checkpoint maxima.

Together these distinguish a peelable core, a small interior invariant, and a
narrow exact starting frontier without mixing their different output schemas.

Example:

    benchmarking/small-invariant-campaign.py \\
        --build build_research \\
        --from-census benchmarking/gap-census.tsv --mechanism M2 \\
        --source-map syntcomp24=tests/suites/benchmarks/syntcomp24/sources.tsv \\
        --out benchmarking/small-invariant-results.tsv --timeout 120
"""

from __future__ import annotations

import argparse
import csv
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from benchlib import read_part, run_systemd_scope  # noqa: E402
from suite_paths import load_source_map, load_tlsf_source_map  # noqa: E402

LEADING_COLUMNS = ["suite", "instance", "worker", "states", "bool_threshold",
                   "inputs", "actions", "solver_final_maxima"]
PROBE_COLUMNS_BY_MODE = {
    "core": [
        "loop", "k", "after_bound_raise", "checkpoint_maxima", "core_maxima",
        "core_contains_init", "verified", "forward_applications",
        "partial_order_checks", "witness_cache_hits", "generators_removed",
        "cascade_depth", "probe_ms",
    ],
    "kernel": [
        "loop", "k", "checkpoint_maxima", "budget", "kernel_maxima", "verified",
        "search_nodes", "dead_ends", "envelope_rejections", "forward_applications",
        "search_ms",
    ],
    "width": [
        "loop", "k", "checkpoint_maxima", "width", "iterations", "final_maxima",
        "contains_init", "matches_full_width", "branch_ms",
    ],
}


def read_instance_list(path: pathlib.Path) -> list[str]:
    names = []
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            names.append(line)
    return names


def parse_pairs(values, what):
    pairs = {}
    for value in values or []:
        if "=" not in value:
            raise SystemExit(f"{what} must be SUITE=PATH, got {value!r}")
        suite, path = value.split("=", 1)
        pairs[suite] = pathlib.Path(path)
    return pairs


def census_cohort(path: pathlib.Path, mechanisms: list[str]) -> dict[str, list[str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows or "mechanism" not in rows[0]:
        raise SystemExit(f"{path} has no mechanism column")
    cohort: dict[str, list[str]] = {}
    for row in rows:
        if mechanisms and not any(row["mechanism"].startswith(p) for p in mechanisms):
            continue
        names = cohort.setdefault(row["suite"], [])
        if row["instance"] not in names:
            names.append(row["instance"])
    return cohort


def read_meta(directory: pathlib.Path) -> dict[str, str]:
    lines = (directory / "meta.tsv").read_text().splitlines()
    if len(lines) < 2:
        return {}
    return dict(zip(lines[0].split("\t"), lines[1].split("\t")))


def table_shape(directory: pathlib.Path) -> tuple[str, str]:
    """Inputs and actions, from the table's own header comment."""
    path = directory / "all-input-actions.tsv"
    if not path.exists():
        return ("0", "0")
    with path.open() as handle:
        header = handle.readline()
    fields = dict(
        part.split("=", 1) for part in header.lstrip("# ").split() if "=" in part
    )
    return (fields.get("inputs", "0"), fields.get("actions", "0"))


def solver_final(directory: pathlib.Path) -> str:
    path = directory / "antichain-final.tsv"
    if not path.exists():
        return "none"
    with path.open() as handle:
        header = handle.readline()
    for part in header.lstrip("# ").split():
        if part.startswith("maxima="):
            return part.split("=", 1)[1]
    return "none"


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--build", required=True, help="research build directory")
    parser.add_argument("--suite", action="append", default=[], metavar="SUITE=LIST")
    parser.add_argument("--from-census", metavar="PATH")
    parser.add_argument("--mechanism", action="append", default=[], metavar="PREFIX")
    parser.add_argument("--source-map", action="append", default=[], metavar="SUITE=PATH")
    parser.add_argument("--tlsf-source-map", action="append", default=[], metavar="SUITE=PATH")
    parser.add_argument("--tlsf-corpus", metavar="DIR")
    parser.add_argument("--out", required=True)
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="research cap; the landing gates keep their own 17 s")
    parser.add_argument("--probe-timeout", type=float, default=300.0,
                        help="wall budget for the offline probe of one worker")
    parser.add_argument("--probe-mode", choices=("core", "kernel", "width"),
                        default="core", help="offline probe mode")
    parser.add_argument("--budget", type=int, default=64,
                        help="maximum generators in kernel mode")
    parser.add_argument("--max-width", type=int, default=256,
                        help="largest prefix before full width in width mode")
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    build = pathlib.Path(args.build)
    solver = build / "src" / "acacia-bonsai"
    probe = build / "src" / "acacia-small-invariant"
    for binary in (solver, probe):
        if not binary.exists():
            raise SystemExit(f"missing {binary}; configure with -Dbuild_research_tools=true "
                             "and -Dacacia_enable_diagnostics=true")

    if not args.suite and not args.from_census:
        raise SystemExit("give --suite SUITE=LIST or --from-census PATH")
    suites = parse_pairs(args.suite, "--suite")
    cohort = (census_cohort(pathlib.Path(args.from_census), args.mechanism)
              if args.from_census else {})
    for suite in cohort:
        suites.setdefault(suite, None)

    source_maps = {s: load_source_map(p)
                   for s, p in parse_pairs(args.source_map, "--source-map").items()}
    tlsf_pairs = parse_pairs(args.tlsf_source_map, "--tlsf-source-map")
    if tlsf_pairs and not args.tlsf_corpus:
        raise SystemExit("--tlsf-source-map needs --tlsf-corpus")
    tlsf_maps = {s: load_tlsf_source_map(p, tlsf_corpus=pathlib.Path(args.tlsf_corpus))
                 for s, p in tlsf_pairs.items()}

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    probe_columns = PROBE_COLUMNS_BY_MODE[args.probe_mode]
    columns = LEADING_COLUMNS + probe_columns
    written = 0
    with out.open("w", encoding="utf-8") as sink:
        sink.write("\t".join(columns) + "\n")
        # Flush the header at once, so a campaign killed before its first row
        # still leaves a readable file rather than an empty one.
        sink.flush()
        for suite, list_path in sorted(suites.items()):
            names = read_instance_list(list_path) if list_path else cohort[suite]
            if args.limit:
                names = names[: args.limit]
            for index, name in enumerate(names, 1):
                ltl = source_maps.get(suite, {}).get(name)
                tlsf = tlsf_maps.get(suite, {}).get(name)
                if ltl is not None and ltl.exists():
                    part = ltl.with_suffix(".part")
                    if not part.exists():
                        print(f"# skip {suite}/{name}: no partition", file=sys.stderr)
                        continue
                    inputs, outputs = read_part(part)
                    command = [str(solver), "--spot-fast", "off",
                               "-F", str(ltl), "-i", inputs, "-o", outputs]
                elif tlsf is not None and tlsf.exists():
                    command = [str(solver), "--spot-fast", "off", "-T", str(tlsf)]
                else:
                    print(f"# skip {suite}/{name}: no source", file=sys.stderr)
                    continue

                # A fresh directory per instance: the dump is keyed by pid, and
                # reusing a directory would mix workers across runs.
                root = pathlib.Path(tempfile.mkdtemp(prefix="acacia-small-invariant-"))
                try:
                    env = dict(os.environ)
                    env.update({
                        "ACACIA_DIAG": "1",
                        "ACACIA_DIAG_INSTANCE": name,
                        "ACACIA_DIAG_PROGRESS_EVERY": "0",
                        "ACACIA_ANTICHAIN_SNAPSHOT_DIR": str(root),
                        "ACACIA_ANTICHAIN_SNAPSHOT_ALL_ACTIONS": "1",
                        # Frontier crossings drive the checkpoints; the loop
                        # modulo is disabled so it cannot add noise.
                        "ACACIA_ANTICHAIN_SNAPSHOT_EVERY": "1000000000",
                        "ACACIA_ANTICHAIN_SNAPSHOT_MAX": "64",
                    })
                    run_systemd_scope(command, args.timeout, args.memory_max,
                                      args.memory_swap_max,
                                      unit_prefix="acacia-small-invariant", env=env)

                    for directory in sorted(root.glob("aut-*")):
                        if not (directory / "all-input-actions.tsv").exists():
                            continue
                        if not list(directory.glob("antichain-*.tsv")):
                            continue
                        meta = read_meta(directory)
                        n_inputs, n_actions = table_shape(directory)
                        final = solver_final(directory)
                        probe_command = [str(probe), "--dir", str(directory),
                                         "--mode", args.probe_mode, "--no-header"]
                        if args.probe_mode == "kernel":
                            probe_command.extend(["--budget", str(args.budget)])
                        elif args.probe_mode == "width":
                            probe_command.extend(["--max-width", str(args.max_width)])
                        try:
                            done = subprocess.run(
                                probe_command,
                                capture_output=True, text=True, timeout=args.probe_timeout)
                        except subprocess.TimeoutExpired:
                            sink.write("\t".join(
                                [suite, name, meta.get("worker", "unknown"),
                                 meta.get("states", "0"), meta.get("bool_threshold", "0"),
                                 n_inputs, n_actions, final]
                                + ["0"] * (len(probe_columns) - 1) + ["timeout"]) + "\n")
                            sink.flush()
                            continue
                        for line in done.stdout.splitlines():
                            if not line.strip():
                                continue
                            sink.write("\t".join(
                                [suite, name, meta.get("worker", "unknown"),
                                 meta.get("states", "0"), meta.get("bool_threshold", "0"),
                                 n_inputs, n_actions, final] + line.split("\t")) + "\n")
                            written += 1
                        sink.flush()
                finally:
                    shutil.rmtree(root, ignore_errors=True)
                if index % 10 == 0:
                    print(f"# {suite} {index}/{len(names)}", file=sys.stderr, flush=True)

    print(f"# wrote {written} checkpoint rows to {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
