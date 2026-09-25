#!/usr/bin/env python3
"""Serial census of actual-input exact GR(1) reduction, independent of names."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
sys.path.insert(0, str(ROOT / "benchmarking/gr1-par2-20260923/oracle"))
from acacia_lift.direct import Decline, lower_exact, sha256_file  # noqa: E402
from acacia_lift.tools import ToolConfiguration, configuration_defaults  # noqa: E402


def sources() -> list[tuple[str, pathlib.Path]]:
    path = ROOT / "benchmarking/run-syntcomp26-coverage.py"
    spec = importlib.util.spec_from_file_location("coverage_module", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    suite = ROOT / "tests/suites/benchmarks/syntcomp26"
    ids = module.read_instance_list(suite / "all.list")
    targets = module.resolve_targets(
        ids, module.read_tlsf_map(suite / "tlsf-sources.tsv"), ROOT / "tlsf-corpus")
    assert len(ids) == 1524
    return [(identifier, targets[identifier][1]) for identifier in ids]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).with_name("campaign") / "generic-census.tsv")
    parser.add_argument("--scratch", type=pathlib.Path,
                        default=ROOT / "build_scratch/stageA/census")
    parser.add_argument("--tlsf-tools-build", type=pathlib.Path,
                        default=configuration_defaults().tlsf_tools_build)
    parser.add_argument("--bound", type=float, default=17 * 2 / 3)
    args = parser.parse_args()
    defaults = configuration_defaults()
    config = ToolConfiguration(args.tlsf_tools_build, defaults.bindings_python,
                               defaults.bindings_site, defaults.buddy_adapter)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.scratch.mkdir(parents=True, exist_ok=True)
    fields = ("id", "input_sha256", "reducible", "elapsed_s", "reason")
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for index, (identifier, source) in enumerate(sources(), 1):
            output = args.scratch / str(index)
            output.mkdir(exist_ok=True)
            start = time.monotonic()
            try:
                lower_exact(source, output, config, start + args.bound)
                reducible, reason = "yes", ""
            except Decline as error:
                reducible, reason = "no", error.reason
            except (OSError, ValueError) as error:
                reducible, reason = "no", type(error).__name__
            writer.writerow({"id": identifier, "input_sha256": sha256_file(source),
                             "reducible": reducible, "elapsed_s":
                             f"{time.monotonic() - start:.6f}", "reason": reason})
            stream.flush()
            for artifact in output.iterdir():
                artifact.unlink()
            output.rmdir()
            if index % 100 == 0:
                print(f"{index}/1524", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
