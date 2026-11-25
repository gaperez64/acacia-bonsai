#!/usr/bin/env python3
"""Check encoder grammar, all synthesis paths, and the unchanged default bytes."""

from itertools import product
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary, *arguments):
    return subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=15)


def rejected_calls(binary, direct, mode):
    yield run(binary, "--aiger-mode", mode)
    for entry in ("legacy", "obligation", "parity", "parity-dpa"):
        for operation in ("decision", "synthesis"):
            yield run(direct, entry, mode, operation)


def main():
    binary, checker, direct = sys.argv[1:]
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
            default = run(binary, *command)
            assert default.returncode == 0, (arm, default)
            original = circuit.read_bytes()
            encodings = set()
            for mode in ("isop", "ite", "both", "both+ud+sub0", "isop+ud+sub1",
                         "both+sub2+ud", "optim", "ite,isop+ud", "isop+dc", "ite+dc",
                         "ite+dc+ud", "both+dc", "both+dc+ud"):
                result = run(binary, *command, "--aiger-mode", mode)
                assert result.returncode == 0, (arm, mode, result)
                if mode == "isop":
                    assert circuit.read_bytes() == original, arm
                encodings.add(circuit.read_bytes())
                checked = run(checker, str(source), str(circuit))
                assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            assert len(encodings) > 1, (arm, "mode did not affect encoding")
    print("AIGER grammar, Spot acceptance, direct rejections, synthesis routes and default bytes passed")


if __name__ == "__main__":
    main()
