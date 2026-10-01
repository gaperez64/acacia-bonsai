#!/usr/bin/env python3
"""The shipped portfolio adapts to input type; explicit arms stay strict."""

from pathlib import Path
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1])
build = Path(sys.argv[2])
arms = sys.argv[3].split(",")
expected_ltl_children = int(sys.argv[4])
legacy = [a for a in arms if ":gr1:oxidd" not in a and
          ":gr1-lift:oxidd" not in a and
          ":param-lift:oxidd" not in a]
native = [a for a in arms if a not in legacy]
assert native


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(binary), *args], capture_output=True,
                          text=True, timeout=30)


ltl = ("-f", "G(i -> o)", "-i", "i", "-o", "o")
plain = run(*ltl, "-v")
assert plain.returncode == 0, plain
assert f"Starting {expected_ltl_children} solver children" in plain.stdout, plain
assert plain.stderr.count("Skipping default native arm") == len(native), plain

synth = build / "default-native-synthesis.aag"
synth.unlink(missing_ok=True)
made = run(*ltl, "-v", "-s", str(synth))
assert made.returncode == 0 and synth.is_file() and synth.stat().st_size, made
assert made.stderr.count("Skipping default native arm") == len(native), made
synth.unlink()

explicit = run(*ltl, "--arms", native[0])
assert (explicit.returncode == 3 and
        "native arms require -T FILE" in explicit.stderr), explicit

source_text = ('INFO { TITLE: "real" SEMANTICS: Mealy TARGET: Mealy }\n'
               'MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G o; } }\n')
source = build / "default-native-input.tlsf"
source.write_text(source_text)
try:
    tlsf = run("-T", str(source), "-v")
    assert tlsf.returncode == 0, tlsf
    assert f"Starting {len(arms)} solver children" in tlsf.stdout, tlsf
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

    wrapped_tlsf = subprocess.run([str(wrapper), "--tlsf", "-v"],
                                  input=source_text, capture_output=True,
                                  text=True, timeout=30)
    assert wrapped_tlsf.returncode == 0, wrapped_tlsf
    assert f"Starting {len(arms)} solver children" in wrapped_tlsf.stdout, wrapped_tlsf
    assert "Skipping default native arm" not in wrapped_tlsf.stderr, wrapped_tlsf

    wrapped_ltl = subprocess.run([str(wrapper), *ltl, "-v"],
                                 capture_output=True, text=True, timeout=30)
    assert wrapped_ltl.returncode == 0, wrapped_ltl
    assert (f"Starting {expected_ltl_children} solver children"
            in wrapped_ltl.stdout), wrapped_ltl
    assert wrapped_ltl.stderr.count("Skipping default native arm") == len(native), wrapped_ltl
