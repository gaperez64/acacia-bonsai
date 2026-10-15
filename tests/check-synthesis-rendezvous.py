#!/usr/bin/env python3
"""Hold two actual proofs ready, then assert exclusive controller construction."""

import importlib.util
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time

from moore_aiger_oracle import Aiger, check


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarking"))
spec = importlib.util.spec_from_file_location(
    "phase_completeness", Path(__file__).resolve().parents[1] /
    "benchmarking/attribution-completeness.py")
phase_completeness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(phase_completeness)


def check_records(directory, result, workers, *, cancelled=()):
    records = [dict(json.loads(line), emitter_pid=int(path.stem))
               for path in directory.glob("*.jsonl")
               for line in path.read_text().splitlines()]
    assert all(row.get("dropped_records", 0) == 0 for row in records), records
    writer, = [row for row in records if row.get("event") == "writer_summary"]
    assert writer["failed_records"] == 0 and not writer["incomplete_packet"], writer
    assert writer["delivered_records"] == len(records) - 1, writer
    starts = [row for row in records if row.get("event") == "worker_start"]
    assert len(starts) == workers, starts
    assert len([row for row in records if row.get("event") == "worker_spec"]) == workers
    assert len([row for row in records if row.get("event") == "parent_terminal"]) == workers
    for start in starts:
        pid = start["worker_pid"]
        emitted = [row for row in records if row.get("worker_pid") == pid
                   and row["emitter_pid"] == pid and "event" in row]
        # Obligation diagnostics have their own unsequenced schema. Every worker
        # lifecycle and phase event must carry the ordinary contiguous sequence.
        assert all("seq" in row or row["event"].startswith("obligation_")
                   for row in emitted), emitted
        child = [row for row in emitted if "seq" in row]
        assert [row["seq"] for row in child] == list(range(1, len(child) + 1)), child
        terminal, = [row for row in records if row.get("event") == "parent_terminal"
                     and row["worker_pid"] == pid]
        assert terminal["emitter_pid"] != pid, terminal
        snapshots = [row for row in records if row.get("event") == "stage_censored"
                     and row["worker_pid"] == pid]
        if pid in cancelled:
            assert terminal["telemetry"] == "incomplete" and terminal["signal"] == signal.SIGKILL
            assert terminal["reason"] in {"winner_cancelled", "interrupted"}, terminal
            # The gate holds an active phase, exercising the existing cancellation snapshot.
            censored, = snapshots
            assert censored["signal"] == signal.SIGKILL, censored
            assert censored["seq"] == len(child) + 1 and censored["emitter_pid"] != pid
            assert not any(row["event"] == "terminal_result" for row in child), child
        else:
            assert terminal["telemetry"] == "complete" and terminal["signal"] == 0, terminal
            assert child[-1]["event"] == "terminal_result", child
            assert not snapshots, snapshots
        assert terminal["seq"] == len(child) + len(snapshots) + 1, terminal
        for selection in (row for row in records if row.get("event") == "parent_verdict"
                          and row["worker_pid"] == pid):
            before = [row for row in child if row["mono_ns"] <= selection["mono_ns"]]
            assert selection["seq"] == len(before) + 1, selection
            assert selection["emitter_pid"] != pid and selection["reason"] == "selected"
        for winner in (row for row in records if row.get("event") == "parent_winner"
                       and row["worker_pid"] == pid):
            assert winner["seq"] == terminal["seq"] + 1 and winner["emitter_pid"] != pid
    # The ordinary route is also checked by the existing full attribution reader.
    if workers == 1:
        row = {"result": {0: "REALIZABLE", 1: "UNREALIZABLE"}.get(result, "UNKNOWN")}
        audit = phase_completeness.audit_row(row, directory, expected_workers=workers)
        assert audit["complete"], audit
    return records


def run(binary, root, ack_library, failure=False, interrupt=False, unreal=False, fast="off",
        helper_first=False):
    with tempfile.TemporaryDirectory(dir=root) as temporary:
        directory = Path(temporary)
        controller = directory / 'controller.aag'
        controller.write_text('previous controller\n')
        fail_pid = directory / 'fail-pid'
        ready_read, ready_write = os.pipe()
        export_read, export_write = os.pipe()
        helper_read, helper_write = os.pipe()
        phases = directory / "phases"
        phases.mkdir()
        env = dict(os.environ, ACACIA_TEST_SYNTHESIS_VERDICT_FD=str(ready_read),
                   ACACIA_TEST_SYNTHESIS_EXPORT_FD=str(export_read),
                   ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD=str(helper_read),
                   ACACIA_TEST_SYNTHESIS_FAIL_PID=str(fail_pid),
                   ACACIA_PHASE_RECORDS=str(phases), ACACIA_TEST_RECORD_ACK="1",
                   LD_PRELOAD=ack_library)
        arms = ('unreal:formula:backward' if unreal else
                'real:small:backward,both:obligation:spot')
        # The UNREAL fixture must reach a worker verdict even with the base preset.
        formula = 'F G o & G (i -> X !o)' if unreal else 'G (o <-> i)'
        process = subprocess.Popen([binary, '-f', formula, '-i', 'i', '-o', 'o',
                                    '--arms', arms, '--spot-fast', fast, '-s', str(controller)], env=env,
                                   pass_fds=(ready_read, export_read, helper_read),
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        os.close(ready_read)
        os.close(export_read)
        os.close(helper_read)
        rows = []
        pending = b''
        stderr_seen = bytearray()

        def exited(name):
            _, stderr = process.communicate(timeout=5)
            raise AssertionError(('process exited before readiness', name, process.returncode,
                                  rows, (bytes(stderr_seen) + stderr).decode(errors='replace')))

        def stage(name, count):
            nonlocal pending
            end = time.monotonic() + 15
            while len([row for row in rows if row.get('stage') == name]) < count:
                if process.poll() is not None:
                    exited(name)
                remaining = end - time.monotonic()
                assert remaining > 0, rows
                if not select.select([process.stderr], [], [], remaining)[0]:
                    raise AssertionError(('no readiness', name, rows))
                chunk = os.read(process.stderr.fileno(), 4096)
                if not chunk:
                    exited(name)
                stderr_seen.extend(chunk)
                pending += chunk
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    if line.startswith(b'{'):
                        rows.append(json.loads(line))
            return [row for row in rows if row.get('stage') == name]

        try:
            ready = stage('synthesis_ready', 1 if unreal else 2)
            assert controller.read_text() == 'previous controller\n'
            if helper_first:
                os.close(helper_write)
                helper_write = None
            else:
                os.close(ready_write)
                ready_write = None
            selected, = stage('synthesis_selected', 1)
            winner = selected['pid']
            starts = [json.loads(line) for path in phases.glob("*.jsonl")
                      for line in path.read_text().splitlines()]
            winner_record, = [row for row in starts if row.get("event") == "worker_start"
                              and row["worker_pid"] == winner]
            assert (winner_record["requested_backend"] == "obligation") == helper_first
            losers = [row['pid'] for row in ready if row['pid'] != winner]
            loser = losers[0] if losers else None
            # The winner has not constructed its AIG, and its competitor is stopped.
            if loser is not None:
                stopped_by = time.monotonic() + 15
                while True:
                    status = Path(f'/proc/{loser}/status').read_text()
                    if '\nState:\tT' in status:
                        break
                    # SIGSTOP is asynchronous; selection can be observed while it is pending.
                    assert time.monotonic() < stopped_by, status
                    time.sleep(.001)
            assert controller.read_text() == 'previous controller\n'
            if failure:
                fail_pid.write_text(str(winner))
                # Let the suspended proof publish its verdict when fallback resumes it.
                if helper_write is not None:
                    os.close(helper_write)
                    helper_write = None
                if ready_write is not None:
                    os.close(ready_write)
                    ready_write = None
            if interrupt:
                process.send_signal(signal.SIGTERM)
            else:
                os.close(export_write)
                export_write = None
            stdout, stderr = process.communicate(timeout=15)
            assert b'REALIZABLE\n' not in stderr
            if interrupt:
                assert process.returncode == 2 and stdout == b'', (stdout, stderr)
                assert controller.read_text() == 'previous controller\n'
            elif unreal:
                assert process.returncode == 1 and stdout == b'UNREALIZABLE\n', (stdout, stderr)
                assert controller.read_text() == 'previous controller\n'
            else:
                assert process.returncode == 0 and stdout.endswith(b'REALIZABLE\n'), (stdout, stderr)
                oracle = check(Aiger.read(controller), 0,
                               lambda state, letter: state if letter['o'] == letter['i']
                               else None, moore=False)
                assert oracle.valid, oracle
                if failure:
                    resumed = [json.loads(line) for line in stderr.splitlines()
                               if line.startswith(b'{') and b'synthesis_selected' in line]
                    assert any(row['pid'] == loser for row in resumed), resumed
            cancelled = ([winner, *losers] if interrupt else [] if failure else losers)
            check_records(phases, process.returncode, len(ready), cancelled=cancelled)
            for row in ready:
                try:
                    os.kill(row['pid'], 0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError(('worker survived', row))
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=5)
            for fd in (ready_write, export_write, helper_write):
                if fd is not None:
                    os.close(fd)


def failed_output(binary, root):
    with tempfile.TemporaryDirectory(dir=root) as temporary:
        destination = Path(temporary)  # Opening a directory as an output stream must fail.
        for inputs in ("i", ""):
            process = subprocess.run([binary, "-f", "G o", "-i", inputs, "-o", "o",
                                      "--arms", "real:small:backward", "-s", str(destination)],
                                     capture_output=True, timeout=15)
            assert process.returncode in (2, 3), process
            assert b"REALIZABLE" not in process.stdout, process


def decomposed_controller(binary, root, ack_library):
    with tempfile.TemporaryDirectory(dir=root) as temporary:
        controller = Path(temporary) / "mixed.aag"
        phases = Path(temporary) / "phases"
        phases.mkdir()
        env = dict(os.environ, ACACIA_PHASE_RECORDS=str(phases), ACACIA_TEST_RECORD_ACK="1",
                   LD_PRELOAD=ack_library)
        result = subprocess.run(
            [binary, "-f", "G(o <-> i) & G F p", "-i", "i", "-o", "o,p",
             "--arms", "real:small:backward", "--spot-fast", "off", "-K", "4",
             "-s", str(controller)], capture_output=True, timeout=15, env=env)
        assert result.returncode == 0, result
        records = check_records(phases, result.returncode, 1)
        completed = [row for row in records if row.get("event") == "stage_completion"
                     and row.get("stage") == "subjob"]
        assert {row["subjob"] for row in completed} == {1, 2}, completed
        circuit = Aiger.read(controller)
        assert circuit.input_names == ("i",) and set(circuit.output_names) == {"o", "p"}
        todo = list(circuit.initial_states())
        seen = set(todo)
        bad_edges = {}
        while todo:
            state = todo.pop()
            bad_edges[state] = []
            for letter in (False, True):
                outputs, following = circuit.step(state, (letter,))
                outputs = dict(zip(circuit.output_names, outputs))
                assert outputs["o"] == letter, (state, letter, outputs)
                if not outputs["p"]:
                    bad_edges[state].append(following)
                if following not in seen:
                    seen.add(following)
                    assert len(seen) <= 200000, "oracle state cap"
                    todo.append(following)
        # A reachable cycle containing only !p edges violates GF p.
        colors = {}

        def visit(state):
            assert colors.get(state, 0) != 1, ("liveness counterexample", state)
            if colors.get(state, 0) == 2:
                return
            colors[state] = 1
            for following in bad_edges[state]:
                visit(following)
            colors[state] = 2

        for state in seen:
            visit(state)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    root = Path(sys.argv[2])
    ack_library = str(Path(sys.argv[3]).resolve())
    run(binary, root, ack_library)
    run(binary, root, ack_library, fast="det")
    run(binary, root, ack_library, helper_first=True)
    run(binary, root, ack_library, failure=True)
    run(binary, root, ack_library, failure=True, helper_first=True)
    run(binary, root, ack_library, interrupt=True)
    run(binary, root, ack_library, unreal=True)
    failed_output(binary, root)
    decomposed_controller(binary, root, ack_library)
    print('exclusive synthesis, construction failure fallback, interruption and UNREAL passed')


if __name__ == '__main__':
    main()
