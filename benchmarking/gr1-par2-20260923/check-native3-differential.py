#!/usr/bin/env python3
"""Compare the native exact GR(1) portfolio with the committed Python oracle."""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking/gr1-par2-20260923/oracle"))
from acacia_lift.direct import Decline, run_exact_direct  # noqa: E402


def corpus_sample(count: int) -> list[Path]:
    root = ROOT / "tests/syntcomp-benchmarks/tlsf"
    candidates = sorted((p for p in root.rglob("*.tlsf") if p.stat().st_size < 20000),
                        key=lambda p: (p.stat().st_size, str(p)))
    first_by_family: dict[str, Path] = {}
    for candidate in candidates:
        family = candidate.relative_to(root).parts[0]
        first_by_family.setdefault(family, candidate)
    selected = list(first_by_family.values())
    selected.extend(p for p in candidates if p not in selected)
    return selected[:count]


def native_verdict(binary: Path, source: Path, deadline: float) -> tuple[str, str]:
    env = os.environ.copy()
    env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(deadline)
    process = subprocess.Popen([str(binary), "-T", str(source), "--arms",
                                "real:gr1:oxidd,unreal:gr1:oxidd"],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, env=env, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=max(1, deadline - time.monotonic() + 1))
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        return "UNKNOWN", "subprocess_timeout"
    verdict = {0: "REALIZABLE", 1: "UNREALIZABLE", 2: "UNKNOWN"}.get(
        process.returncode, "ERROR")
    return verdict, stderr.strip().replace("\n", " | ")[:500]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--tlsf-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=50)
    parser.add_argument("--seconds", type=float, default=10)
    args = parser.parse_args()
    binary = args.binary.resolve()
    build = args.tlsf_build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    config = SimpleNamespace(
        monitor=ROOT / "subprojects/tlsf-tools/scripts/gr1_monitor_game.py",
        tlsf2ltl=build / "tlsf2ltl", tlsf2tlsf=build / "tlsf2tlsf",
        tlsfinfo=build / "tlsfinfo", solver=build / "tlsfsolve",
        checker=build / "tlsfcertcheck",
        bindings_python=Path("/usr/bin/python3.13"),
        bindings_site=Path("/usr/local/lib64/python3.13/site-packages"),
    )
    disagreements = []
    rows = []
    for index, source in enumerate(corpus_sample(args.count)):
        native, detail = native_verdict(binary, source, time.monotonic() + args.seconds)
        try:
            oracle = run_exact_direct(source, output / f"oracle_{index:03d}", config,
                                      time.monotonic() + args.seconds,
                                      args.seconds / 2).verdict
        except Decline as error:
            oracle = f"DECLINED:{error.stage}:{error.reason}"
        match = native == oracle or (native == "UNKNOWN" and oracle.startswith("DECLINED:"))
        if not match:
            disagreements.append(str(source.relative_to(ROOT)))
        rows.append({"source": str(source.relative_to(ROOT)), "native": native,
                     "oracle": oracle, "match": int(match), "native_diagnostic": detail})
        print(f"{index + 1}/{args.count} {rows[-1]['source']} {native} {oracle}", flush=True)
    with (output / "results.tsv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    print(f"cases={len(rows)} disagreements={len(disagreements)}")
    for source in disagreements:
        print(f"DISAGREEMENT {source}")
    return 1 if disagreements else 0


if __name__ == "__main__":
    raise SystemExit(main())
