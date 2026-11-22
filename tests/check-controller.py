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

        def check(expected, max_states=None):
            command = [checker, str(spec), str(circuit)]
            if max_states is not None:
                command.extend(["--max-states", str(max_states)])
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            code = {"verified": 0, "failed_verification": 1, "verify_unsupported": 3}[expected]
            assert result.returncode == code and result.stdout.strip() == expected, result
            return result

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
        check("failed_verification")

        # Dependence in unreachable states is compatible with a Moore controller.
        spec.write_text('''
INFO { TITLE: "reachable timing" SEMANTICS: Moore TARGET: Moore }
MAIN { INPUTS { request; } OUTPUTS { grant; } GUARANTEE { G !grant; } }
''')
        circuit.write_text("aag 3 1 1 1 1\n2\n4 0\n6\n6 4 2\n"
                           "i0 request\no0 grant\nc\n")
        check("verified", 1)
        # Now q becomes one: the same dependence is reachable after the first step.
        spec.write_text(spec.read_text().replace("G !grant", "G (grant -> request)"))
        circuit.write_text(circuit.read_text().replace("4 0\n", "4 1\n"))
        check("failed_verification")

        # The highest latch changes after step zero, including beyond native word widths.
        for width in (31, 32, 33, 65):
            circuit.write_text(
                f"aag {width + 1} 1 {width} 1 0\n2\n"
                + "".join(f"{2 * (n + 2)} {int(n == width - 1)}\n" for n in range(width))
                + f"{2 * (width + 1)}\ni0 request\no0 grant\nc\n")
            spec.write_text('''
INFO { TITLE: "wide state" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { request; } OUTPUTS { grant; } GUARANTEE { G !grant; } }
''')
            check("failed_verification")
            spec.write_text(spec.read_text().replace("G !grant", "!grant && X G grant"))
            check("verified")
            check("verified", 2)
            assert "budget exceeded" in check("verify_unsupported", 1).stderr

        # Keep Spot's restricted parser: never reinterpret explicit reset fields as zero.
        for reset in (0, 1, 4):
            circuit.write_text(f"aag 2 1 1 1 0\n2\n4 0 {reset}\n4\n"
                               "i0 request\no0 grant\nc\n")
            assert "reset" in check("verify_unsupported").stderr
    print("Controller checks and negative controls passed")


if __name__ == "__main__":
    main()
