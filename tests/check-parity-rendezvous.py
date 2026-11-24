#!/usr/bin/env python3
"""Exercise selection, export failure, interruption and memory supervision on actual parity proofs."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import resource
import select
import signal
import subprocess
import tempfile
import time

from moore_aiger_oracle import Aiger, check


class Events:
    def __init__(self, process):
        self.process = process
        self.pending = b""
        self.rows = []

    def wait(self, stage, count=1):
        deadline = time.monotonic() + 20
        while len([row for row in self.rows if row.get("stage") == stage]) < count:
            remaining = deadline - time.monotonic()
            assert remaining > 0 and select.select([self.process.stderr], [], [], remaining)[0], (
                "readiness missing", stage, self.rows)
            chunk = os.read(self.process.stderr.fileno(), 4096)
            assert chunk, ("early exit", self.process.poll(), self.rows)
            self.pending += chunk
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                if line.startswith(b"{"):
                    self.rows.append(json.loads(line))
        return [row for row in self.rows if row.get("stage") == stage]


def records(directory):
    return [json.loads(line) for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines()]


def wait_stopped(pid):
    deadline = time.monotonic() + 20
    while "\nState:\tT" not in Path(f"/proc/{pid}/status").read_text():
        assert time.monotonic() < deadline, ("worker did not stop", pid)
        select.select([], [], [], .001)


def run(binary, scratch, *, parity_first=True, failure=False, interrupt=False,
        parent_death=False, unreal=False):
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        root = Path(temporary)
        target = root / "controller.aag"
        target.write_text("previous controller\n")
        phases = root / "phases"
        phases.mkdir()
        gates = [os.pipe() for _ in range(4)]
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases),
                   ACACIA_TEST_SYNTHESIS_VERDICT_FD=str(gates[0][0]),
                   ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD=str(gates[1][0]),
                   ACACIA_TEST_SYNTHESIS_EXPORT_FD=str(gates[2][0]),
                   ACACIA_TEST_SYNTHESIS_GRACE_FD=str(gates[2][0]),
                   ACACIA_TEST_SYNTHESIS_SELECTION_FD=str(gates[3][0]),
                   ACACIA_TEST_SYNTHESIS_FAIL_PID=str(root / "fail-pid"))
        formula = "G (o <-> X i)" if unreal else "G (o <-> i)"
        arms = "both:parity:spot" if unreal else "real:small:backward,both:parity:spot"
        process = subprocess.Popen([binary, "-f", formula, "-i", "i", "-o", "o", "--arms", arms,
                                    "-s", str(target)], env=env,
                                   pass_fds=tuple(read for read, _ in gates),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for read, _ in gates:
            os.close(read)
        writes = [write for _, write in gates]
        workers = []
        try:
            events = Events(process)
            ready = events.wait("synthesis_ready", 1 if unreal else 2)
            workers = [row["pid"] for row in ready]
            assert target.read_text() == "previous controller\n"
            first = 1 if parity_first or unreal else 0
            os.close(writes[first])
            writes[first] = -1
            events.wait("synthesis_proved")
            if not unreal:
                other = 1 - first
                os.close(writes[other])
                writes[other] = -1
                events.wait("synthesis_proved", 2)
            os.close(writes[3])
            writes[3] = -1
            selected, = events.wait("synthesis_selected")
            if not unreal:
                expected, = [row["pid"] for row in ready
                             if row["worker_kind"] == ("parity" if parity_first else "legacy")]
                assert selected["pid"] == expected, (selected, workers)
            assert target.read_text() == "previous controller\n"
            if failure:
                (root / "fail-pid").write_text(str(selected["pid"]))
            if parent_death:
                private, = root.glob("*.acacia-private-*")
                partial = private / ("1" if parity_first else "0")
                partial.write_text("partial controller\n")
                process.kill()
            elif interrupt:
                process.send_signal(signal.SIGTERM)
            else:
                os.close(writes[2])
                writes[2] = -1
            stdout, stderr = process.communicate(timeout=20)
            if parent_death:
                assert process.returncode == -signal.SIGKILL
                assert target.read_text() == "previous controller\n"
                for worker in workers:
                    pid, status = os.waitpid(worker, 0)
                    assert pid == worker and os.WIFSIGNALED(status), status
                    assert os.WTERMSIG(status) == signal.SIGKILL, status
                assert private.is_dir() and partial.read_text() == "partial controller\n"
            elif interrupt:
                assert process.returncode == 2 and not stdout, (stdout, stderr)
                assert target.read_text() == "previous controller\n"
            elif unreal:
                assert process.returncode == 1 and stdout == b"UNREALIZABLE\n", (stdout, stderr)
                assert target.read_text() == "previous controller\n"
            else:
                assert process.returncode == 0 and stdout.endswith(b"REALIZABLE\n"), (stdout, stderr)
                oracle = check(Aiger.read(target), 0, lambda state, letter:
                               state if letter["o"] == letter["i"] else None, moore=False)
                assert oracle.valid, oracle
                rows = records(phases)
                winner, = [row for row in rows if row.get("event") == "parent_winner"]
                assert (winner["requested_backend"] == "parity") == (parity_first != failure), rows
                exporters = [row for row in rows if row.get("event") == "stage_entry"
                             and row.get("stage") in {"strategy", "encode-controller", "parity_export"}]
                if parity_first and not failure:
                    assert exporters and all(row["worker_pid"] == selected["pid"] for row in exporters)
            if not parent_death:
                for worker in workers:
                    assert not Path(f"/proc/{worker}").exists(), worker
                rows = records(phases)
                assert len([row for row in rows if row.get("event") == "parent_terminal"]) == len(workers)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=20)
            for write in writes:
                if write >= 0:
                    os.close(write)
            for worker in workers:
                try:
                    os.kill(worker, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    os.waitpid(worker, os.WNOHANG)
                except ChildProcessError:
                    pass


def memory_limit(binary, scratch, updated_limit=None):
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        root = Path(temporary)
        gate_read, gate_write = os.pipe()
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(root),
                   ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD=str(gate_read),
                   ACACIA_TEST_SYNTHESIS_PARITY_RSS_BYTES=str(144 * 1024 * 1024))
        flags = ["--parity-memory-share=equal"]
        if updated_limit is not None:
            (root / "memory.max").write_text(str(800 * 1024 * 1024))
            (root / "memory.current").write_text(str(350 * 1024 * 1024))
            env["ACACIA_TEST_CGROUP_DIRECTORY"] = str(root)
            flags = ["--parity-memory-share=headroom"]

        def limit():
            resource.setrlimit(resource.RLIMIT_AS, (256 * 1024 * 1024, 256 * 1024 * 1024))

        process = subprocess.Popen([binary, "-f", "G o", "-i", "", "-o", "o", "--arms", "both:parity:spot",
                                    "-s", str(root / "controller.aag"), *flags], env=env, preexec_fn=limit,
                                   pass_fds=(gate_read,), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        os.close(gate_read)
        try:
            if updated_limit is not None:
                Events(process).wait("synthesis_ready")
                # The over-share worker must survive while the scope has headroom.
                try:
                    process.wait(timeout=0.1)
                except subprocess.TimeoutExpired:
                    pass
                else:
                    raise AssertionError(("worker killed before limit change", process.returncode))
                if updated_limit:
                    (root / "next.max").write_text(updated_limit)
                    (root / "next.max").replace(root / "memory.max")
                else:
                    (root / "memory.max").unlink()
            stdout, stderr = process.communicate(timeout=20)
            assert process.returncode == 2 and not stdout, (stdout, stderr)
            rows = records(root)
            terminal, = [row for row in rows if row.get("event") == "parent_terminal"]
            assert terminal["reason"] == "rss_share" and terminal["signal"] == signal.SIGKILL, rows
            assert not (root / "controller.aag").exists()
        finally:
            os.close(gate_write)
            if process.poll() is None:
                process.kill()
                process.wait(timeout=20)


def stalled_extractor(binary, checker, scratch, mode):
    """Pipe gates fix proof order, turns and exits without timing margins."""
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        root = Path(temporary)
        target = root / "controller.aag"
        spec = root / "spec.tlsf"
        spec.write_text('''INFO { TITLE: "selection race" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEE { G (o <-> i); } }
''')
        gates = [os.pipe() for _ in range(8)]
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(root),
                   ACACIA_TEST_SYNTHESIS_VERDICT_FD=str(gates[0][0]),
                   ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD=str(gates[1][0]),
                   ACACIA_TEST_SYNTHESIS_EXPORT_FD=str(gates[2][0]),
                   ACACIA_TEST_SYNTHESIS_PARITY_EXPORT_FD=str(gates[3][0]),
                   ACACIA_TEST_SYNTHESIS_GRACE_FD=str(gates[4][0]),
                   ACACIA_TEST_SYNTHESIS_SELECTION_FD=str(gates[5][0]),
                   ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD=str(gates[6][0]),
                   ACACIA_TEST_SYNTHESIS_FAIL_PID=str(root / "fail-pid"))
        pressure = mode in {"headroom", "scan", "growth", "failure-pressure", "failure-blocked",
                            "original-return", "turns-legacy", "turns-parity"}
        if pressure:
            (root / "memory.max").write_text(str(512 * 1024 * 1024))
            # Stopped workers' charges are included; 128 MiB of usable headroom otherwise.
            used = 460 if mode in {"headroom", "failure-pressure", "failure-blocked"} else 320
            (root / "memory.current").write_text(str(used * 1024 * 1024))
            env["ACACIA_TEST_CGROUP_DIRECTORY"] = str(root)
        if mode == "scan":
            env["ACACIA_TEST_SYNTHESIS_PARITY_RSS_BYTES"] = str(160 * 1024 * 1024)
        if mode == "growth":
            env["ACACIA_TEST_SYNTHESIS_LEGACY_EXPORT_RSS_BYTES"] = str(128 * 1024 * 1024)
        if mode == "original-return":
            env["ACACIA_TEST_SYNTHESIS_LEGACY_RSS_BYTES"] = str(96 * 1024 * 1024)
        if mode == "original-ceiling":
            env["ACACIA_TEST_SYNTHESIS_LEGACY_EXPORT_RSS_BYTES"] = str(128 * 1024 * 1024)
        if mode == "stop-pending":
            env["ACACIA_TEST_SYNTHESIS_STOP_FD"] = str(gates[7][0])
        if mode == "failure-pressure":
            env["ACACIA_TEST_SYNTHESIS_RECLAIM_FD"] = str(gates[7][0])
        if mode == "conflict":
            env["ACACIA_TEST_SYNTHESIS_CONFLICT"] = "1"
        arms = "real:small:backward"
        if mode != "alone":
            arms += ",both:parity:spot"
        three = mode in {"scan", "failure-pressure", "failure-blocked", "undecided"}
        if three:
            arms += ",both:obligation:spot"
        process = subprocess.Popen([binary, "-T", str(spec), "--arms", arms, "--spot-fast", "off",
                                    "-s", str(target)],
                                   env=env, pass_fds=tuple(read for read, _ in gates),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for read, _ in gates:
            os.close(read)
        writes = [write for _, write in gates]

        def release(index):
            os.close(writes[index])
            writes[index] = -1

        def wait_record(reason):
            end = time.monotonic() + 20
            while not any(row.get("reason") == reason for row in records(root)):
                assert process.poll() is None and time.monotonic() < end, records(root)
                select.select([], [], [], .001)

        try:
            events = Events(process)
            ready = events.wait("synthesis_ready", 3 if three else 1 if mode == "alone" else 2)
            pids = {row["worker_kind"]: row["pid"] for row in ready}
            first_kind = "parity" if mode in {"growth", "undecided"} else "legacy"
            release(1 if first_kind == "parity" else 0)
            events.wait("synthesis_proved")
            events.wait("synthesis_selecting")
            # Publish matching proofs before SIGSTOP; an undecided helper stays gated.
            if mode != "alone":
                release(0 if first_kind == "parity" else 1)
                events.wait("synthesis_proved", 2)
            if mode in {"scan", "failure-blocked"}:
                release(6)
                events.wait("synthesis_proved", 3)
            release(5)
            selected, = events.wait("synthesis_selected")
            assert selected["pid"] == pids[first_kind], (selected, ready)
            events.wait("synthesis_turn")
            original_limit = resource.prlimit(selected["pid"], resource.RLIMIT_AS)
            assert not target.exists()
            if mode == "fallback":
                private, = root.glob("*.acacia-private-*")
                (private / "0").write_text("partial controller\n")
            if mode.startswith("failure-"):
                (root / "fail-pid").write_text(str(selected["pid"]))
                release(2)
                if mode == "failure-pressure":
                    events.wait("synthesis_reclaimed")
                    assert not Path(f"/proc/{pids['obligation']}").exists()
                    assert any(row.get("reason") == "extraction_irrelevant"
                               and row["worker_pid"] == pids["obligation"] for row in records(root))
                    (root / "memory.current").write_text(str(320 * 1024 * 1024))
                    release(7)
                    events.wait("synthesis_selected", 2)
                    release(3)
            elif mode == "boundary":
                os.write(writes[4], b"e")
                events.wait("synthesis_exit_boundary")
                release(2)
            else:
                os.write(writes[4], b"x")
                if mode == "stop-pending":
                    events.wait("synthesis_stop_pending")
                    os.write(writes[4], b"t")
                elif mode in {"alone", "headroom", "conflict"}:
                    reason = {"headroom": "scope_headroom", "conflict": "verdict_conflict",
                              "alone": "no_alternative"}[mode]
                    wait_record(reason)
                    if mode != "alone":
                        wait_stopped(pids["parity"])
                        assert not any(row.get("event") == "parent_verdict"
                                       and row["worker_pid"] == pids["parity"] for row in records(root))
                    if mode == "alone":
                        os.write(writes[4], b"t")
                    else:
                        process.send_signal(signal.SIGTERM)
                else:
                    turns = events.wait("synthesis_turn", 2)
                    expected = "obligation" if mode == "scan" else (
                        "legacy" if first_kind == "parity" else "parity")
                    assert turns[-1]["worker_pid"] == pids[expected], (turns, pids)
                    events.wait("synthesis_selected", 2)
                    wait_stopped(selected["pid"])
                    if mode == "scan":
                        wait_record("scope_headroom")
                        wait_stopped(pids["parity"])
                    if mode == "undecided":
                        wait_stopped(pids["obligation"])
                    if mode == "growth":
                        soft, hard = resource.prlimit(pids["legacy"], resource.RLIMIT_AS)
                        assert soft != resource.RLIM_INFINITY and hard == original_limit[1], (soft, hard)
                        os.write(writes[2], b"x")  # Grow after SIGCONT, then block export again.
                        failed, = events.wait("synthesis_allocation_failed")
                        assert failed["pid"] == pids["legacy"], failed
                        turns = events.wait("synthesis_turn", 3)
                        assert turns[-1]["worker_pid"] == pids["parity"], turns
                        assert not Path(f"/proc/{pids['legacy']}").exists()
                        release(3)
                    elif mode in {"original-return", "original-ceiling"}:
                        private, = root.glob("*.acacia-private-*")
                        partial = private / "1"
                        partial.write_text("partial fallback controller\n")
                        if mode == "original-return":
                            (root / "memory.current").write_text(str(400 * 1024 * 1024))
                            os.write(writes[4], b"x")
                        else:
                            (root / "fail-pid").write_text(str(pids["parity"]))
                            release(3)
                        turns = events.wait("synthesis_turn", 3)
                        assert turns[-1]["worker_pid"] == pids["legacy"], turns
                        assert resource.prlimit(pids["legacy"], resource.RLIMIT_AS) == original_limit
                        assert not partial.exists()
                        if mode == "original-return":
                            wait_record("original_headroom")
                            assert not Path(f"/proc/{pids['parity']}").exists()
                        release(2)
                        expected = "legacy"
                    elif mode == "sole-fallback":
                        soft, hard = resource.prlimit(pids["parity"], resource.RLIMIT_AS)
                        assert soft != hard, (soft, hard)
                        os.kill(pids["legacy"], signal.SIGKILL)
                        wait_record("sole_extractor")
                        assert resource.prlimit(pids["parity"], resource.RLIMIT_AS) == (hard, hard)
                        release(3)
                    elif mode.startswith("turns-"):
                        total = 7 if mode == "turns-legacy" else 6
                        for count in range(3, total + 1):
                            os.write(writes[4], b"x")
                            turns = events.wait("synthesis_turn", count)
                        assert [row["worker_pid"] for row in turns] == [
                            pids["legacy" if i % 2 == 0 else "parity"] for i in range(total)]
                        assert [row["quantum_ns"] for row in turns] == [
                            250_000_000 * 2 ** (i // 2) for i in range(total)], turns
                        # Both extractors can now finish; the active worker must win immediately.
                        release(2)
                        release(3)
                        expected = mode.removeprefix("turns-")
                    else:
                        release(3 if expected == "parity" else 2)
            stdout, stderr = process.communicate(timeout=20)
            rows = records(root)
            if mode in {"alone", "headroom", "conflict", "failure-blocked", "stop-pending"}:
                expected_code = 3 if mode == "failure-blocked" else 2
                assert process.returncode == expected_code and not stdout, (process.returncode, stdout, stderr)
                assert not target.exists()
                if mode in {"alone", "stop-pending"}:
                    assert b'"stage":"deadline","status":"expired"' in stderr, stderr
                if mode == "failure-blocked":
                    assert any(row.get("reason") == "extraction_failed" for row in rows), rows
            else:
                assert process.returncode == 0 and stdout.endswith(b"REALIZABLE\n"), (stdout, stderr)
                checked = subprocess.run([checker, str(spec), str(target)],
                                         capture_output=True, timeout=20)
                assert checked.returncode == 0 and checked.stdout == b"verified\n", checked
                published, = [row for row in rows if row.get("event") == "parent_extraction"
                              and row["reason"] == "published"]
                winner_kind = ("legacy" if mode == "boundary" else "parity" if mode in {
                    "growth", "failure-pressure"} else expected)
                assert published["worker_pid"] == pids[winner_kind], rows
                if mode == "growth":
                    terminal, = [row for row in rows if row.get("event") == "parent_terminal"
                                 and row["worker_pid"] == pids["legacy"]]
                    assert terminal["exit_code"] == 3 and terminal["signal"] == 0, terminal
                if mode in {"original-return", "original-ceiling"}:
                    assert not any(row.get("reason") in {"scope_headroom", "rss_budget"}
                                   and row["worker_pid"] == pids["legacy"] for row in rows), rows
                if mode == "boundary":
                    assert not any(row.get("reason") == "fallback" for row in rows), rows
                else:
                    assert any(row.get("reason") == "fallback" for row in rows), rows
            assert not list(root.glob("*.acacia-*")), list(root.iterdir())
            assert all(not Path(f"/proc/{row['pid']}").exists() for row in ready)
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=20)
            for fd in writes:
                if fd >= 0:
                    os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("checker", type=Path)
    args = parser.parse_args()
    libc = ctypes.CDLL(None, use_errno=True)
    assert libc.prctl(36, 1, 0, 0, 0) == 0  # Adopt children when their parent is killed.
    binary = str(args.binary.resolve())
    for mode in ("fallback", "alone", "headroom", "conflict", "scan", "growth",
                 "failure-pressure", "failure-blocked", "boundary", "turns-legacy",
                 "turns-parity", "undecided", "original-return", "original-ceiling",
                 "sole-fallback", "stop-pending"):
        if mode in {"growth", "sole-fallback"} and os.environ.get("ACACIA_TEST_SKIP_RLIMIT_AS") == "1":
            print(f"Skipping {mode} RLIMIT_AS case: incompatible with ASan shadow memory")
            continue
        stalled_extractor(binary, str(args.checker.resolve()), args.scratch, mode)
    for parity_first in (False, True):
        run(binary, args.scratch, parity_first=parity_first)
        run(binary, args.scratch, parity_first=parity_first, failure=True)
    run(binary, args.scratch, interrupt=True)
    run(binary, args.scratch, parent_death=True)
    run(binary, args.scratch, unreal=True)
    if os.environ.get("ACACIA_TEST_SKIP_RLIMIT_AS") == "1":
        print("Skipping RLIMIT_AS case: incompatible with ASan shadow memory")
    else:
        memory_limit(binary, args.scratch)
        for updated_limit in (str(400 * 1024 * 1024), "max", ""):
            memory_limit(binary, args.scratch, updated_limit)
    print("Parity selection, fallback, cancellation, parent death and RSS supervision passed")


if __name__ == "__main__":
    main()
