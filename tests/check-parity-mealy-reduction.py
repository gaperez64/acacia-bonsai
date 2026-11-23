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
    binary, checker, fixture = sys.argv[1:]
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
        for option, inputs, outputs, objective in (
                ("--budget-fallback", [f"i{n}" for n in range(6)],
                 [f"o{n}" for n in range(40)], "G F o0"),
                ("--reselection", ["i"], ["o"], "G F o")):
            phases = root / option.removeprefix("--")
            phases.mkdir()
            answer = run(fixture, option, str(circuit),
                         env=dict(os.environ, ACACIA_PHASE_RECORDS=str(phases)))
            assert answer.returncode == 0, answer
            rows = [json.loads(line) for path in phases.glob("*.jsonl")
                    for line in path.read_text().splitlines()]
            attempts = [row for row in rows if row.get("event") == "parity_reduction_attempt"]
            assert attempts and all(row["option"] in MODES and row["wall_ns"] > 0
                                    for row in attempts), rows
            if option == "--budget-fallback":
                assert len(attempts) == 1, attempts
                assert attempts[0]["outcome"] == "fallback", attempts
                assert attempts[0]["cause"] == "deadline", attempts
                assert attempts[0]["wall_ns"] < 500_000_000, attempts
                print(f'Adversarial deadline fallback: {attempts[0]["wall_ns"] / 1e6:.1f} ms')
            else:
                assert any(row["attempt"] == "reselect" and row["outcome"] == "accepted"
                           for row in attempts), attempts
            source.write_text('INFO { TITLE: "adversarial" SEMANTICS: Mealy TARGET: Mealy }\n'
                              f'MAIN {{ INPUTS {{ {"; ".join(inputs)}; }} '
                              f'OUTPUTS {{ {"; ".join(outputs)}; }} '
                              f'GUARANTEES {{ {objective}; }} }}')
            checked = run(checker, str(source), str(circuit))
            assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            if option == "--reselection":
                checked = run(checker, str(source), str(circuit) + ".incumbent")
                assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
        # Suspended competitors retain their charges: almost no space remains below 7/8.
        scope = root / "scope"
        scope.mkdir()
        limit = 5 * 1024**3
        threshold = limit - limit // 8
        for maximum, current, cause in (
                (str(limit), str(threshold - 1), "scope_headroom"),
                (str(limit), str(threshold), "scope_headroom"),
                (str(limit), str(limit), "scope_headroom"),
                ("invalid", "0", "scope_memory"),
                ("123junk", "0", "scope_memory"),
                (str(limit), None, "scope_memory")):
            (scope / "memory.max").write_text(maximum + "\n")
            (scope / "memory.current").unlink(missing_ok=True)
            if current is not None:
                (scope / "memory.current").write_text(current + "\n")
            phases = root / f"scope-{current}-{maximum}"
            phases.mkdir()
            answer = run(fixture, "--scope-fallback", str(circuit), str(scope),
                         env=dict(os.environ, ACACIA_PHASE_RECORDS=str(phases)))
            assert answer.returncode == 0, answer
            rows = [json.loads(line) for path in phases.glob("*.jsonl")
                    for line in path.read_text().splitlines()]
            attempts = [row for row in rows if row.get("event") == "parity_reduction_attempt"]
            assert len(attempts) == 1, rows
            assert attempts[0]["outcome"] == "skipped" and attempts[0]["cause"] == cause, attempts
            assert attempts[0]["child_pid"] == -1 and attempts[0]["memory_bytes"] == 0, attempts
            inputs, outputs = [f"i{n}" for n in range(6)], [f"o{n}" for n in range(40)]
            source.write_text('INFO { TITLE: "scope" SEMANTICS: Mealy TARGET: Mealy }\n'
                              f'MAIN {{ INPUTS {{ {"; ".join(inputs)}; }} '
                              f'OUTPUTS {{ {"; ".join(outputs)}; }} GUARANTEES {{ G F o0; }} }}')
            checked = run(checker, str(source), str(circuit))
            assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
        # Finite headroom and worker shares shrink caps; max/absent limits still reduce.
        for name, maximum, share in (
                ("budget", str(1024**3), None),
                ("unlimited", "max", None),
                ("absent", None, None),
                ("share", "max", 256 * 1024**2)):
            (scope / "memory.max").unlink(missing_ok=True)
            if maximum is not None:
                (scope / "memory.max").write_text(maximum + "\n")
            # No usage reading is needed without a finite limit.
            (scope / "memory.current").unlink(missing_ok=True)
            if maximum not in {None, "max"}:
                (scope / "memory.current").write_text("0\n")
            phases = root / f"scope-{name}"
            phases.mkdir()
            answer = run(fixture, "--scope-reductions", str(scope),
                         *([str(share)] if share else []),
                         env=dict(os.environ, ACACIA_PHASE_RECORDS=str(phases)))
            assert answer.returncode == 0, answer
            rows = [json.loads(line) for path in phases.glob("*.jsonl")
                    for line in path.read_text().splitlines()]
            attempts = [row for row in rows if row.get("event") == "parity_reduction_attempt"]
            assert any(row["outcome"] == "accepted" for row in attempts), rows
            children = [row for row in attempts if row["child_pid"] > 0]
            assert children, rows
            cap = share or 512 * 1024**2
            assert all(0 < row["memory_bytes"] <= cap for row in children), children
            if name in {"budget", "share"}:
                assert all(row["memory_bytes"] < cap for row in children), children
            assert any(row["outcome"] == "skipped" and row["cause"] == "rss_share"
                       and row["child_pid"] == -1 for row in attempts), rows
    print("Parity reduction grammar, unchanged decision stages, and independent controllers passed")


if __name__ == "__main__":
    main()
