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
                "FG !o | FG !p", "FG !i | FG !j",
                "GF ((i & X o) | (j & X p)) & (GF i -> GF p)"]
    with tempfile.TemporaryDirectory(dir=args.scratch) as temporary:
        root = Path(temporary)
        default_results = {}
        reached = set()
        final_bounds = set()
        for engine in ("spot-game", "antichain", "strpm-simd"):
            for formula in fixtures:
                phases = root / "phases"
                phases.mkdir(exist_ok=True)
                env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
                command = [binary, "-f", formula, "-i", "i,j,unused_input",
                           "-o", "o,p,unused_output", "--arms", "both:parity:spot"]
                if engine == "antichain":
                    # A nondefault schedule must reach the actual winning bound.
                    command += ["--parity-buchi-engine", engine, "-M", "5", "-K", "11", "-I", "2"]
                elif engine == "strpm-simd":
                    command += ["--parity-buchi-engine", engine]
                decision = execute(command, env)
                if engine == "spot-game":
                    default_results[formula] = decision.stdout
                    explicit = execute([*command, "--parity-buchi-engine", engine])
                    assert explicit.stdout == decision.stdout
                else:
                    assert decision.stdout == default_results[formula], (formula, decision)
                rows = [json.loads(line) for path in phases.glob("*.jsonl")
                        for line in path.read_text().splitlines()]
                solved, = [row for row in rows if row.get("event") == "parity_solved"]
                verdict, = [row for row in rows if row.get("event") == "parity_verdict"]
                tier, = [row for row in rows if row.get("event") == "parity_tier"]
                reached.add(tier["tier"])
                expected = "strpm-simd" if tier["tier"] == 4 else (
                    engine if engine != "spot-game" else "spot-fast")
                assert all(row["route"] == row["effective_backend"] == expected
                           for row in (tier, solved, verdict)), rows
                assert solved["tier"] == verdict["tier"] == tier["tier"], rows
                fields = ("k", "t", "pairs", "fallback", "fallback_reason", "effective_backend")
                assert all(verdict[key] == solved[key] for key in fields), rows
                if tier["tier"] == 4 or engine != "spot-game":
                    parameters = [row for row in rows if row.get("event") == "parity_parameters"]
                    assert parameters and all(parameters[-1][key] == verdict[key]
                                              for key in ("k", "t", "pairs")), rows
                    if verdict["fallback"]:
                        fallback, = [row for row in rows
                                     if row.get("event") == "parity_fallback_end"]
                        assert all(fallback[key] == verdict[key] for key in fields), rows
                    assert verdict["k"] > 0
                    if tier["tier"] == 4 or engine == "strpm-simd":
                        assert verdict["t"] > 0 and verdict["pairs"] > 0
                        final_bounds.add((verdict["k"], verdict["t"]))
                    else:
                        assert 5 <= verdict["k"] <= 11 and (verdict["k"] - 5) % 2 == 0
                assert not any(row.get("phase") in {"strategy", "encode-controller"} for row in rows)
                assert not any(row.get("event") == "verification_complete" for row in rows)
                assert solved["trust"] == ("exact_antichain" if expected == "antichain"
                                           else "exact_game")
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
        assert reached == {1, 2, 3, 4}, ("engine matrix missed a tier", reached)
        assert any(k > 1 for k, _ in final_bounds), "adaptive k never increased"
        assert any(t > 1 for _, t in final_bounds), "adaptive t never increased"
        wrong_engine = subprocess.run([binary, "-f", "1", "--parity-buchi-engine", "unknown"],
                                      capture_output=True, text=True, timeout=20)
        assert wrong_engine.returncode == 3 and "spot-game, antichain, or strpm-simd" in wrong_engine.stderr
        for engine in ("spot-game", "antichain", "strpm-simd"):
            phases = root / "repeat-phases"
            phases.mkdir(exist_ok=True)
            command = [binary, "-f", "G o", "-i", "", "-o", "o", "--arms", "both:parity:spot"]
            for previous in ("spot-game", "antichain", "strpm-simd"):
                command += ["--parity-buchi-engine", previous]
            execute([*command, "--parity-buchi-engine", engine],
                    dict(os.environ, ACACIA_PHASE_RECORDS=str(phases)))
            rows = [json.loads(line) for path in phases.glob("*.jsonl")
                    for line in path.read_text().splitlines()]
            solved, = [row for row in rows if row.get("event") == "parity_solved"]
            assert solved["effective_backend"] == ("spot-fast" if engine == "spot-game" else engine)
            for path in phases.glob("*.jsonl"):
                path.unlink()
        for wrong in ("real:parity:spot", "both:parity:backward", "both:parity:spot:frozen-graph"):
            result = subprocess.run([binary, "-f", "1", "--arms", wrong],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 3 and "parity arms require" in result.stderr, result
        if args.tlsf:
            for semantics in ("Mealy", "Moore"):
                for target in ("Mealy", "Moore"):
                    for signal in ("i", "o"):
                        source = root / "indexed.tlsf"
                        source.write_text(f'''INFO {{ TITLE: "indexed objective" SEMANTICS: {semantics}
TARGET: {target} }}
GLOBAL {{ PARAMETERS {{ n = 3; }} }}
MAIN {{ INPUTS {{ i[n]; }} OUTPUTS {{ o[n]; }}
GUARANTEES {{ &&[0 <= j < n] G {signal}[j]; }} }}
''')
                        decisions = []
                        for engine in ("spot-game", "antichain", "strpm-simd"):
                            phases = root / "indexed-phases"
                            phases.mkdir(exist_ok=True)
                            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
                            decision = execute([binary, "-T", str(source), "--arms", "both:parity:spot",
                                                "--parity-buchi-engine", engine,
                                                "-M", "5", "-K", "5"], env)
                            assert decision.returncode == (1 if signal == "i" else 0), (
                                semantics, target, signal, engine, decision)
                            decisions.append(decision.stdout)
                            controller = root / "indexed.aag"
                            controller.unlink(missing_ok=True)
                            made = execute([binary, "-T", str(source), "--arms", "both:parity:spot",
                                            "--parity-buchi-engine", engine, "-s", str(controller)])
                            assert made.stdout == decision.stdout, (engine, made, decision)
                            if signal == "o":
                                shift = "X " if target != semantics and target == "Moore" else ""
                                specification = " & ".join(f"G({shift}o_{j})" for j in range(3))
                                subprocess.run([checker, "--check-controller", str(controller),
                                                specification, target.lower()], check=True,
                                               capture_output=True, text=True, timeout=20)
                            else:
                                assert not controller.exists(), made
                            rows = [json.loads(line) for path in phases.glob("*.jsonl")
                                    for line in path.read_text().splitlines()]
                            tier, = [row for row in rows if row.get("event") == "parity_tier"]
                            assert tier["tier"] == 3, (semantics, target, signal, engine, rows)
                            for path in phases.glob("*.jsonl"):
                                path.unlink()
                        assert len(set(decisions)) == 1, (semantics, target, signal, decisions)
            for engine in ("spot-game", "antichain", "strpm-simd"):
                for semantics in ("Mealy", "Moore"):
                    for target in ("Mealy", "Moore"):
                        for guarantee, original, real in (
                                ("G (o <-> i)", "G(o <-> i)", semantics == "Mealy"),
                                ("G (i -> X o)", "G(i -> X o)", True),
                                ("F G !o", "FG !o", True),
                                ("F G !i", "FG !i", False),
                                ("F G !o || F G !i", "FG !o | FG !i", True),
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
                                            "--parity-buchi-engine", engine, "-s", str(controller)])
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
