#!/usr/bin/env python3
"""Measure exact-reduction admission under the wrapper's unchanged eligibility gate."""

from __future__ import annotations

import argparse
import collections
import csv
import importlib.util
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking/gr1-par2-20260923/oracle"))

from acacia_lift.direct import Decline, lower_exact  # noqa: E402
from acacia_lift.tools import ToolConfiguration, configuration_defaults  # noqa: E402


def sources() -> list[tuple[str, pathlib.Path]]:
    path = pathlib.Path(__file__).with_name("generic-census.py")
    spec = importlib.util.spec_from_file_location("generic_census", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.sources()


def gate(cap: int) -> float:
    # Wrapper defaults: 1 s eligibility and 5% of the outer cap.
    return min(1.0, 0.05 * cap)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).with_name("campaign") /
                        "generic-admission.tsv")
    parser.add_argument("--scratch", type=pathlib.Path,
                        default=ROOT / "build_scratch/stageA/admission")
    parser.add_argument("--tlsf-tools-build", type=pathlib.Path,
                        default=configuration_defaults().tlsf_tools_build)
    args = parser.parse_args()
    defaults = configuration_defaults()
    config = ToolConfiguration(args.tlsf_tools_build, defaults.bindings_python,
                               defaults.bindings_site, defaults.buddy_adapter)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.scratch.mkdir(parents=True, exist_ok=True)
    fields = ("id", "admitted_17", "elapsed_17_s", "reason_17",
              "admitted_60", "elapsed_60_s", "reason_60")
    counts: dict[int, collections.Counter[str]] = {17: collections.Counter(),
                                                     60: collections.Counter()}
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for index, (identifier, source) in enumerate(sources(), 1):
            row: dict[str, str] = {"id": identifier}
            for cap in (17, 60):
                output = args.scratch / f"{index}-{cap}"
                output.mkdir(exist_ok=True)
                start = time.monotonic()
                try:
                    lower_exact(source, output, config, start + gate(cap))
                    admitted, reason = "yes", ""
                except Decline as error:
                    admitted, reason = "no", error.reason
                except (OSError, ValueError) as error:
                    admitted, reason = "no", type(error).__name__
                row[f"admitted_{cap}"] = admitted
                row[f"elapsed_{cap}_s"] = f"{time.monotonic() - start:.6f}"
                row[f"reason_{cap}"] = reason
                counts[cap]["admitted" if admitted == "yes" else reason] += 1
                for artifact in output.iterdir():
                    artifact.unlink()
                output.rmdir()
            writer.writerow(row)
            stream.flush()
            if index % 100 == 0:
                print(f"{index}/1524: 17 s {counts[17]['admitted']}, "
                      f"60 s {counts[60]['admitted']}", file=sys.stderr, flush=True)
    print(f"17 s: {dict(counts[17])}; 60 s: {dict(counts[60])}")


if __name__ == "__main__":
    main()
