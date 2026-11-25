#!/usr/bin/env python3
"""Verify decomposed fast-path controllers with output don't-cares (#466)."""

from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile


def main():
    binary, checker, mode, case = sys.argv[1:]
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        source = Path(__file__).parent / "tlsf" / "dc-outputs" / f"{case}.tlsf"
        if case == "two-components":
            source = directory / "input.tlsf"
            source.write_text('''
INFO { TITLE: "two components" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { a; b; } OUTPUTS { x; y; unused; }
       GUARANTEE { G (x <-> (a && b)); G y; } }
''')
        circuit = directory / "controller.aag"
        phases = directory / "phases"
        phases.mkdir()
        result = subprocess.run(
            [binary, "--arms", "real:small:backward", "--spot-fast", "det",
             "-T", str(source), "-s", str(circuit), "--aiger-mode", mode],
            capture_output=True, text=True, timeout=10,
            env=dict(os.environ, ACACIA_PHASE_RECORDS=str(phases)))
        assert result.returncode == 0, result
        records = [json.loads(line) for file in phases.glob("*.jsonl")
                   for line in file.read_text().splitlines()]
        assert sum(row.get("event") == "route_selected" and row.get("route") == "spot_fast"
                   for row in records) >= 2, records
        checked = subprocess.run([checker, str(source), str(circuit)],
                                 capture_output=True, text=True, timeout=10)
        assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
        print(f"{case} {mode}: verified")


if __name__ == "__main__":
    main()
