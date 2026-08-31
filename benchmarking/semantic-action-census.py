#!/usr/bin/env python3
"""Semantic-action census of Acacia's MONA input/output precomputer.

Sprint A stage A0.  The question it answers is how much of Acacia's action
construction is *output-path duplication*: `src/ios_precomputers/mona.hh`
descends the canonical relation `R(i,o,p,q)` one output path at a time and
decodes a transition set at every path that reaches the state-variable
frontier, even when many paths reach the same residual BDD node.  BuDDy nodes
are canonical, so paths reaching the same node denote the same endpoint
relation and decode to identical transition sets, and CPre unions over outputs.

The decisive ratio per worker is

    raw_output_paths / unique_residual_roots

which bounds what a pre-decoding equality quotient can remove, and

    unique_residual_roots / minimal_residual_roots

which bounds what inclusion-dominance pruning can remove on top of it.

Two modes, because the numbers come from different places:

  census   (default)  runs with ACACIA_DIAG_ALPHABET_CENSUS_ONLY, which reports
                      the BDD-side structure and stops before the expansion.
                      Cheap, and it reaches workers the solver would otherwise
                      answer through a fast path.
  decode   (--decode) runs the solver normally and additionally counts the
                      transition sets the expansion actually decoded.  This is
                      the validation number stage A1 must match exactly, but it
                      only exists for workers that reach the precomputer.

Example:

    benchmarking/semantic-action-census.py \\
        --build build_sprint_diag \\
        --suite syntcomp24=tests/suites/benchmarks/syntcomp24/regress.list \\
        --source-map syntcomp24=tests/suites/benchmarks/syntcomp24/sources.tsv \\
        --out benchmarking/semantic-action-census.tsv --timeout 25
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from benchlib import read_part, run_systemd_scope  # noqa: E402
from suite_paths import load_source_map, load_tlsf_source_map  # noqa: E402


def load_run_diag_targets():
    """Import the sibling runner, which owns the diagnostics parsing."""
    spec = importlib.util.spec_from_file_location(
        "run_diag_targets", HERE / "run_diag_targets.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


# Census column -> ACACIA_DIAG field.  `census_ms` covers traversal and
# residual-root dedup together because they are one memoized DAG walk; the two
# cannot be separated without walking twice and changing what is measured.
DIAG_COLUMNS = {
    "aut_states": "aut_states",
    "input_classes": "alphabet_input_nodes",
    "raw_output_paths": "alphabet_output_paths",
    "unique_residual_roots": "alphabet_output_nodes",
    "minimal_residual_roots": "alphabet_minimal_output_nodes",
    "max_output_paths": "alphabet_max_output_paths",
    "max_residual_roots": "alphabet_max_output_nodes",
    "decoded_transition_sets": "decoded_transition_sets",
    "unique_action_vecs": "decoded_unique_transition_sets",
    "relation_bdd_nodes": "alphabet_bdd_nodes",
    "dominance_tests": "alphabet_dominance_tests",
    "dominance_declines": "alphabet_dominance_declines",
    "census_ms": "alphabet_census_ms",
    "dominance_ms": "alphabet_dominance_ms",
    "decode_ms": "decode_ms",
    "max_f": "max_f",
    "actions_seen": "actions_seen",
}

COLUMNS = ["suite", "instance", "worker", "mode", "route"] + list(DIAG_COLUMNS) + ["result"]


def read_instance_list(path: pathlib.Path) -> list[str]:
    names = []
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            names.append(line)
    return names


def census_cohort(path: pathlib.Path, mechanisms: list[str]) -> dict[str, list[str]]:
    """Cohort straight out of a gap-census TSV, so the M1 and M2 sets this
    census runs on are the same rows the gap census classified, with no
    hand-maintained copy to drift."""
    import csv

    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows or "mechanism" not in rows[0]:
        raise SystemExit(f"{path} has no mechanism column")
    cohort: dict[str, list[str]] = {}
    for row in rows:
        mechanism = row["mechanism"]
        if mechanisms and not any(mechanism.startswith(prefix) for prefix in mechanisms):
            continue
        names = cohort.setdefault(row["suite"], [])
        if row["instance"] not in names:
            names.append(row["instance"])
    return cohort


def parse_pairs(values: list[str], what: str) -> dict[str, pathlib.Path]:
    pairs = {}
    for value in values or []:
        if "=" not in value:
            raise SystemExit(f"{what} must be SUITE=PATH, got {value!r}")
        suite, path = value.split("=", 1)
        pairs[suite] = pathlib.Path(path)
    return pairs


def census_rows(diag_rows: list[dict[str, str]], decode: bool) -> list[dict[str, str]]:
    """One row per worker.

    In census mode the numbers arrive on the `alphabet-census` checkpoint, which
    is the only checkpoint the census-only run reaches.  In decode mode the
    terminal `final` row carries both the census and the decode counters, so a
    worker that never reached the precomputer is reported with zeros rather than
    dropped -- an absent worker and a worker with nothing to quotient are
    different findings.
    """
    wanted = "final" if decode else "alphabet-census"
    rows = []
    seen = set()
    for row in diag_rows:
        if row.get("checkpoint") != wanted:
            continue
        worker = row.get("path", "unknown")
        if worker in seen:
            continue
        seen.add(worker)
        rows.append(row)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", required=True, help="diagnostics build directory")
    parser.add_argument("--suite", action="append", default=[],
                        metavar="SUITE=LIST", help="repeatable; a suite and its .list file")
    parser.add_argument("--from-census", metavar="PATH",
                        help="take the cohort from a gap-census TSV instead of .list files")
    parser.add_argument("--mechanism", action="append", default=[], metavar="PREFIX",
                        help="repeatable; with --from-census, keep rows whose mechanism starts "
                        "with PREFIX (for example M1, M2)")
    parser.add_argument("--source-map", action="append", default=[],
                        metavar="SUITE=PATH", help="repeatable; a suite and its sources.tsv")
    parser.add_argument("--tlsf-source-map", action="append", default=[],
                        metavar="SUITE=PATH",
                        help="repeatable; a suite and its tlsf-sources.tsv.  syntcomp26 has no "
                        ".ltl map, so its rows only reach the census through this route")
    parser.add_argument("--tlsf-corpus", metavar="DIR",
                        help="materialized TLSF corpus, required with --tlsf-source-map")
    parser.add_argument("--out", required=True, help="census TSV to write")
    parser.add_argument("--timeout", type=float, default=25.0)
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--limit", type=int, default=0, help="stop after N instances per suite")
    parser.add_argument("--decode", action="store_true",
                        help="solve normally and count decoded transition sets, instead of "
                        "stopping after the BDD-side census")
    parser.add_argument("--no-dominance", action="store_true",
                        help="skip the inclusion-dominance pass")
    parser.add_argument("--dominance-ms", default="",
                        help="per-input wall-clock budget for dominance, in milliseconds")
    args = parser.parse_args()

    runner = load_run_diag_targets()
    binary = pathlib.Path(args.build) / "src" / "acacia-bonsai"
    if not binary.exists():
        raise SystemExit(f"no diagnostics binary at {binary}")

    if not args.suite and not args.from_census:
        raise SystemExit("give --suite SUITE=LIST or --from-census PATH")
    suites = parse_pairs(args.suite, "--suite")
    cohort = census_cohort(pathlib.Path(args.from_census), args.mechanism) if args.from_census else {}
    for suite, names in cohort.items():
        suites.setdefault(suite, None)
    source_maps = {suite: load_source_map(path)
                   for suite, path in parse_pairs(args.source_map, "--source-map").items()}
    tlsf_pairs = parse_pairs(args.tlsf_source_map, "--tlsf-source-map")
    if tlsf_pairs and not args.tlsf_corpus:
        raise SystemExit("--tlsf-source-map needs --tlsf-corpus")
    tlsf_maps = {suite: load_tlsf_source_map(path, tlsf_corpus=pathlib.Path(args.tlsf_corpus))
                 for suite, path in tlsf_pairs.items()}
    missing = sorted(set(suites) - set(source_maps) - set(tlsf_maps))
    if missing:
        raise SystemExit(f"no source map for suite(s): {', '.join(missing)}")

    env = runner.diagnostic_environment(
        dict(os.environ),
        progress_every="0",
        memory_max=args.memory_max,
        memory_swap_max=args.memory_swap_max,
        preprocessing_census_only=False,
        alphabet_census_only=not args.decode,
        semantic_dominance=not args.no_dominance,
        semantic_decode=args.decode,
    )
    if args.dominance_ms:
        env["ACACIA_DIAG_SEMANTIC_DOMINANCE_MS"] = args.dominance_ms

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    mode = "decode" if args.decode else "census"
    written = 0
    with out.open("w", encoding="utf-8") as sink:
        sink.write("\t".join(COLUMNS) + "\n")
        for suite, list_path in sorted(suites.items()):
            names = read_instance_list(list_path) if list_path else cohort[suite]
            if args.limit:
                names = names[: args.limit]
            source_map = source_maps.get(suite, {})
            tlsf_map = tlsf_maps.get(suite, {})
            for index, name in enumerate(names, 1):
                # Prefer the .ltl pair when the suite has one; syntcomp26 has
                # only a TLSF map, so those rows go through the native frontend.
                ltl = source_map.get(name)
                tlsf = tlsf_map.get(name)
                if ltl is not None and ltl.exists():
                    part = ltl.with_suffix(".part")
                    if not part.exists():
                        print(f"# skip {suite}/{name}: no partition", file=sys.stderr)
                        continue
                    inputs, outputs = read_part(part)
                    route = "ltl"
                    command = [str(binary), "-F", str(ltl), "-i", inputs, "-o", outputs]
                elif tlsf is not None and tlsf.exists():
                    route = "tlsf"
                    command = [str(binary), "-T", str(tlsf)]
                else:
                    print(f"# skip {suite}/{name}: no source", file=sys.stderr)
                    continue

                accumulator = runner.DiagnosticAccumulator()
                run = run_systemd_scope(
                    command,
                    args.timeout,
                    args.memory_max,
                    args.memory_swap_max,
                    unit_prefix="acacia-semantic-census",
                    env=env | {"ACACIA_DIAG_INSTANCE": name},
                    capture_filter=lambda line: "ACACIA_DIAG" in line,
                    capture_consumer=accumulator.add_line,
                )

                workers = census_rows(accumulator.rows(), args.decode)
                if not workers:
                    # A worker that produced no census at all is still evidence:
                    # it says the run never reached the precomputer.
                    sink.write("\t".join(
                        [suite, name, "none", mode, route]
                        + ["0"] * len(DIAG_COLUMNS)
                        + ["timeout" if run.timed_out else "no-census"]) + "\n")
                    sink.flush()
                    continue
                for row in workers:
                    values = [row.get(field, "0") or "0" for field in DIAG_COLUMNS.values()]
                    sink.write("\t".join(
                        [suite, name, row.get("path", "unknown"), mode, route]
                        + values
                        + [row.get("result", "unknown")]) + "\n")
                    written += 1
                sink.flush()
                if index % 20 == 0:
                    print(f"# {suite} {index}/{len(names)}", file=sys.stderr, flush=True)

    print(f"# wrote {written} worker rows to {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
