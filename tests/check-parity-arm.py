#!/usr/bin/env python3
"""Check the production parity arm and exported timing against generated specifications."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from moore_aiger_oracle import Aiger
from obligation_test_support import assert_parse_calls


def execute(command, env=None):
    result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=20)
    assert result.returncode in (0, 1), (command, result.stdout, result.stderr)
    verdicts = [line for line in result.stdout.splitlines()
                if line in {"REALIZABLE", "UNREALIZABLE"}]
    assert len(verdicts) == 1, result
    return result


def check_records(directory, expected_calls=None, synthesis=False):
    rows = [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines()]
    assert_parse_calls(rows, expected_calls or {})
    tier, = [row for row in rows if row.get("event") == "parity_tier"]
    solved, = [row for row in rows if row.get("event") == "parity_solved"]
    verdict, = [row for row in rows if row.get("event") == "parity_verdict"]
    assert tier["tier"] == solved["tier"] == verdict["tier"] in range(1, 5), rows
    expected_route, expected_backend = (("deterministic_buchi", "spot-fast")
                                        if tier["tier"] < 4 else ("acd_parity", "spot"))
    selected, = [row for row in rows if row.get("event") == "route_selected"
                 and row.get("route") == expected_route]
    assert selected["effective_backend"] == expected_backend, rows
    assert selected["seq"] < tier["seq"] < solved["seq"] < verdict["seq"], rows
    if tier["tier"] == 4:
        solve, = [row for row in rows if row.get("event") == "stage_entry"
                  and row.get("stage") == "solve"]
        assert tier["seq"] < solve["seq"] < solved["seq"], rows
    if not synthesis:
        assert not any(row.get("phase", row.get("stage")) in {"strategy", "encode-controller"}
                       for row in rows), rows
    assert not any(row.get("event") == "verification_complete"
                   or row.get("stage", "").startswith("verify-") for row in rows), rows
    for row in [tier, solved, verdict]:
        assert row["trust"] == "exact_game", row
        assert not row["same_library_checked"] and not row["independent_checked"], row
        assert row["worker_pid"] == solved["worker_pid"], row
    identity, = [row for row in rows if row.get("event") == "work_identity"
                 and row.get("provider") == "spot-parity"]
    return tier["tier"], (identity["formula_sha256"], identity["alphabet_sha256"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("checker", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--tlsf", action="store_true")
    parser.add_argument("--integration-binary", type=Path)
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
                tier, _ = check_records(phases, {"parity": 1} if moore else {})
                seen_tiers.add(tier)
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
            for semantics in ("Mealy", "Moore", "Strict,Mealy", "Strict,Moore"):
                for target in ("Mealy", "Moore"):
                    for guarantee, original, real in (
                            ("G (o <-> i)", "G(o <-> i)", semantics.endswith("Mealy")),
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
                        selections = [(binary, "both:parity:spot", {}, {"parity": 1})]
                        if args.integration_binary:
                            portfolio = "real:small:backward,both:parity:spot"
                            integration = str(args.integration_binary.resolve())
                            selections += [
                                (integration, portfolio, {"ACACIA_TEST_CHILD_MODES": "stall,"},
                                 {"parent": 1}),
                                (integration, portfolio, {"ACACIA_TEST_TLSF_PARENT_FAILURE": "1"},
                                 {"parent": 1, "parity": 1}),
                            ]
                        original_identity = None
                        original_alphabet = None
                        for executable, arms, extra_env, expected_calls in selections:
                            for synthesis in (False, True):
                                for path in phases.glob("*.jsonl"):
                                    path.unlink()
                                controller.unlink(missing_ok=True)
                                env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases), **extra_env)
                                command = [executable, "-T", str(source), "--arms", arms]
                                if synthesis:
                                    command += ["-s", str(controller)]
                                made = execute(command, env)
                                assert (made.returncode == 0) == real, (
                                    semantics, target, guarantee, made)
                                _, identity = check_records(phases, expected_calls, synthesis)
                                if original_identity is None:
                                    original_identity = identity
                                assert identity == original_identity, (identity, original_identity)
                                if synthesis and real:
                                    circuit = Aiger.read(controller)
                                    alphabet = circuit.input_names, circuit.output_names
                                    assert set(alphabet[0]) == {"i", "unused_input"}, alphabet
                                    assert set(alphabet[1]) == {"o", "unused_output"}, alphabet
                                    if original_alphabet is None:
                                        original_alphabet = alphabet
                                    assert alphabet == original_alphabet, (
                                        alphabet, original_alphabet)
                                    # Check original target timing independently of the solver.
                                    specification = original
                                    if not semantics.endswith(target):
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
