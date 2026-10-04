#!/usr/bin/env python3
"""The shipped portfolio adapts to input type; explicit arms stay strict."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1])
build = Path(sys.argv[2])
arms = sys.argv[3].split(",")
expected_ltl_children = int(sys.argv[4])
verbose_enabled = sys.argv[5] == "true"
legacy = [a for a in arms if ":gr1:oxidd" not in a and
          ":gr1-lift:oxidd" not in a and
          ":gr1-real-lift:oxidd" not in a and
          ":param-lift:oxidd" not in a]
native = [a for a in arms if a not in legacy]
assert native


def run_recorded(command: list[str], *, input: str | None = None
                 ) -> tuple[subprocess.CompletedProcess[str], list[dict]]:
    # Worker startup records remain available when NO_VERBOSE hides child counts.
    with tempfile.TemporaryDirectory(prefix="default-native-records-", dir=build) as tmp:
        env = os.environ.copy()
        env["ACACIA_PHASE_RECORDS"] = tmp
        result = subprocess.run(command, input=input, env=env, capture_output=True,
                                text=True, timeout=30)
        records = [json.loads(line) for path in Path(tmp).glob("*.jsonl")
                   for line in path.read_text().splitlines()]
    assert any(record.get("arm") == "legacy_parent" and
               record.get("phase") == "record_summary" for record in records), (result, records)
    return result, records


def check_children(result: subprocess.CompletedProcess[str], records: list[dict],
                   expected: int, expected_native: bool) -> None:
    startups = [record["arm"] for record in records
                if record.get("phase") == "child_startup"]
    assert len(startups) == expected, (result, startups)
    for arm in native:
        assert (arm in startups) == expected_native, (result, arm, startups)
    if verbose_enabled:
        assert f"Starting {expected} solver children" in result.stdout, result


ltl = ("-f", "G(i -> o)", "-i", "i", "-o", "o")
plain, plain_records = run_recorded([str(binary), *ltl, "-v"])
assert plain.returncode == 0, plain
check_children(plain, plain_records, expected_ltl_children, False)
assert plain.stderr.count("Skipping default native arm") == len(native), plain

synth = build / "default-native-synthesis.aag"
synth.unlink(missing_ok=True)
made, made_records = run_recorded([str(binary), *ltl, "-v", "-s", str(synth)])
assert made.returncode == 0 and synth.is_file() and synth.stat().st_size, made
assert not any(record.get("phase") == "child_startup" and
               record.get("arm") in native for record in made_records), (made, made_records)
assert made.stderr.count("Skipping default native arm") == len(native), made
synth.unlink()

explicit = subprocess.run([str(binary), *ltl, "--arms", native[0]],
                          capture_output=True, text=True, timeout=30)
assert (explicit.returncode == 3 and
        "native arms require -T FILE" in explicit.stderr), explicit

source_text = ('INFO { TITLE: "real" SEMANTICS: Mealy TARGET: Mealy }\n'
               'MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G o; } }\n')
source = build / "default-native-input.tlsf"
source.write_text(source_text)
try:
    tlsf, tlsf_records = run_recorded([str(binary), "-T", str(source), "-v"])
    assert tlsf.returncode == 0, tlsf
    check_children(tlsf, tlsf_records, len(arms), True)
    assert "Skipping default native arm" not in tlsf.stderr, tlsf
finally:
    source.unlink(missing_ok=True)

# Run the checked binary through the real wrapper in an isolated layout. Its
# configuration list normally points at shipped builds, while this test uses
# the six-arm test build passed by Meson.
with tempfile.TemporaryDirectory(prefix="default-native-wrapper-", dir=build) as tmp:
    root = Path(tmp)
    (root / "scripts").mkdir()
    (root / "config").mkdir()
    (root / "build_test_native" / "src").mkdir(parents=True)
    wrapper = root / "scripts" / "acacia-bonsai.sh"
    wrapper.symlink_to(Path(__file__).resolve().parents[1] /
                       "scripts" / "acacia-bonsai.sh")
    (root / "config" / "docker-default.list").write_text("test_native\n")
    (root / "build_test_native" / "src" / "acacia-bonsai").symlink_to(
        binary.resolve())

    wrapped_tlsf, wrapped_tlsf_records = run_recorded(
        [str(wrapper), "--tlsf", "-v"], input=source_text)
    assert wrapped_tlsf.returncode == 0, wrapped_tlsf
    check_children(wrapped_tlsf, wrapped_tlsf_records, len(arms), True)
    assert "Skipping default native arm" not in wrapped_tlsf.stderr, wrapped_tlsf

    wrapped_ltl, wrapped_ltl_records = run_recorded([str(wrapper), *ltl, "-v"])
    assert wrapped_ltl.returncode == 0, wrapped_ltl
    check_children(wrapped_ltl, wrapped_ltl_records, expected_ltl_children, False)
    assert wrapped_ltl.stderr.count("Skipping default native arm") == len(native), wrapped_ltl
