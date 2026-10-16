#!/usr/bin/env python3
"""Check the production parity arm and exported timing against generated specifications."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def execute(command, env=None):
    result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=20)
    assert result.returncode in (0, 1), (command, result.stdout, result.stderr)
    verdicts = [line for line in result.stdout.splitlines()
                if line in {"REALIZABLE", "UNREALIZABLE"}]
    assert len(verdicts) == 1, result
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("checker", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--route", type=Path)
    parser.add_argument("--tlsf", action="store_true")
    args = parser.parse_args()
    binary, checker = str(args.binary.resolve()), str(args.checker.resolve())
    fixtures = ["1", "0", "G o", "F o", "GF o", "GF i", "FG o", "FG i", "G(i <-> o)",
                "G(i -> X o)", "GF i -> GF o", "(GF i & GF o) | (FG !i & FG !o)",
                "FG !o | FG !p", "FG !i | FG !j"]
    with tempfile.TemporaryDirectory(dir=args.scratch) as temporary:
        root = Path(temporary)
        for formula in fixtures:
            phases = root / "phases"
            phases.mkdir(exist_ok=True)
            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
            command = [binary, "-f", formula, "-i", "i,j,unused_input",
                       "-o", "o,p,unused_output", "--arms", "both:parity:spot"]
            decision = execute(command, env)
            rows = [json.loads(line) for path in phases.glob("*.jsonl")
                    for line in path.read_text().splitlines()]
            solved = [row for row in rows if row.get("event") == "parity_solved"]
            assert len(solved) == 1, rows
            assert solved[0]["tier"] in range(1, 5)
            assert not any(row.get("phase") in {"strategy", "encode-controller"} for row in rows)
            assert not any(row.get("event") == "verification_complete" for row in rows)
            assert solved[0]["trust"] == "exact_game"
            if args.route:
                reference = execute([str(args.route.resolve()), "--formula", formula,
                                     "--inputs", "i,j,unused_input", "--outputs", "o,p,unused_output",
                                     "--verify", "on", "--cross-check"])
                assert reference.returncode == decision.returncode, (formula, reference, decision)
            controller = root / "controller.aag"
            controller.unlink(missing_ok=True)
            made = execute([*command, "-s", str(controller)])
            assert made.returncode == decision.returncode, (formula, made, decision)
            if made.returncode == 0:
                subprocess.run([checker, "--check-controller", str(controller), formula, "mealy"],
                               check=True, capture_output=True, text=True, timeout=20)
            else:
                assert not controller.exists(), (formula, made)
            for path in phases.glob("*.jsonl"):
                path.unlink()
        for wrong in ("real:parity:spot", "both:parity:backward", "both:parity:spot:frozen-graph"):
            result = subprocess.run([binary, "-f", "1", "--arms", wrong],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 3 and "parity arms require" in result.stderr, result
        if args.tlsf:
            for semantics in ("Mealy", "Moore"):
                for target in ("Mealy", "Moore"):
                    for guarantee, original, real in (
                            ("G (o <-> i)", "G(o <-> i)", semantics == "Mealy"),
                            ("G (i -> X o)", "G(i -> X o)", True),
                            ("G F o", "GF o", True),
                            ("G F i", "GF i", False)):
                        source = root / "spec.tlsf"
                        source.write_text(f'''INFO {{ TITLE: "generated" SEMANTICS: {semantics}
TARGET: {target} }}
MAIN {{ INPUTS {{ i; unused_input; }} OUTPUTS {{ o; unused_output; }}
GUARANTEES {{ {guarantee}; }} }}
''')
                        controller = root / "timing.aag"
                        controller.unlink(missing_ok=True)
                        made = execute([binary, "-T", str(source), "--arms", "both:parity:spot",
                                        "-s", str(controller)])
                        assert (made.returncode == 0) == real, (semantics, target, guarantee, made)
                        if real:
                            # Changing target preserves source semantics by shifting one block.
                            specification = original
                            if semantics != target:
                                if target == "Moore":
                                    specification = specification.replace("o", "X o")
                                else:
                                    specification = specification.replace("i", "X i")
                            subprocess.run([checker, "--check-controller", str(controller),
                                            specification, target.lower()], check=True,
                                           capture_output=True, text=True, timeout=20)
                        else:
                            assert not controller.exists(), made
    print("Production decisions, research agreement, controller products and TLSF timing passed")


if __name__ == "__main__":
    main()
