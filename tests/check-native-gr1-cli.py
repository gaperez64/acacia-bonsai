#!/usr/bin/env python3
"""Exercise exact native arm polarity, parsing, deadlines, and renaming."""

from __future__ import annotations

from importlib.util import module_from_spec, spec_from_file_location
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]

spec = spec_from_file_location("obfuscate_tlsf", ROOT / "benchmarking" /
                               "gr1-par2-20260923" / "obfuscate-tlsf.py")
assert spec and spec.loader
obfuscator = module_from_spec(spec)
spec.loader.exec_module(obfuscator)

REAL = '''INFO { TITLE: "real" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G o; } }
'''
UNREAL = '''INFO { TITLE: "unreal" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G i; } }
'''
ADVERSARIAL = '''INFO { TITLE: "crossed names" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { controllable_o0; } OUTPUTS { uncontrollable_i0; }
       GUARANTEES { G uncontrollable_i0; } }
'''


def run(binary: Path, args: list[str], deadline: float | None = None,
        corrupt_proof: bool = False) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if deadline is not None:
        env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(deadline)
    else:
        env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
    if corrupt_proof:
        env["ACACIA_NATIVE_TEST_CORRUPT_PROOF"] = "1"
    else:
        env.pop("ACACIA_NATIVE_TEST_CORRUPT_PROOF", None)
    process = subprocess.Popen([str(binary), *args], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, env=env,
                               start_new_session=True)
    stdout, stderr = process.communicate(timeout=10)
    # The parent must terminate and reap every child on a winner or deadline.
    try:
        os.killpg(process.pid, 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError(f"process group {process.pid} survived: {args}")
    return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)


def expect(binary: Path, args: list[str], code: int, text: str,
           deadline: float | None = None, corrupt_proof: bool = False) -> None:
    result = run(binary, args, deadline, corrupt_proof)
    if result.returncode != code or text not in result.stdout + result.stderr:
        raise AssertionError((args, code, text, result))


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    build_dir = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(dir=build_dir) as location:
        directory = Path(location)
        real = directory / "real.tlsf"
        unreal = directory / "unreal.tlsf"
        malformed = directory / "malformed.tlsf"
        nul_source = directory / "embedded_nul.tlsf"
        adversarial = directory / "crossed_names.tlsf"
        real.write_text(REAL)
        unreal.write_text(UNREAL)
        malformed.write_text("not TLSF\n")
        nul_source.write_bytes(REAL.encode() + b"\x00")
        adversarial.write_text(ADVERSARIAL)
        for source, winner, loser, verdict, code in (
            (real, "real", "unreal", "REALIZABLE", 0),
            (unreal, "unreal", "real", "UNREALIZABLE", 1),
        ):
            expect(binary, ["-T", str(source), "--arms", f"{winner}:gr1:oxidd"],
                   code, verdict)
            expect(binary, ["--arms", f"{winner}:gr1:oxidd", "-T", str(source)],
                   code, verdict)
            expect(binary, ["-T", str(source), "--arms", f"{loser}:gr1:oxidd"],
                   2, "opposite side solved")
            renamed, _ = obfuscator.write_obfuscated(source, directory, code + 11)
            expect(binary, ["-T", str(renamed), "--arms", f"{winner}:gr1:oxidd"],
                   code, verdict)
        expect(binary, ["-T", str(adversarial), "--arms", "real:gr1:oxidd"],
               0, "REALIZABLE")
        crossed_renamed, _ = obfuscator.write_obfuscated(adversarial, directory, 37)
        expect(binary, ["-T", str(crossed_renamed), "--arms", "real:gr1:oxidd"],
               0, "REALIZABLE")
        expect(binary, ["-T", str(real), "--arms",
                        "real:gr1:oxidd,unreal:formula:backward"], 0, "REALIZABLE",
               time.monotonic() + 3)
        expect(binary, ["-T", str(malformed), "--arms", "real:gr1:oxidd"],
               2, "UNKNOWN")
        for _ in range(4):
            concurrent = run(binary, ["-T", str(malformed), "--arms",
                                      "real:gr1:oxidd,unreal:gr1:oxidd"])
            lines = concurrent.stderr.splitlines()
            assert concurrent.returncode == 2 and lines[-1] == "UNKNOWN"
            diagnostics = [json.loads(line) for line in lines[:-1]]
            assert {item["arm"] for item in diagnostics} == {
                "real:gr1:oxidd", "unreal:gr1:oxidd"}
        expect(binary, ["-T", str(nul_source), "--arms", "real:gr1:oxidd"],
               2, "invalid source bytes or embedded NUL")
        expect(binary, ["-T", str(malformed), "--arms",
                        "real:small:backward,real:gr1:oxidd"], 2, "UNKNOWN")
        expect(binary, ["-T", str(real), "--arms", "real:gr1:oxidd"],
               2, "deadline", time.monotonic() - 1)
        if "--hook-binary" in sys.argv[3:]:
            hook_binary = Path(sys.argv[sys.argv.index("--hook-binary") + 1])
            expect(hook_binary, ["-T", str(real), "--arms", "real:gr1:oxidd"],
                   2, "proof not verified", corrupt_proof=True)
        # A previously intermittent OxiDD checker crash appeared only after
        # the solver had used the same thread on this larger certificate.
        round_robin = (ROOT / "tests/syntcomp-benchmarks/tlsf/round_robin_arbiter" /
                       "parametric/round_robin_arbiter.tlsf")
        for _ in range(12):
            expect(binary, ["-T", str(round_robin), "--arms",
                            "real:gr1:oxidd,unreal:gr1:oxidd"], 0, "REALIZABLE",
                   time.monotonic() + 5)
        for arms, diagnostic in (
            ("real:gr1:oxidd,real:gr1:oxidd", "duplicate arm"),
            ("real:gr1:oxidd:frozen-graph", "does not accept a provider"),
            ("unreal:param-lift:oxidd", "realizability only"),
            ("real:gr1:backward", "requires the oxidd backend"),
        ):
            expect(binary, ["-T", str(real), "--arms", arms], 3, diagnostic)
        expect(binary, ["--arms", "real:gr1:oxidd", "-f", "G o", "-i", "i",
                        "-o", "o"], 3, "require -T")
        expect(binary, ["--arms", "real:gr1:oxidd", "-F", str(real), "-i", "i",
                        "-o", "o"], 3, "require -T")
        expect(binary, ["-T", str(real), "-f", "G o", "--arms",
                        "real:gr1:oxidd"], 3, "mutually exclusive")
        expect(binary, ["-F", str(real), "-T", str(real), "--arms",
                        "real:gr1:oxidd"], 3, "cannot be combined")
        expect(binary, ["-T", str(real), "--arms", "real:gr1:oxidd", "-s",
                        str(directory / "controller")], 3, "do not support -s")
    print("native GR(1) CLI checks passed")


if __name__ == "__main__":
    main()
