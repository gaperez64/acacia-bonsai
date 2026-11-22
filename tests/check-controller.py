#!/usr/bin/env python3
"""Check emitted circuits and negative controls, including liveness and timing."""

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarking"))
from benchlib import load_tool  # noqa: E402

cases = load_tool("tests/check-moore-synthesis.py").cases


def main():
    binary, checker = sys.argv[1:]
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        spec, circuit = directory / "spec.tlsf", directory / "controller.aag"
        # Use the existing generated timing fixtures across all semantics/target pairs.
        fixtures = {}
        for description, text, _ in cases(False):
            key = (*description[:2], description[2] == "no-input",
                   "strict-assumptions" in description)
            fixtures.setdefault(key, text)
        fixtures["liveness"] = '''
INFO { TITLE: "recurrence" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { request; } OUTPUTS { grant; }
       GUARANTEE { G F grant; } }
'''
        for text in fixtures.values():
            spec.write_text(text)
            result = subprocess.run([binary, "-T", str(spec), "-s", str(circuit)],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, result
            checked = subprocess.run([checker, str(spec), str(circuit)],
                                     capture_output=True, text=True, timeout=20)
            assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked

        # A safety-only check would incorrectly accept this always-false circuit.
        circuit.write_text("aag 1 1 0 1 0\n2\n0\ni0 request\no0 grant\nc\n")
        checked = subprocess.run([checker, str(spec), str(circuit)], capture_output=True,
                                 text=True, timeout=20)
        assert checked.returncode == 1 and checked.stdout.strip() == "failed_verification", checked
        circuit.write_text("aag 1 1 0 1 0\n2\n1\ni0 wrong\no0 grant\nc\n")
        assert subprocess.run([checker, str(spec), str(circuit)], timeout=20).returncode != 0

        # The formula permits equality; the Moore machine model forbids feedthrough.
        spec.write_text('''
INFO { TITLE: "timing" SEMANTICS: Moore TARGET: Moore }
MAIN { INPUTS { request; } OUTPUTS { grant; }
       GUARANTEE { G (grant <-> request); } }
''')
        circuit.write_text("aag 1 1 0 1 0\n2\n2\ni0 request\no0 grant\nc\n")
        assert subprocess.run([checker, str(spec), str(circuit)], timeout=20).returncode != 0
    print("Controller checks and negative controls passed")


if __name__ == "__main__":
    main()
