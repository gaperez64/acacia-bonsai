#!/usr/bin/env python3
"""Check the production parity arm and exported timing against generated specifications."""

import argparse
import hashlib
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


def check_records(directory, expected_calls=None, synthesis=False, formula=False):
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
    if formula:
        digest = hashlib.sha256(identity["formula"].encode()).hexdigest()
        assert digest == identity["formula_sha256"], identity
    else:
        assert "formula" not in identity, identity
    return tier["tier"], (identity["formula_sha256"], identity["alphabet_sha256"])



def check_oxidd(binary, checker, root, integration):
    arm = "both:parity:oxidd"
    phases = root / "oxidd-phases"
    phases.mkdir()
    source = root / "oxidd.tlsf"
    controller = root / "oxidd.aag"
    env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))

    def rows():
        return [json.loads(line) for path in phases.glob("*.jsonl")
                for line in path.read_text().splitlines()]

    def clear():
        controller.unlink(missing_ok=True)
        for path in phases.glob("*.jsonl"):
            path.unlink()

    def write(guarantee, semantics="Mealy", target="Mealy"):
        source.write_text(f'''INFO {{ TITLE: "native parity" SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ INPUTS {{ i; unused_input; }} OUTPUTS {{ o; unused_output; }}
GUARANTEES {{ {guarantee}; }} }}''')

    for formula, real in (("G(i -> X o)", True), ("F o", True), ("true", True),
                          ("false", False), ("G F (i && X o)", False),
                          ("F G (!i || X !o)", True)):
        write(formula)
        clear()
        reference = execute([binary, "-T", str(source), "--arms", "both:parity:spot"])
        for synthesis in (False, True):
            clear()
            command = [binary, "-T", str(source), "--arms", arm]
            if synthesis:
                command += ["-s", str(controller)]
            answer = execute(command, env)
            assert answer.returncode == reference.returncode == (0 if real else 1), answer
            records = rows()
            imported, = [row for row in records if row.get("event") == "dpa_import"]
            assert imported["reason"] == "none" and imported["edges"] and imported["labels"], records
            assert imported["states"] == imported["imported_states"], records
            assert any(row.get("event") == "parity_solved" and row.get("arm") == arm
                       for row in records), records
            forbidden = {"translate", "orient", "classify", "parity"}
            assert not any(row.get("stage", row.get("phase")) in forbidden
                           for row in records), records
            if synthesis and real:
                subprocess.run([checker, "--check-controller", str(controller),
                                formula.replace("true", "1").replace("false", "0"), "mealy"],
                               check=True, capture_output=True, text=True, timeout=20)
                circuit = Aiger.read(controller)
                assert set(circuit.input_names) == {"i", "unused_input"}, circuit
                assert set(circuit.output_names) == {"o", "unused_output"}, circuit
            else:
                assert not controller.exists(), answer
    # Reuse the incumbent timing matrix, with the native arm's effective-Mealy objective.
    for semantics in ("Mealy", "Moore", "Strict,Mealy", "Strict,Moore"):
        for target in ("Mealy", "Moore"):
            for guarantee in ("G (o <-> i)", "G (i -> X o)"):
                write(guarantee, semantics, target)
                clear()
                reference = execute([binary, "-T", str(source), "--arms", "both:parity:spot"])
                answer = execute([binary, "-T", str(source), "--arms", arm,
                                  "-s", str(controller)])
                assert reference.returncode == answer.returncode, (semantics, target, answer)
                if answer.returncode == 0:
                    original = guarantee
                    if not semantics.endswith(target):
                        original = original.replace("o", "X o") if target == "Moore" else original.replace("i", "X i")
                    subprocess.run([checker, "--check-controller", str(controller),
                                    original, target.lower()], check=True,
                                   capture_output=True, text=True, timeout=20)
    execute([binary, "-T", str(source), "--arms", "both:parity:spot,both:parity:oxidd"])
    duplicate = subprocess.run([binary, "-T", str(source), "--arms", f"{arm},{arm}"],
                               capture_output=True, text=True, timeout=20)
    assert duplicate.returncode == 3 and "duplicate" in duplicate.stderr.lower(), duplicate
    clear()
    unknown = subprocess.run([binary, "-f", "1", "-i", "i", "-o", "o", "--arms", arm],
                             capture_output=True, text=True, timeout=20)
    assert unknown.returncode == 2 and "UNKNOWN" in unknown.stderr, unknown
    write("F G F o")
    unknown = subprocess.run([binary, "-T", str(source), "--arms", arm,
                              "-s", str(controller)], env=env,
                             capture_output=True, text=True, timeout=20)
    assert unknown.returncode == 2 and "UNKNOWN" in unknown.stderr, unknown
    assert not controller.exists(), unknown
    assert any(row.get("reason") == "dpa_unsupported" for row in rows()), rows()
    if integration:
        write("X X X o")
        for setting, reason, stage in (("ACACIA_TEST_DPA_STATE_LIMIT", "dpa_state_limit", "expand"),
                                       ("ACACIA_TEST_DPA_IMPORT_ALLOCATION", "dpa_host_limit", "import")):
            clear()
            unknown = subprocess.run([str(integration.resolve()), "-T", str(source),
                                      "--arms", arm, "-s", str(controller)],
                                     env=dict(env, **{setting: "1"}),
                                     capture_output=True, text=True, timeout=20)
            assert unknown.returncode == 2 and "UNKNOWN" in unknown.stderr, unknown
            records = rows()
            imported, = [row for row in records if row.get("event") == "dpa_import"]
            assert imported["edges"] and imported["labels"], imported
            assert imported["reason"] == reason and imported["stage"] == stage, imported
            assert not controller.exists(), unknown
            assert not any(row.get("event") in {"parity_solved", "parity_verdict"}
                           or row.get("stage", row.get("phase")) == "solve"
                           for row in records), records


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
        if args.tlsf:
            check_oxidd(binary, checker, root, args.integration_binary)
        # A quoted AP exercises JSON escaping and exceeds both record size limits.
        output = "long-output-" + "o" * 20000
        for case, (output, capture) in enumerate(((output, False), (output, True),
                                                ("line\u2028separator", True))):
            phases = root / f"formula-{case}"
            phases.mkdir()
            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
            env.pop("ACACIA_PHASE_RECORDS_FORMULA", None)
            if capture:
                env["ACACIA_PHASE_RECORDS_FORMULA"] = "1"
            execute([binary, "-f", f'G "{output}"', "-i", "i", "-o", output,
                     "--arms", "both:parity:spot"], env)
            check_records(phases, formula=capture)
        seen_tiers = set()
        for moore in ((False, True) if args.tlsf else (False,)):
            for formula in fixtures:
                phases = root / "phases"
                phases.mkdir(exist_ok=True)
                env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
                env.pop("ACACIA_PHASE_RECORDS_FORMULA", None)
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
        for wrong in ("real:parity:spot", "both:parity:backward", "both:parity:spot:frozen-graph",
                      "real:parity:oxidd", "both:parity:oxidd:frozen-graph"):
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
                                env.pop("ACACIA_PHASE_RECORDS_FORMULA", None)
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
