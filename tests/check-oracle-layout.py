"""Generated CLI parity for the sparse oracle representation switch."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = str(Path(sys.argv[1]).resolve())
root = Path(sys.argv[2]).resolve()
env = dict(os.environ)
for key in ("ACACIA_OUTER_DEADLINE_MONOTONIC", "ACACIA_PHASE_RECORDS",
            "ACACIA_SPOT_CAPTURE_DIR", "ACACIA_DIAG"):
    env.pop(key, None)


def run(args, extra_env=None):
    result = subprocess.run([binary, *args], env=env | (extra_env or {}),
                            capture_output=True, timeout=15)
    return result.returncode, result.stdout, result.stderr


assert b"--oracle-layout" in run(["--help"])[1]
invalid = run(["--oracle-layout", "unknown", "-f", "true"])
assert invalid[0] != 0 and b"scan or grouped" in invalid[2]
with tempfile.TemporaryDirectory(prefix="oracle-layout-", dir=root) as scratch:
    scratch = Path(scratch)
    for index, formula in enumerate(("G(req -> F(grant))", "G(req <-> X(grant))",
                                     "GF(req) & GF(grant)", "G(!req) & F(req)",
                                     "G(grant)", "true", "false")):
        source = scratch / f"generated-{index}.ltl"
        source.write_text(formula)
        for arm in ("real:small:spot-guarded-sparse", "unreal:formula:spot-guarded-sparse"):
            args = ["-F", str(source), "-i", "req", "-o", "grant", "--arms", arm,
                    "--weakening", "off", "--equivariance", "off", "--spot-fast", "off",
                    "-M", "2", "-K", "2"]
            baseline = run(args)
            assert baseline == run([*args, "--oracle-layout", "scan"])
            assert baseline == run([*args, "--oracle-layout", "grouped"])
    for semantics in ("Mealy", "Moore"):
        source = scratch / f"generated-{semantics}.tlsf"
        source.write_text(f'''INFO {{ TITLE: "generated" DESCRIPTION: "oracle parity"
 SEMANTICS: {semantics} TARGET: {semantics} }}
MAIN {{ INPUTS {{ req; }} OUTPUTS {{ grant; }}
 GUARANTEES {{ G(req <-> grant); }} }}
''')
        args = ["--tlsf", str(source), "--arms", "real:small:spot-guarded-sparse",
                "--spot-fast", "off", "--weakening", "off", "--equivariance", "off",
                "-M", "2", "-K", "2"]
        baseline = run(args)
        assert baseline == run([*args, "--oracle-layout", "scan"])
        assert baseline == run([*args, "--oracle-layout", "grouped"])
    # Observation stays opt-in, and both independent oracle instances record
    # the chosen layout. No solver-known deadline is supplied.
    args = ["-f", "G(req <-> X(grant))", "-i", "req", "-o", "grant",
            "--arms", "real:small:spot-guarded-sparse", "--spot-fast", "off",
            "--weakening", "off", "--equivariance", "off", "-M", "2", "-K", "2"]
    baseline = run(args)
    for layout in (None, "scan", "grouped"):
        captures = scratch / (layout or "default")
        captures.mkdir()
        selected = args if layout is None else [*args, "--oracle-layout", layout]
        plain = run(selected)
        observed = run(selected, {"ACACIA_SPOT_CAPTURE_DIR": str(captures)})
        assert baseline == plain == observed
        rows = [json.loads(p.read_text()) for p in captures.glob("*.json")]
        assert rows
        expected = layout or "grouped"
        assert all(row["search_oracle_layout"] == row["verify_oracle_layout"] == expected
                   for row in rows)
    output = scratch / "controller.aag"
    args = ["-f", "G(grant)", "-i", "req", "-o", "grant",
            "--arms", "real:small:spot-guarded-sparse", "--equivariance", "off",
            "-s", str(output)]
    scan = run([*args, "--oracle-layout", "scan"])
    assert scan[0] == 0 and output.is_file()
    first = output.read_bytes()
    for selection in ([], ["--oracle-layout", "grouped"]):
        assert scan == run([*args, *selection])
        assert output.read_bytes() == first
print("oracle-layout: generated LTL/TLSF decision, observation and synthesis parity passed")
