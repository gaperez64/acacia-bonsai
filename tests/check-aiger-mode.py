#!/usr/bin/env python3
"""Check encoder grammar, all synthesis paths, and the unchanged default bytes."""

from itertools import product
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary, *arguments):
    return subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=15)


def main():
    binary, checker = sys.argv[1:]
    assert "--aiger-mode MODE" in run(binary, "--help").stdout
    methods = ("ite", "isop", "both")
    modifiers = ("", "+dc", "+ud", "+dc+ud")
    splits = ("", "+sub0", "+sub1", "+sub2")
    modes = [a + b + c for a, b, c in product(methods, modifiers, splits)]
    modes += ["both+sub2+ud+dc", "isop+dc+dc+sub0+sub1", "optim", "ite,isop+dc"]
    for mode in modes:
        result = run(binary, "--aiger-mode", mode, "-f", "true", "-i", "", "-o", "",
                     "-r", "small")
        assert result.returncode == 0, (mode, result)
    for mode in ("", "ISOP", "unknown", "both+dual", "isop+sub3", "ite+sub",
                 "isop+sub00", "both+", "+dc", "ite++dc", "ite,", ",ite", "ite,,isop",
                 "both +dc", "ite+dc,invalid"):
        result = run(binary, "--aiger-mode", mode)
        assert result.returncode == 3 and "invalid --aiger-mode" in result.stderr, result
        assert not result.stdout, result
    missing = run(binary, "--aiger-mode")
    assert missing.returncode == 3 and "requires an argument" in missing.stderr, missing

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        source, circuit = directory / "spec.tlsf", directory / "controller.aag"
        source.write_text('''
INFO { TITLE: "encoding" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { a; b; c; d; } OUTPUTS { grant; }
       GUARANTEE { G (grant <-> ((a && b) || (!a && c) || (a && d))); } }
''')
        for arm in ("real:small:backward", "both:obligation:spot", "both:parity:spot"):
            command = ["--arms", arm, "-T", str(source), "-s", str(circuit)]
            default = run(binary, *command)
            assert default.returncode == 0, (arm, default)
            original = circuit.read_bytes()
            encodings = set()
            for mode in ("isop", "ite", "both", "both+dc+ud+sub0", "isop+dc+ud+sub1",
                         "both+sub2+ud+dc", "optim", "ite,isop+dc"):
                result = run(binary, *command, "--aiger-mode", mode)
                assert result.returncode == 0, (arm, mode, result)
                if mode == "isop":
                    assert circuit.read_bytes() == original, arm
                encodings.add(circuit.read_bytes())
                checked = run(checker, str(source), str(circuit))
                assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            assert len(encodings) > 1, (arm, "mode did not affect encoding")
    print("AIGER grammar, three synthesis routes, verification and default bytes passed")


if __name__ == "__main__":
    main()
