#!/usr/bin/env python3
"""Compare generated parity routes with the current Acacia frontend, serially."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def execute(argv):
    process = subprocess.run(argv, capture_output=True, text=True, timeout=20, check=False)
    if process.returncode not in (0, 1):
        raise AssertionError(f"{argv}: {process.stderr}\n{process.stdout}")
    verdicts = [line for line in process.stdout.splitlines()
                if line in ("REALIZABLE", "UNREALIZABLE")]
    if len(verdicts) != 1:
        raise AssertionError(f"missing verdict: {argv}: {process.stdout}")
    return verdicts[0], process.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("route", type=Path)
    parser.add_argument("acacia", type=Path)
    args = parser.parse_args()
    formulas = ["1", "0", "G o", "F o", "GF o", "GF i", "FG o", "G(i <-> o)",
                "G(i -> X o)", "GF i -> GF o", "(GF i & GF o) | (FG !i & FG !o)"]
    with tempfile.TemporaryDirectory(prefix="parity-route-") as directory:
        for moore in (False, True):
            for index, formula in enumerate(formulas):
                # Acacia's raw LTL frontend is Mealy. Its TLSF Moore lowering
                # replaces input atoms by X(input) once; the route uses native order.
                effective = formula.replace("i", "X i") if moore else formula
                reference, _ = execute([str(args.acacia), "-f", effective, "-i", "i", "-o", "o"])
                game = Path(directory) / f"{moore}-{index}.json"
                verdict, output = execute([
                    str(args.route), "--formula", formula, "--inputs", "i", "--outputs", "o",
                    "--semantics", "moore" if moore else "mealy", "--verify", "on", "--cross-check",
                    "--game-output", str(game),
                ])
                if verdict != reference:
                    raise AssertionError(f"verdict mismatch: moore={moore}: {formula}")
                summary = next(json.loads(line) for line in output.splitlines()
                               if line.startswith('{"event":"result"'))
                if not summary["checked"]:
                    raise AssertionError("unverified verdict")
                if summary["tier"] < 4 and summary["route"] != "acacia":
                    raise AssertionError("simple acceptance bypassed Acacia")
                graph = json.loads(game.read_text())
                if not graph["vertices"] or any(v["winner"] not in (0, 1)
                                                 for v in graph["vertices"]):
                    raise AssertionError("missing winning regions")
                for vertex in graph["vertices"]:
                    if vertex["owner"] == vertex["winner"] and (
                            vertex["strategy"] not in vertex["successors"]):
                        raise AssertionError("missing positional strategy")
    print("22 generated Mealy/Moore verdicts agree with Acacia")


if __name__ == "__main__":
    main()
