#!/usr/bin/env python3
"""Check ABC grammar, circuit reduction, independent verification and fallback."""

import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

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


@contextmanager
def private_staging(root, enabled=True):
    mode = root.stat().st_mode & 0o777
    if not enabled:
        root.chmod(0o500)
    try:
        yield
    finally:
        root.chmod(mode)


def synthesize(binary, command, root, backend, library, *, staging=True, **overrides):
    with tempfile.TemporaryDirectory(dir=root) as directory:
        phases = Path(directory)
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases), **overrides)
        if library:
            env["LD_PRELOAD"] = library
        with private_staging(root, staging), \
                LegacyGate(env, backend == "obligation" and bool(library)) as gate:
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
            circuit.chmod(0o640)
            for mode in (("--aiger-abc", "on"), ()):
                on = synth(binary, *mode)
                assert circuit.stat().st_mode & 0o777 == 0o640, arm
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
            with tempfile.TemporaryDirectory(dir=root) as directory:
                target = Path(directory) / "target.aag"
                circuit.rename(target)
                link = target.relative_to(root)
                circuit.symlink_to(link)
                on = synth(binary, "--aiger-abc", "on")
                assert circuit.is_symlink() and circuit.readlink() == link, (arm, on)
                assert circuit.read_bytes() == target.read_bytes(), arm
                assert target.stat().st_mode & 0o777 == 0o640, arm
                if REQUIRE_REWRITE:
                    assert size(target) < original_size, (arm, on)
                verify()
                circuit.unlink()
                target.rename(circuit)
            if RSS_SAMPLING_AVAILABLE:
                with tempfile.TemporaryDirectory(dir=root) as directory:
                    directory = Path(directory)
                    snapshot = directory / "spot.aag"
                    snapshot.write_bytes(original)
                    helper = directory / "helper"
                    marker = directory / "invoked"
                    real_helper = Path(binary).with_name("acacia-aiger-abc")
                    helper.write_text(f'''#!{sys.executable}
import json, os, sys
from pathlib import Path
assert Path({str(circuit)!r}).read_bytes() == Path({str(snapshot)!r}).read_bytes()
rows = [json.loads(line) for path in Path(os.environ["ACACIA_PHASE_RECORDS"]).glob("*.jsonl")
        for line in path.read_text().splitlines() if line.endswith("}}")]
workers = [row["worker_pid"] for row in rows if row.get("event") == "worker_spawn"]
assert workers and os.getppid() not in workers
assert all(not Path(f"/proc/{{pid}}").exists() for pid in workers)
Path({str(marker)!r}).write_text("publisher after worker cleanup")
os.execv({str(real_helper)!r}, [{str(real_helper)!r}, *sys.argv[1:]])
''')
                    helper.chmod(0o755)
                    circuit.unlink()
                    published = synth(binary, "--aiger-abc", "on", ACACIA_ABC_HELPER=str(helper))
                    assert marker.exists() or not REQUIRE_REWRITE, published
                    verify()
            if backend == "backward":
                # Atomic ABC replacement must not detach hard links or replace a FIFO.
                alias = root / "alias.aag"
                alias.hardlink_to(circuit)
                inode = circuit.stat().st_ino
                synth(binary, "--aiger-abc", "on")
                assert circuit.stat().st_ino == alias.stat().st_ino == inode
                assert circuit.read_bytes() == alias.read_bytes() == original
                alias.unlink()
                fifo = root / "controller.fifo"
                os.mkfifo(fifo)
                with subprocess.Popen(["cat", str(fifo)], stdout=subprocess.PIPE) as reader:
                    try:
                        fifo_command = [str(fifo) if arg == str(circuit) else arg for arg in command]
                        synthesize(binary, [*fifo_command, "--aiger-abc", "on"], root,
                                   backend, options.fault_library)
                        received, _ = reader.communicate(timeout=15)
                        assert reader.returncode == 0 and received == original and fifo.is_fifo()
                    finally:
                        if reader.poll() is None:
                            reader.kill()
                            reader.communicate()
                fifo.unlink()
            if backend == "backward" and REQUIRE_REWRITE:
                # Exercise production discovery in a copied installation, while
                # the real build helper still exists. No implicit build fallback.
                with tempfile.TemporaryDirectory(dir=root) as installed:
                    installed = Path(installed)
                    executable = installed / "acacia-bonsai"
                    helper = installed / "acacia-aiger-abc"
                    shutil.copy2(binary, executable)

                    def installed_synth():
                        return synth(str(executable), "--aiger-abc", "on", ACACIA_ABC_HELPER="")

                    missing = installed_synth()
                    assert circuit.read_bytes() == original, missing
                    verify()
                    shutil.copy2(Path(binary).with_name(helper.name), helper)
                    matched = installed_synth()
                    if REQUIRE_REWRITE:
                        assert size(circuit) < original_size, matched
                    verify()
                    helper.chmod(0o644)
                    unexecutable = installed_synth()
                    assert circuit.read_bytes() == original, unexecutable
                    verify()

                    rows = original.decode().splitlines()
                    _, _, inputs, latches, outputs, _ = rows[0].split()
                    inputs, latches, outputs = map(int, (inputs, latches, outputs))
                    assert not latches and inputs > 1 and outputs > 1
                    # Valid and smaller, with the same names, but false outputs.
                    different = "\n".join([
                        f"aag {inputs} {inputs} 0 {outputs} 0",
                        *rows[1:1 + inputs], *["0"] * outputs,
                        *[line.decode() for line in symbols(original)], ""])

                    def frame(payload, version=1):
                        return f"acacia-aiger-abc {version} {len(payload.encode())}\n{payload}"

                    responses = {
                        "valid-but-wrong-helper": different,
                        "wrong-version": frame(different, 0),
                        "truncated-frame": frame(different)[:-5],
                        "extra-frame-output": frame(different) + "extra\n",
                        "truncated-aiger": frame("aag 1 1 0 1 0\n2\n"),
                        "extra-aiger-output": frame(different + "extra\n"),
                        "extra-latch-symbol": frame(different + "lgarbage\n"),
                        "extra-comment-output": frame(different + "c\nextra\n"),
                        "wrong-format": frame(different.replace("aag ", "aig ", 1)),
                        "unchanged-size": frame(original.decode()),
                    }
                    for kind in ("i", "o"):
                        first, second = [line.decode() for line in symbols(original)
                                         if line.startswith(kind.encode())][:2]
                        swapped = different.replace(first, "placeholder").replace(
                            second, first[:2] + second[2:]).replace(
                                "placeholder", second[:2] + first[2:])
                        responses[f"reordered-{kind}-names"] = frame(swapped)
                    for label, response in responses.items():
                        helper.write_text("#!/bin/sh\nprintf invoked > \"$0.ran\"\n"
                                          f"printf %s {shlex.quote(response)} > \"$2\"\n")
                        helper.chmod(0o755)
                        marker = helper.with_name(helper.name + ".ran")
                        failed = installed_synth()
                        assert marker.read_text() == "invoked", (label, failed)
                        marker.unlink()
                        assert circuit.read_bytes() == original, (label, failed)
                        verify()
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
                # Without private staging the selected worker still serializes
                # Spot's AIG for ABC; preserve coverage of that fallback writer.
                for failure in ("exception", "stream", "oversize"):
                    failed = synth(binary, "--aiger-abc", "on",
                                   staging=False,
                                   ACACIA_TEST_AIGER_STAGING_FAILURE=failure)
                    assert f"forced AIGER staging {failure}" in failed.stderr, failed
                    assert circuit.read_bytes() == original, (arm, failure, failed)
                    verify()
            if backend in {"backward", "parity"} and options.fault_library:
                # Expire the real invocation deadline inside optional work,
                # before helper supervision (staging), or with the writer stopped.
                with tempfile.TemporaryDirectory(dir=root) as directory:
                    helper = Path(directory) / "helper"
                    helper.write_text('#!/bin/sh\nprintf invoked > "$0.ran"\n'
                                      'printf "%s %s" "$$" "$PPID" > "$0.pids"\n'
                                      'kill -STOP "$PPID"\n')
                    helper.chmod(0o755)
                    env = dict(os.environ, LD_PRELOAD=options.fault_library,
                               ACACIA_OUTER_DEADLINE_MONOTONIC=str(time.monotonic() + 1))
                    if backend == "parity":
                        env["ACACIA_ABC_HELPER"] = str(helper)
                    else:
                        env["ACACIA_TEST_AIGER_STAGING_FAILURE"] = "stall"
                    # The fallback needs an existing writable destination when
                    # its parent directory cannot create private staging files.
                    circuit.write_text("previous controller\n")
                    with private_staging(root, backend != "backward"):
                        expired = run(binary, *command, "--aiger-abc", "on", env=env)
                    assert expired.returncode == 2 and '"stage":"deadline"' in expired.stderr, expired
                    if backend == "parity":
                        assert helper.with_name("helper.ran").read_text() == "invoked", expired
                        # The stopped writer is #475's publisher, after the
                        # selected worker exited; its helper must be reaped too.
                        for pid in helper.with_name("helper.pids").read_text().split():
                            assert not Path(f"/proc/{pid}").exists(), (pid, expired)
                    else:
                        assert "forced AIGER staging stall" in expired.stderr, expired
                    assert circuit.read_bytes() == original, (arm, expired)
                    verify()
            assert sentinel.read_text() == "keep this file unchanged\n"
        assert set(root.iterdir()) == {circuit, sentinel, root / "abc.history"}


if __name__ == "__main__":
    main()
