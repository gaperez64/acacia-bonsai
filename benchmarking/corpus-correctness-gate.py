#!/usr/bin/env python3
"""Run G4 and classify Meson's individual records, allowing only corpus timeouts."""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess


FALSE_VERDICT = re.compile(r"FALSE[ _-]+(?:POSITIVE|NEGATIVE|VERDICT)", re.I)


def classify(records, meson_exit=0):
    failures = []
    timeouts = 0
    for record in records:
        output = str(record.get("stdout", "")) + "\n" + str(record.get("stderr", ""))
        result = record.get("result")
        if FALSE_VERDICT.search(output):
            failures.append(f"{record.get('name')}: false verdict marker")
        if result == "TIMEOUT":
            timeouts += 1
        elif result != "OK":
            failures.append(f"{record.get('name')}: {result}")
    if not records:
        failures.append("no test records")
    # Meson counts permitted timeouts in its exit status. Every nonzero exit
    # must be accounted for by those records, including when Meson itself fails.
    if meson_exit and (not timeouts or meson_exit not in {1, min(125, timeouts)}):
        failures.append(f"unexplained Meson exit {meson_exit}")
    return not failures, timeouts, failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--log", type=pathlib.Path, help="classify an existing JSON-lines test log")
    parser.add_argument("--meson-exit", type=int, default=0)
    args = parser.parse_args(argv)
    code = args.meson_exit
    path = args.log
    if path is None:
        path = args.build / "meson-logs/g4.json"
        path.unlink(missing_ok=True)
        code = subprocess.run(
            ["meson", "test", "-C", str(args.build), "--num-processes", "1",
             "--suite=ab/realizable", "--suite=ab/unrealizable", "--logbase=g4"],
            check=False).returncode
    try:
        records = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        passed, timeouts, failures = classify(records, code)
    except (OSError, ValueError) as error:
        passed, timeouts, failures = False, 0, [str(error)]
    print(f"permitted timeouts: {timeouts}")
    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"GATE {'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
