#!/usr/bin/env python3
"""Check ABC grammar, circuit reduction, independent verification and fallback."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary, *args, **kwargs):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=15, **kwargs)


def size(path):
    header = path.read_text().splitlines()[0].split()
    assert header[0] == "aag", header
    return int(header[3]) + int(header[5])


def symbols(text):
    return [line for line in text.splitlines() if line.startswith((b"i", b"o"))]


def main():
    binary, available, *synthesis = sys.argv[1:]
    assert "--aiger-abc on|off" in run(binary, "--help").stdout
    for value in ("", "ON", "yes", "1", "auto"):
        answer = run(binary, "--aiger-abc", value)
        assert answer.returncode == 3 and "invalid --aiger-abc" in answer.stderr, answer
    answer = run(binary, "--aiger-abc")
    assert answer.returncode == 3 and "requires an argument" in answer.stderr, answer
    command = ["-f", "true", "-i", "", "-o", "", "-r", "small"]
    default = run(binary, *command)
    off = run(binary, *command, "--aiger-abc", "off")
    assert default.returncode == off.returncode == 0 and default.stdout == off.stdout
    on = run(binary, *command, "--aiger-abc", "on")
    if available == "false":
        assert on.returncode == 3 and "requires a build with ABC" in on.stderr, on
        assert not on.stdout, on
        return
    assert on.returncode == 0 and on.stdout == off.stdout, on
    if not synthesis:
        return
    checker, spec, *fault_binary = synthesis
    with tempfile.TemporaryDirectory() as temporary:
        circuit = Path(temporary) / "controller.aag"
        for arm in ("real:small:backward", "both:obligation:spot", "both:parity:spot"):
            command = ["-T", spec, "--arms", arm, "-s", str(circuit)]
            off = run(binary, *command, "--aiger-abc", "off")
            assert off.returncode == 0, off
            original, original_size = circuit.read_bytes(), size(circuit)
            checked = run(checker, spec, str(circuit))
            assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            on = run(binary, *command, "--aiger-abc", "on")
            assert on.returncode == 0 and size(circuit) < original_size, (arm, on, circuit.read_text())
            assert symbols(circuit.read_bytes()) == symbols(original), arm
            checked = run(checker, spec, str(circuit))
            assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked
            default = run(binary, *command)
            assert default.returncode == 0 and size(circuit) < original_size, default
            if fault_binary:
                failed = run(fault_binary[0], *command, "--aiger-abc", "on",
                             env=dict(os.environ, ACACIA_TEST_AIGER_ABC_FAILURE="1"))
                assert failed.returncode == 0 and "forced ABC failure" in failed.stderr, failed
                assert circuit.read_bytes() == original, (arm, failed)
                checked = run(checker, spec, str(circuit))
                assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked


if __name__ == "__main__":
    main()
