#!/usr/bin/env python3
"""Spot-check generated .ltl/.part pairs by rerunning SyFCo exactly."""

from __future__ import annotations

import argparse
import pathlib
import random
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("converted", type=pathlib.Path)
    parser.add_argument("--syfco", default="syfco")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--seed", type=int, default=20260804)
    args = parser.parse_args()

    sources = sorted(
        source
        for source in args.source.glob("*.tlsf")
        if (args.converted / f"{source.stem}.ltl").is_file()
    )
    if args.count < 1 or args.count > len(sources):
        raise SystemExit(f"--count must be between 1 and {len(sources)}")
    selected = random.Random(args.seed).sample(sources, args.count)

    with tempfile.TemporaryDirectory(prefix="acacia-syfco-check-") as raw_tmp:
        tmp = pathlib.Path(raw_tmp)
        for source in sorted(selected):
            expected_part = tmp / f"{source.stem}.part"
            result = subprocess.run(
                [
                    args.syfco,
                    "--format",
                    "ltlxba",
                    "--mode",
                    "fully",
                    "--part-file",
                    str(expected_part),
                    str(source),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            expected_ltl = result.stdout.rstrip() + "\n"
            actual_ltl = (args.converted / f"{source.stem}.ltl").read_text()
            actual_part = (args.converted / f"{source.stem}.part").read_text()
            if actual_ltl != expected_ltl or actual_part != expected_part.read_text():
                raise SystemExit(f"round-trip mismatch: {source.name}")
            print(f"OK {source.name}")

    print(f"verified {len(selected)} deterministic SyFCo conversions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
