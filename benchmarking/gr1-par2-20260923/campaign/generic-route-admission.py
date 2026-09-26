#!/usr/bin/env python3
"""Serial admission census using Stage C target reduction and its unchanged gate."""

from __future__ import annotations

import collections
import csv
import hashlib
import importlib.util
import pathlib
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "benchmarking/gr1-par2-20260923/oracle"))
from acacia_lift.direct import Decline  # noqa: E402
from acacia_lift.lifting import settings, source as lowering  # noqa: E402
from acacia_lift.tools import ToolConfiguration, configuration_defaults  # noqa: E402


def sources():
    script = HERE.parent / "generic-census.py"
    spec = importlib.util.spec_from_file_location("generic_census", script)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.sources()


def main() -> None:
    build = pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-SB-8b158d7")
    defaults = configuration_defaults()
    config = ToolConfiguration(build, defaults.bindings_python,
                               defaults.bindings_site, defaults.buddy_adapter)
    scratch = ROOT / "build_scratch/stageC/admission"
    scratch.mkdir(parents=True, exist_ok=True)
    rows = sources()
    if len(rows) != 1524 or len({identifier for identifier, _ in rows}) != 1524:
        raise ValueError("expected 1,524 distinct original inputs")
    fields = ("id", "source_sha256", "admitted_17", "elapsed_17_s", "semantics_17",
              "reason_17", "admitted_60", "elapsed_60_s", "semantics_60", "reason_60")
    output = HERE / "generic-selection" / "admission-census.tsv"
    output.parent.mkdir(exist_ok=True)
    ids = {17: [], 60: []}
    counts = {17: collections.Counter(), 60: collections.Counter()}
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for index, (identifier, original) in enumerate(rows, 1):
            row = {"id": identifier,
                   "source_sha256": hashlib.sha256(original.read_bytes()).hexdigest()}
            for cap in (17, 60):
                case_dir = scratch / f"{index}-{cap}"
                case_dir.mkdir()
                started = time.monotonic()
                gate = min(settings.DEFAULT_ELIGIBILITY_BUDGET_SECONDS,
                           settings.ELIGIBILITY_CAP_FRACTION * cap)
                deadline = started + gate
                admitted, semantics, reason = "no", "", ""
                try:
                    try:
                        lowering.lower(original, case_dir / "exact", config, deadline)
                        admitted, semantics = "yes", "exact"
                    except Decline as exact_error:
                        try:
                            lowering.lower(original, case_dir / "strict", config, deadline,
                                           semantics="strict")
                            admitted, semantics = "yes", "strict"
                        except Decline as strict_error:
                            reason = f"exact:{exact_error.reason};strict:{strict_error.reason}"
                except (OSError, ValueError, RuntimeError) as error:
                    reason = type(error).__name__
                row.update({f"admitted_{cap}": admitted,
                            f"elapsed_{cap}_s": f"{time.monotonic() - started:.6f}",
                            f"semantics_{cap}": semantics, f"reason_{cap}": reason})
                counts[cap][semantics if admitted == "yes" else reason] += 1
                if admitted == "yes":
                    ids[cap].append(identifier)
                shutil.rmtree(case_dir)
            writer.writerow(row)
            stream.flush()
            if index % 100 == 0:
                print(f"{index}/1524: 17 s {len(ids[17])}, 60 s {len(ids[60])}",
                      file=sys.stderr, flush=True)
    for cap in (17, 60):
        (output.parent / f"admitted-{cap}.list").write_text(
            "".join(f"{identifier}\n" for identifier in ids[cap]), encoding="utf-8")
        print(f"{cap} s: {len(ids[cap])} admitted; {dict(counts[cap])}")


if __name__ == "__main__":
    main()
