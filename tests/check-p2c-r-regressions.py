#!/usr/bin/env python3
"""Recheck an evidence-selected R success set without collecting timing measurements."""
from __future__ import annotations

import argparse
import csv
import os
import signal
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--list", type=Path, required=True)
    parser.add_argument("--tlsf-map", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    args = parser.parse_args()
    with args.tlsf_map.open(newline="") as handle:
        sources = {row["instance"]: row["tlsf"]
                   for row in csv.DictReader(handle, delimiter="\t")}
    instances = [line for line in args.list.read_text().splitlines()
                 if line and not line.startswith("#")]
    assert instances and len(instances) == len(set(instances))
    env = os.environ.copy()
    env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
    env.pop("ACACIA_PHASE_RECORDS", None)
    env.pop("ACACIA_NATIVE_TEST_LIFT_FAULT", None)
    for instance in instances:
        source = args.corpus / sources[instance]
        assert source.is_file(), source
        for setting in ("off", "on"):
            with subprocess.Popen(
                    [str(args.binary.resolve()), "-T", str(source), "--arms",
                     "real:param-lift:oxidd", "--r-typed-roles", setting],
                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True, start_new_session=True) as process:
                try:
                    stdout, stderr = process.communicate(timeout=90)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.communicate()
                    raise
                assert process.returncode == 0 and stdout.strip() == "REALIZABLE", (
                    instance, setting, process.returncode, stdout, stderr)
        print(f"{instance}: checked R off/on REAL", flush=True)
    print(f"PASS: {len(instances)} incumbent R successes preserved")


if __name__ == "__main__":
    main()
