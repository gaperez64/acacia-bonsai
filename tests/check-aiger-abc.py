#!/usr/bin/env python3
"""Check ABC grammar, circuit reduction, independent verification and fallback."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from obligation_test_support import LegacyGate, records


RSS_SAMPLING_AVAILABLE = sys.platform.startswith("linux")
REQUIRE_REWRITE = RSS_SAMPLING_AVAILABLE and os.environ.get("ACACIA_TEST_REQUIRE_ABC") == "1"


def run(binary, *args, **kwargs):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=15, **kwargs)


def size(path):
    header = path.read_text().splitlines()[0].split()
    assert header[0] == "aag", header
    return int(header[3]) + int(header[5])


def symbols(text):
    return [line for line in text.splitlines() if line.startswith((b"i", b"o"))]


def synthesize(binary, command, root, backend, library, **overrides):
    with tempfile.TemporaryDirectory(dir=root) as directory:
        phases = Path(directory)
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases), **overrides)
        if library:
            env["LD_PRELOAD"] = library
        with LegacyGate(env, backend == "obligation" and bool(library)) as gate:
            with subprocess.Popen([binary, *command], cwd=root, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                  pass_fds=gate.descriptors) as process:
                try:
                    stdout, stderr = gate.communicate(process, phases, timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
                    raise
        answer = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        assert answer.returncode == 0, answer
        winner, = [row for row in records(phases) if row.get("event") == "parent_winner"]
        assert winner["requested_backend"] == backend, (backend, winner, answer)
        # This safety fixture takes the parity worker's deterministic Buchi route.
        expected = "spot-fast" if backend == "parity" else backend
        assert winner["effective_backend"] == expected, (backend, winner, answer)
        if backend == "parity":
            assert winner["route"] == "deterministic_buchi", winner
        return answer


def main():
    parser = argparse.ArgumentParser()
    for name in ("binary", "available"):
        parser.add_argument(name)
    for name in ("checker", "spec", "budget", "fault_binary"):
        parser.add_argument(name, nargs="?")
    parser.add_argument("--fault-library")
    options = parser.parse_args()
    for name in ("binary", "checker", "spec", "budget", "fault_binary", "fault_library"):
        if value := getattr(options, name):
            setattr(options, name, str(Path(value).resolve()))
    binary, available = options.binary, options.available
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
    # ABC's pre-main constructors belong exclusively to the optional helper.
    if nm := shutil.which("nm"):
        linked = run(nm, binary)
        assert linked.returncode == 0, linked
        assert "Abc_Start" not in linked.stdout and "Ufar_StopIfRequested" not in linked.stdout
    if not options.checker:
        return
    if options.fault_binary:
        decision = run(options.fault_binary, *command, "--aiger-abc", "on",
                       env=dict(os.environ, ACACIA_TEST_AIGER_ABC_FAILURE="1"))
        assert decision.returncode == 0 and decision.stdout == off.stdout, decision
        assert "forced ABC failure" not in decision.stderr, decision
    checker, spec = options.checker, options.spec
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        circuit = root / "controller.aag"

        def verify():
            checked = run(checker, spec, str(circuit))
            assert checked.returncode == 0 and checked.stdout.strip() == "verified", checked

        sentinel = root / "sentinel"
        sentinel.write_text("keep this file unchanged\n")
        # First check an absent history, then a symlink to an unrelated file.
        for backend, arm in (("backward", "real:small:backward"),
                             ("obligation", "both:obligation:spot"),
                             ("parity", "both:parity:spot")):
            if backend == "obligation" and not options.fault_library:
                print("Skipping obligation route: worker gate unavailable on this platform")
                continue
            command = ["-T", spec, "--arms", arm, "-s", str(circuit)]
            if backend == "backward":
                command += ["--spot-fast", "off"]

            def synth(executable=binary, *extra, **env):
                return synthesize(executable, [*command, *extra], root, backend,
                                  options.fault_library, **env)

            synth(binary, "--aiger-abc", "off")
            original, original_size = circuit.read_bytes(), size(circuit)
            verify()
            for mode in (("--aiger-abc", "on"), ()):
                on = synth(binary, *mode)
                assert size(circuit) <= original_size, (arm, on, circuit.read_text())
                assert symbols(circuit.read_bytes()) == symbols(original), arm
                if size(circuit) == original_size:
                    assert circuit.read_bytes() == original, (arm, on)
                    assert not REQUIRE_REWRITE, ("controlled ABC rewrite did not run", arm, on)
                    print(f"{backend}: verified unchanged fallback (resource/platform/time budget)")
                else:
                    assert RSS_SAMPLING_AVAILABLE, arm
                verify()
                assert sentinel.read_text() == "keep this file unchanged\n"
                history = root / "abc.history"
                if not history.is_symlink():
                    assert not history.exists(), history.read_text() if history.exists() else ""
                    history.symlink_to(sentinel)
            # Deliberate memory/deadline declines must retain verified bytes.
            circuit.write_bytes(original)
            declined = run(options.budget, str(circuit), str(circuit))
            assert declined.returncode == 0 and not declined.stderr, declined
            assert circuit.read_bytes() == original
            verify()
            if options.fault_binary:
                failed = synth(options.fault_binary, "--aiger-abc", "on",
                               ACACIA_TEST_AIGER_ABC_FAILURE="1")
                if REQUIRE_REWRITE:
                    assert "forced ABC failure" in failed.stderr, failed
                assert circuit.read_bytes() == original, (arm, failed)
                verify()
            if backend in {"backward", "obligation"} and options.fault_library:
                for failure in ("exception", "stream", "oversize"):
                    failed = synth(binary, "--aiger-abc", "on",
                                   ACACIA_TEST_AIGER_STAGING_FAILURE=failure)
                    assert f"forced AIGER staging {failure}" in failed.stderr, failed
                    assert circuit.read_bytes() == original, (arm, failure, failed)
                    verify()
            assert sentinel.read_text() == "keep this file unchanged\n"
        assert set(root.iterdir()) == {circuit, sentinel, root / "abc.history"}


if __name__ == "__main__":
    main()
