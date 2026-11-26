#!/usr/bin/env python3
"""Check encoder grammar, all synthesis paths, and the unchanged default bytes."""

from itertools import product
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from obligation_test_support import records


def run(binary, *arguments, **kwargs):
    return subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=15,
                          **kwargs)


def synthesize(binary, controlled, arm, command):
    with tempfile.TemporaryDirectory() as temporary:
        phases = Path(temporary)
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases))
        if arm == "both:obligation:spot" and controlled:
            # -s adds backward fallbacks. Hold their verdicts so this tests obligation bytes.
            reader, writer = os.pipe()
            try:
                with open(os.devnull) as ready:
                    env.update(ACACIA_TEST_SYNTHESIS_VERDICT_FD=str(reader),
                               ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD=str(ready.fileno()))
                    result = run(controlled, *command, env=env,
                                 pass_fds=(reader, ready.fileno()))
            finally:
                os.close(reader)
                os.close(writer)
        else:
            result = run(binary, *command, env=env)
        assert result.returncode == 0, (arm, result)
        winner, = [row for row in records(phases) if row.get("event") == "parent_winner"]
        backend = arm.split(":")[1 if arm.startswith("both:") else 2]
        backends = {backend, "backward"} if arm == "both:obligation:spot" and not controlled else {backend}
        assert winner["requested_backend"] in backends, winner
        if arm == "both:obligation:spot" and controlled:
            assert winner["effective_backend"] == "obligation", winner
        return tuple(winner.get(key) for key in ("requested_backend", "effective_backend", "route"))


def rejected_calls(binary, direct, mode):
    yield run(binary, "--aiger-mode", mode)
    for entry in ("legacy", "obligation", "parity", "parity-dpa"):
        for operation in ("decision", "synthesis"):
            yield run(direct, entry, mode, operation)


def main():
    binary, checker, direct, controlled = sys.argv[1:]
    if not controlled:
        print("SKIP obligation-route byte comparison: synthesis verdict hooks unavailable")
    help_text = run(binary, "--help").stdout
    assert "--aiger-mode MODE" in help_text
    assert "a canonical subset of Spot's syntax: no empty components" in help_text
    methods = ("ite", "isop", "both", "optim")
    modifiers = ("", "+ud", "+dc", "+dc+ud", "+ud+dc")
    splits = ("", "+sub0", "+sub1", "+sub2")
    modes = [a + b + c for a, b, c in product(methods, modifiers, splits)]
    modes += ["both+sub2+ud+dc", "isop+dc+dc+sub0+sub1", "ite,isop+dc",
              "both+dc,optim", "isop,optim+sub1+dc,ite", "isop+ud+ud+sub0+sub1"]
    for mode in modes:
        result = run(binary, "--aiger-mode", mode, "-f", "true", "-i", "", "-o", "",
                     "-r", "small")
        assert result.returncode == 0, (mode, result)
        encoded = run(direct, "spot", mode, "synthesis")
        assert encoded.returncode == 0 and encoded.stdout.startswith("aag "), (mode, encoded)
    # Empty components are intentionally refused, even where Spot ignores trailing ones.
    for mode in ("", "ISOP", "unknown", "both+dual", "isop+sub3", "ite+sub",
                 "isop+sub00", "both+", "+dc", "ite++dc", "ite,", ",ite", "ite,,isop",
                 "ite+", "ite+,isop", "both +dc", "ite+dc,invalid"):
        for result in rejected_calls(binary, direct, mode):
            assert result.returncode == 3 and "invalid --aiger-mode" in result.stderr, result
            assert not result.stdout, result
    missing = run(binary, "--aiger-mode")
    assert missing.returncode == 3 and "requires an argument" in missing.stderr, missing

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        source, circuit = directory / "spec.tlsf", directory / "controller.aag"
        source.write_text('''
INFO { TITLE: "encoding" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { a; b; c; d; unused_input; } OUTPUTS { grant; unused_output; }
       GUARANTEE { G (grant <-> ((a && b) || (!a && c) || (a && d))); } }
''')
        for arm, options in (("real:small:backward", []),
                             ("real:small:backward", ["--spot-fast", "off"]),
                             ("both:obligation:spot", []), ("both:parity:spot", [])):
            command = ["--arms", arm, *options, "-T", str(source), "-s", str(circuit),
                       "--aiger-abc", "off"]
            route = synthesize(binary, controlled, arm, command)
            pinned = arm != "both:obligation:spot" or bool(controlled)
            original = circuit.read_bytes()
            encodings = set()
            for mode in ("isop", "ite", "both", "both+ud+sub0", "isop+ud+sub1",
                         "both+sub2+ud", "optim", "ite,isop+ud", "isop+dc", "ite+dc",
                         "ite+dc+ud", "both+dc", "both+dc+ud"):
                actual = synthesize(binary, controlled, arm, [*command, "--aiger-mode", mode])
                if pinned:
                    assert actual == route, (arm, mode, route, actual)
                if mode == "isop" and pinned:
                    assert circuit.read_bytes() == original, arm
                encodings.add(circuit.read_bytes())
                checked = run(checker, str(source), str(circuit))
                assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            assert len(encodings) > 1, (arm, "mode did not affect encoding")
    print("AIGER grammar, Spot acceptance, direct rejections, synthesis routes and default bytes passed")


if __name__ == "__main__":
    main()
