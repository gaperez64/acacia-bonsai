#!/usr/bin/env python3
"""Exercise one-sided admission, fallback, alpha-renaming and decision-only synthesis."""

import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
ATTRIBUTION = runpy.run_path(str(ROOT / "tests/check-attribution.py"))


def main():
    binary, closure_binary = (Path(arg).resolve() for arg in sys.argv[1:3])
    obfuscator = os.environ.get("ACACIA_TLSFOBF") or shutil.which("tlsfobf")
    with tempfile.TemporaryDirectory(prefix="restriction-") as temporary:
        root = Path(temporary)
        sequence = 0

        def run(source, arm, *, enabled=True, synthesis=False, capture=False, solver=binary):
            nonlocal sequence
            sequence += 1
            directory = root / str(sequence)
            directory.mkdir()
            env = dict(os.environ, ACACIA_PHASE_RECORDS=str(directory),
                       ACACIA_PHASE_INVOCATION=f"restriction-{sequence}")
            command = [str(solver), "-T", str(source), "--arms", arm, "-K", "4",
                       "--restriction-prepass", "on" if enabled else "off"]
            if capture:
                env["ACACIA_SPOT_CAPTURE_DIR"] = str(directory / "captures")
                command += ["--spot-fast", "off", "--weakening", "off",
                            "--candidate-mode", "only"]
            controller = directory / "controller.aag"
            if synthesis:
                command += ["-s", str(controller)]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
            records = ATTRIBUTION["rows"](directory)
            ATTRIBUTION["complete_records"](
                records, expected_winners=int(result.returncode in (0, 1)))
            attempts = [r for r in records if r.get("event") == "restriction_attempt"]
            # No candidate reconverts TLSF; only the parent's original conversion is needed.
            assert len([r for r in records if r.get("event") == "tlsf_parse"]) == 1, records
            return result, attempts, controller

        fixtures = ROOT / "tests/fixtures/restrictions"
        for filename, verdict, arm in (
            ("recurrence-input.tlsf", 1, "unreal:formula:backward"),
            ("pair-input.tlsf", 1, "unreal:formula:backward"),
            ("scalar-output.tlsf", 0, "real:small:backward"),
        ):
            source = fixtures / filename
            result, attempts, _ = run(source, arm)
            assert result.returncode == verdict, result
            assert any(r["outcome"] == "accepted" for r in attempts), (result, attempts)
            expected = [(r["candidate"], r["family"], r["polarity"], r["outcome"])
                        for r in attempts]
            off, attempts_off, _ = run(source, arm, enabled=False)
            assert off.returncode == verdict and not attempts_off, (off, attempts_off)
            if obfuscator:
                renamed = root / filename
                subprocess.run([obfuscator, "--mode", "rename", "--seed", "451", "--output",
                                str(renamed), str(source)], check=True, capture_output=True,
                               text=True, timeout=10)
                obfuscated, renamed_attempts, _ = run(renamed, arm)
                assert obfuscated.returncode == verdict, obfuscated
                assert [(r["candidate"], r["family"], r["polarity"], r["outcome"])
                        for r in renamed_attempts] == expected, renamed_attempts

        for name, inputs, outputs, objective, arm, verdict in (
            ("input-polarity", "p[2];", "o;", "p[0] <-> p[1]",
             "unreal:formula:backward", 1),
            ("output-polarity", "i;", "p;", "p", "real:small:backward", 0),
        ):
            source = root / (name + ".tlsf")
            source.write_text('INFO { TITLE: "polarity" SEMANTICS: Mealy TARGET: Mealy }\n'
                              f'MAIN {{ INPUTS {{ {inputs} }} OUTPUTS {{ {outputs} }} '
                              f'GUARANTEES {{ {objective}; }} }}\n')
            result, attempts, _ = run(source, arm)
            assert result.returncode == verdict, result
            assert attempts and all(r["outcome"] != "accepted" for r in attempts), attempts

        def causal_frame(objective, *, enabled):
            source = root / "causal-frame.tlsf"
            source.write_text('INFO { TITLE: "causal frame" SEMANTICS: Mealy TARGET: Mealy }\n'
                              'MAIN { INPUTS { p[2]; } OUTPUTS { o; } '
                              f'GUARANTEES {{ {objective}; }} }}\n')
            result, attempts, controller = run(
                source, "unreal:formula:spot-guarded-sparse:closure-buchi",
                enabled=enabled, capture=True, solver=closure_binary)
            captures = [json.loads(p.read_text())
                        for p in (controller.parent / "captures").glob("*.json")]
            assert len(captures) == 1, (result, attempts, captures)
            worker = captures[0]
            assert worker["route"] == "closure-buchi" and worker["transform"] == "formula", worker
            assert worker["inputs"] == ["o"] and worker["outputs"] == ["p_0", "p_1"], worker
            assert float(worker["verification_ms"]) >= 0, worker
            return result, attempts, worker

        # Closure skips the direct-strategy shortcut and reaches the transformed unreal game.
        result, attempts, worker = causal_frame("X (o <-> (p[0] || p[1]))", enabled=True)
        assert result.returncode == 2 and worker["status"] == "LOSE_K", (result, worker)
        assert attempts and all(r["outcome"] != "accepted" for r in attempts), attempts
        assert worker["worker_formula"] == "X((p_0 | p_1) <-> Xo)", worker
        # Mutation: shifting o in o <-> X P gives Xo <-> X P, equivalent to the first
        # objective with its worker shift omitted. That game wrongly proves UNREAL.
        mutant, _, worker = causal_frame("o <-> X (p[0] || p[1])", enabled=False)
        assert worker["worker_formula"] == "X(p_0 | p_1) <-> Xo", worker
        assert mutant.returncode == 1 and worker["status"] == "WIN_K", (mutant, worker)

        result, attempts, controller = run(fixtures / "scalar-output.tlsf",
                                            "real:small:backward", synthesis=True)
        assert result.returncode == 0 and not attempts, result
        text = controller.read_text().splitlines()
        assert text[0].startswith("aag ") and int(text[0].split()[4]) == 1, text
        assert "o0 all_finished" in text, text

        invalid = subprocess.run([str(binary), "--restriction-prepass=invalid"],
                                 capture_output=True, text=True, timeout=10)
        assert invalid.returncode == 3 and "expects on or off" in invalid.stderr, invalid
    print("restriction CLI, polarity, complete synthesis outputs and telemetry passed; "
          + ("tlsfobf rename parity passed" if obfuscator else "tlsfobf unavailable: rename skipped"))


if __name__ == "__main__":
    main()
