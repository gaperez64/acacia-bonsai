#!/usr/bin/env python3
"""Check bounded parity reductions, independent certificates, and decision no-op."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


MODES = ("none", "bisim", "bisim-output", "reselect")


def run(binary, *arguments, env=None):
    return subprocess.run([binary, *arguments], capture_output=True, text=True,
                          timeout=20, env=env)


def main():
    binary, checker = sys.argv[1:]
    assert "--parity-mealy-reduction MODE" in run(binary, "--help").stdout
    for mode in ("", "BISIM", "output", "2", "bisim-output,none"):
        answer = run(binary, "--parity-mealy-reduction", mode)
        assert answer.returncode == 3, answer
        assert "invalid --parity-mealy-reduction" in answer.stderr, answer
        assert not answer.stdout, answer
    missing = run(binary, "--parity-mealy-reduction")
    assert missing.returncode == 3 and "requires an argument" in missing.stderr, missing
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, circuit = root / "spec.tlsf", root / "controller.aag"
        # Safety, liveness, tier four, and native target adaptation, with unused APs.
        for semantics, target, formula in (
                ("Mealy", "Mealy", "G (i -> X o)"),
                ("Mealy", "Mealy", "G F o"),
                ("Mealy", "Mealy", "G F i"),
                ("Mealy", "Mealy", "(G F i && G F o) || (F G !i && F G !o)"),
                ("Moore", "Moore", "G (i -> X o)"),
                ("Strict,Moore", "Moore", "G F o"),
                ("Mealy", "Moore", "G (i -> X o)")):
            source.write_text(f'''INFO {{ TITLE: "reduction" SEMANTICS: {semantics} TARGET: {target} }}
MAIN {{ INPUTS {{ i; unused_input; }} OUTPUTS {{ o; unused_output; }}
GUARANTEES {{ {formula}; }} }}''')
            command = ["--arms", "both:parity:spot", "-T", str(source)]
            expected_code = 1 if formula == "G F i" else 0
            reference = None
            for mode in MODES:
                phases = root / "phases"
                phases.mkdir(exist_ok=True)
                for path in phases.glob("*.jsonl"):
                    path.unlink()
                answer = run(binary, *command, "--parity-mealy-reduction", mode,
                             env=dict(os.environ, ACACIA_PHASE_RECORDS=str(phases)))
                assert answer.returncode == expected_code, answer
                rows = [json.loads(line) for path in phases.glob("*.jsonl")
                        for line in path.read_text().splitlines()]
                stages = [(row["event"], row.get("stage"), row.get("states"), row.get("edges"))
                          for row in rows if row.get("event") in
                          {"stage_entry", "stage_completion"}]
                assert stages, rows
                assert not any(stage in {"strategy", "reduce-controller", "reselect-controller",
                                         "encode-controller"}
                               for _, stage, _, _ in stages), rows
                if reference is None:
                    reference = stages
                assert stages == reference, (mode, stages, reference)
                circuit.unlink(missing_ok=True)
                answer = run(binary, *command, "--parity-mealy-reduction", mode,
                             "-s", str(circuit))
                assert answer.returncode == expected_code, answer
                if expected_code:
                    assert not circuit.exists(), answer
                    continue
                checked = run(checker, str(source), str(circuit))
                assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            if expected_code:
                continue
            default = run(binary, *command, "-s", str(circuit))
            assert default.returncode == 0, default
            expected = circuit.read_bytes()
            explicit = run(binary, *command, "-s", str(circuit),
                           "--parity-mealy-reduction", "reselect")
            assert explicit.returncode == 0 and circuit.read_bytes() == expected, explicit
    print("Parity reduction grammar, unchanged decision stages, and independent controllers passed")


if __name__ == "__main__":
    main()
