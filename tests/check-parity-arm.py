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


def check_records(directory):
    rows = [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines()]
    tier, = [row for row in rows if row.get("event") == "parity_tier"]
    solved, = [row for row in rows if row.get("event") == "parity_solved"]
    verdict, = [row for row in rows if row.get("event") == "parity_verdict"]
    assert tier["tier"] == solved["tier"] == verdict["tier"] in range(1, 5), rows
    expected_route, expected_backend = (("deterministic_buchi", "spot-fast")
                                        if tier["tier"] < 4 else ("acd_parity", "strpm-simd"))
    selected, = [row for row in rows if row.get("event") == "route_selected"
                 and row.get("route") == expected_route]
    assert selected["effective_backend"] == expected_backend, rows
    assert selected["seq"] < tier["seq"] < solved["seq"] < verdict["seq"], rows
    parameters = [row for row in rows if row.get("event") == "parity_parameters"]
    if tier["tier"] == 4:
        assert parameters, rows
        solve, = [row for row in rows if row.get("event") == "stage_entry"
                  and row.get("stage") == "solve"]
        assert tier["seq"] < solve["seq"] < parameters[0]["seq"], rows
        assert parameters[-1]["seq"] < solved["seq"], rows
        assert [row["pairs"] for row in parameters] == list(range(1, len(parameters) + 1)), rows
        assert all(row["k"] > 0 and row["t"] > 0 and row["tier"] == 4
                   for row in parameters), rows
        assert tuple(parameters[-1][key] for key in ("k", "t", "pairs")) == tuple(
            solved[key] for key in ("k", "t", "pairs")), rows
    else:
        assert not parameters, rows
    fallback = [row for row in rows if row.get("event", "").startswith("parity_fallback_")]
    if solved["fallback"]:
        assert [row["event"] for row in fallback] == ["parity_fallback_start",
                                                     "parity_fallback_end"], rows
        assert fallback[0]["seq"] < fallback[1]["seq"] < solved["seq"], rows
        assert fallback[0]["mono_ns"] <= fallback[1]["mono_ns"], rows
        for row in fallback:
            assert row["fallback"] and row["fallback_reason"], row
            assert all(row[key] == solved[key] for key in ("k", "t", "pairs", "tier",
                                                          "fallback_reason")), rows
    else:
        assert not fallback, rows
    assert not any(row.get("phase", row.get("stage")) in {"strategy", "encode-controller"}
                   for row in rows), rows
    assert not any(row.get("event") == "verification_complete"
                   or row.get("stage", "").startswith("verify-") for row in rows), rows
    for row in [tier, *parameters, *fallback, solved, verdict]:
        assert row["trust"] == "exact_game", row
        assert not row["same_library_checked"] and not row["independent_checked"], row
        assert row["worker_pid"] == solved["worker_pid"], row
    return tier["tier"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("checker", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--tlsf", action="store_true")
    args = parser.parse_args()
    binary, checker = str(args.binary.resolve()), str(args.checker.resolve())
    fixtures = ["1", "0", "G o", "F o", "GF o", "GF i", "FG o", "FG i", "G(i <-> o)",
                "G(i -> X o)", "GF i -> GF o", "(GF i & GF o) | (FG !i & FG !o)",
                "FG !o | FG !p", "FG !i | FG !j"]
    with tempfile.TemporaryDirectory(dir=args.scratch) as temporary:
        root = Path(temporary)
        seen_tiers = set()
        for moore in ((False, True) if args.tlsf else (False,)):
            for formula in fixtures:
                phases = root / "phases"
                phases.mkdir(exist_ok=True)
                env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
                effective = formula.replace("i", "X i").replace("j", "X j") if moore else formula
                interface = ["-i", "i,j,unused_input", "-o", "o,p,unused_output"]
                source = ["-f", formula, *interface]
                if moore:
                    tlsf = root / "moore.tlsf"
                    guarantee = formula.replace("GF", "G F").replace("FG", "F G")
                    guarantee = guarantee.replace("1", "true").replace("0", "false")
                    guarantee = guarantee.replace("&", "&&").replace("|", "||")
                    tlsf.write_text(f'''INFO {{ TITLE: "generated" SEMANTICS: Moore TARGET: Mealy }}
MAIN {{ INPUTS {{ i; j; unused_input; }} OUTPUTS {{ o; p; unused_output; }}
GUARANTEES {{ {guarantee}; }} }}
''')
                    source = ["-T", str(tlsf)]
                command = [binary, *source, "--arms", "both:parity:spot"]
                decision = execute(command, env)
                seen_tiers.add(check_records(phases))
                reference = execute([binary, "-f", effective, *interface, "--arms",
                                     "real:small:backward,unreal:formula:backward"])
                assert reference.returncode == decision.returncode, (moore, formula,
                                                                    reference, decision)
                controller = root / "controller.aag"
                controller.unlink(missing_ok=True)
                made = execute([*command, "-s", str(controller)])
                assert made.returncode == decision.returncode, (formula, made, decision)
                if made.returncode == 0:
                    subprocess.run([checker, "--check-controller", str(controller), effective,
                                    "mealy"], check=True, capture_output=True, text=True, timeout=20)
                else:
                    assert not controller.exists(), (formula, made)
                for path in phases.glob("*.jsonl"):
                    path.unlink()
        assert {1, 4} <= seen_tiers, seen_tiers
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
    print("Production route records, Acacia agreement, controller products and TLSF timing passed")


if __name__ == "__main__":
    main()
