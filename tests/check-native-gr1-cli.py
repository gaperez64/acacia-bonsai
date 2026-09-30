#!/usr/bin/env python3
"""Exercise exact native arm polarity, parsing, deadlines, and renaming."""

from __future__ import annotations

from importlib.util import module_from_spec, spec_from_file_location
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]

spec = spec_from_file_location("obfuscate_tlsf", ROOT / "benchmarking" /
                               "tools" / "obfuscate-tlsf.py")
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
    record_worker = Path(sys.argv[3]).resolve()
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
            expect(binary, ["-T", str(source), "--arms", "both:gr1:oxidd"],
                   code, verdict)
            expect(binary, ["--arms", f"{winner}:gr1:oxidd", "-T", str(source)],
                   code, verdict)
            expect(binary, ["-T", str(source), "--arms", f"{loser}:gr1:oxidd"],
                   2, "opposite side solved")
            renamed, _ = obfuscator.write_obfuscated(source, directory, code + 11)
            expect(binary, ["-T", str(renamed), "--arms", f"{winner}:gr1:oxidd"],
                   code, verdict)
            expect(binary, ["-T", str(renamed), "--arms", "both:gr1:oxidd"],
                   code, verdict)
        expect(binary, ["-T", str(adversarial), "--arms", "real:gr1:oxidd"],
               0, "REALIZABLE")
        crossed_renamed, _ = obfuscator.write_obfuscated(adversarial, directory, 37)
        expect(binary, ["-T", str(crossed_renamed), "--arms", "real:gr1:oxidd"],
               0, "REALIZABLE")
        expect(binary, ["-T", str(real), "--arms",
                        "real:gr1:oxidd,unreal:formula:backward"], 0, "REALIZABLE",
               time.monotonic() + 3)
        expect(binary, ["-T", str(unreal), "--arms",
                        "real:gr1:oxidd,both:gr1:oxidd"], 1, "UNREALIZABLE",
               time.monotonic() + 3)
        expect(binary, ["-T", str(malformed), "--arms", "real:gr1:oxidd"],
               2, "UNKNOWN")
        both_failure = run(binary, ["-T", str(malformed), "--arms", "both:gr1:oxidd"])
        assert both_failure.returncode == 2 and both_failure.stderr.endswith("UNKNOWN\n")
        assert any(json.loads(line).get("arm") == "both:gr1:oxidd"
                   for line in both_failure.stderr.splitlines()[:-1])
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
        expect(binary, ["-T", str(real), "--arms", "both:gr1:oxidd"],
               2, "deadline", time.monotonic() - 1)
        if "--hook-binary" in sys.argv[3:]:
            hook_binary = Path(sys.argv[sys.argv.index("--hook-binary") + 1])
            for source in (real, unreal):
                expect(hook_binary, ["-T", str(source), "--arms", "both:gr1:oxidd"],
                       2, "proof not verified", corrupt_proof=True)
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
            ("both:param-lift:oxidd", "both is supported only for the gr1 transform"),
            ("both:small:backward", "both is supported only for the gr1 transform"),
            ("both:gr1:backward", "requires the oxidd backend"),
            ("both:gr1:oxidd:frozen-graph", "does not accept a provider"),
            ("both:gr1:oxidd,both:gr1:oxidd", "duplicate arm"),
            ("neither:gr1:oxidd", "invalid polarity"),
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
        check_record_failures(record_worker, binary, real, directory)
    print("native GR(1) CLI checks passed")


def check_record_failures(worker: Path, binary: Path, real: Path,
                          root: Path) -> None:
    def execute(record_dir: Path | None, *, fifo: bool = False,
                fifo_reader: bool = False, file_limit: bool = False,
                stall: bool = False, flood: bool = False,
                kill_writer: bool = False) -> tuple[int, str, float]:
        env = os.environ.copy()
        env.pop("ACACIA_PHASE_RECORDS", None)
        env.pop("ACACIA_TEST_RECORD_WRITER_STALL", None)
        if record_dir is not None:
            env["ACACIA_PHASE_RECORDS"] = str(record_dir)
        if stall:
            env["ACACIA_TEST_RECORD_WRITER_STALL"] = "1"
        def limit_file() -> None:
            resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
        before = time.monotonic()
        process = subprocess.Popen([str(worker)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, env=env,
                                   preexec_fn=limit_file if file_limit else None)
        reader_fd = None
        if fifo:
            assert record_dir is not None
            os.mkfifo(record_dir / f"{process.pid}.jsonl")
            if fifo_reader:
                reader_fd = os.open(record_dir / f"{process.pid}.jsonl",
                                    os.O_RDONLY | os.O_NONBLOCK)
        # The worker waits on stdin after recorder setup. This checks that
        # records-off creates no writer child or pipe.
        children = Path(f"/proc/{process.pid}/task/{process.pid}/children")
        for _ in range(100):
            count = len(children.read_text().split())
            if count or record_dir is None:
                break
            time.sleep(.001)
        assert count == (1 if record_dir is not None else 0), count
        if kill_writer:
            assert count == 1
            os.kill(int(children.read_text().split()[0]), signal.SIGKILL)
        try:
            stdout, stderr = process.communicate("f\n" if flood else "\n", timeout=5)
        finally:
            if reader_fd is not None:
                os.close(reader_fd)
        assert not stderr, stderr
        return process.returncode, stdout, time.monotonic() - before

    baseline = execute(None)
    assert baseline[:2] == (0, "REALIZABLE\n")
    working = root / "records-working"
    working.mkdir()
    assert execute(working)[:2] == baseline[:2]
    written = [json.loads(line) for path in working.glob("*.jsonl")
               for line in path.read_text().splitlines()]
    assert any(row.get("phase") == "record_failure_test" for row in written)
    assert any(row.get("phase") == "record_summary" and
               row.get("dropped_records") == 0 for row in written)
    assert execute(working, kill_writer=True)[:2] == baseline[:2]
    fifo_dir = root / "records-fifo"
    fifo_dir.mkdir()
    assert execute(fifo_dir, fifo=True)[:2] == baseline[:2]
    assert execute(fifo_dir, fifo=True, fifo_reader=True)[:2] == baseline[:2]
    limit_dir = root / "records-limit"
    limit_dir.mkdir()
    assert execute(limit_dir, file_limit=True)[:2] == baseline[:2]
    assert len(list(limit_dir.iterdir())) == 1
    assert next(limit_dir.iterdir()).stat().st_size == 0
    unwritable = root / "records-unwritable"
    unwritable.mkdir(mode=0o500)
    try:
        assert execute(unwritable)[:2] == baseline[:2]
        if os.geteuid() != 0:
            assert not list(unwritable.iterdir())
    finally:
        unwritable.chmod(0o700)
    stalled = root / "records-stalled-regular"
    stalled.mkdir()
    result = execute(stalled, stall=True)
    assert result[:2] == baseline[:2] and result[2] < 1, result
    flood_result = execute(stalled, stall=True, flood=True)
    assert flood_result[:2] == baseline[:2] and flood_result[2] < 1, flood_result
    assert not list(stalled.iterdir())
    # The production single-arm path has the same parent/writer lifecycle.
    def run_real(record_dir: Path | None, deadline: float,
                 arm: str = "real:gr1:oxidd") -> tuple[int, str, float]:
        env = os.environ.copy()
        env.pop("ACACIA_PHASE_RECORDS", None)
        if record_dir is not None:
            env["ACACIA_PHASE_RECORDS"] = str(record_dir)
        env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = str(deadline)
        before = time.monotonic()
        proc = subprocess.run([str(binary), "-T", str(real), "--arms", arm],
                              capture_output=True, text=True, env=env, timeout=5)
        return proc.returncode, proc.stdout, time.monotonic() - before
    baseline_real = run_real(None, time.monotonic() + 2)
    with_records = run_real(root, time.monotonic() + 2)
    assert with_records[:2] == baseline_real[:2], (baseline_real, with_records)
    assert with_records[2] < 2, with_records
    both_records_dir = root / "records-both"
    both_records_dir.mkdir()
    both_result = run_real(both_records_dir, time.monotonic() + 2, "both:gr1:oxidd")
    assert both_result[:2] == baseline_real[:2], (baseline_real, both_result)
    both_records = [json.loads(line) for path in both_records_dir.glob("*.jsonl")
                    for line in path.read_text().splitlines()]
    assert any(row.get("phase") == "proof_binding" and
               row.get("arm") == "both:gr1:oxidd" for row in both_records)
    assert any(row.get("phase") == "record_summary" and
               row.get("arm") == "both:gr1:oxidd" for row in both_records)
    unknown_off = run_real(None, time.monotonic() + 2, "unreal:gr1:oxidd")
    unknown_on = run_real(root, time.monotonic() + 2, "unreal:gr1:oxidd")
    assert unknown_on[:2] == unknown_off[:2] and unknown_on[0] == 2, (unknown_off, unknown_on)
    assert unknown_on[2] < 2, unknown_on
    expired_off = run_real(None, time.monotonic() - 1)
    expired_on = run_real(root, time.monotonic() - 1)
    assert expired_on[:2] == expired_off[:2] == (2, ""), (expired_off, expired_on)
    assert expired_on[2] < .5, expired_on


if __name__ == "__main__":
    main()
