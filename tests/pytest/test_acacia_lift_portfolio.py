"""Fast stdlib tests for the lifting wrapper and its opt-in runner hook."""

from __future__ import annotations

import csv
import contextlib
import ctypes
import importlib.util
import io
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
WRAPPER = ROOT / "scripts" / "acacia-lift-portfolio.py"
COVERAGE = ROOT / "benchmarking" / "run-syntcomp26-coverage.py"
BENCHMARKING = ROOT / "benchmarking"


def load_module(name: str, path: pathlib.Path):
    old_path = list(sys.path)
    sys.path.insert(0, str(path.parent))
    try:
        spec = importlib.util.spec_from_file_location(name, path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path[:] = old_path


LIFT_SCRIPT = r'''#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import pathlib
import signal
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument("--request-mode")
parser.add_argument("-T", dest="tlsf")
parser.add_argument("--budget")
parser.add_argument("--output-dir")
parser.add_argument("--evidence-out")
parser.add_argument("--real-check")
args, _ = parser.parse_known_args()
counter = os.environ.get("FAKE_LIFT_COUNTER")
if counter:
    with open(counter, "a", encoding="utf-8") as stream:
        stream.write("1\n")
data = pathlib.Path(args.tlsf).read_bytes()
capture = os.environ.get("FAKE_LIFT_INPUT")
if capture:
    pathlib.Path(capture).write_bytes(data)
mode = os.environ.get("FAKE_LIFT_MODE", "decline")
verified = False
if mode in {"timeout", "closed_pipe_descendant"}:
    pathlib.Path(os.environ["FAKE_PGID"]).write_text(str(os.getpgrp()))
    child = subprocess.Popen([
        sys.executable, "-c",
        "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)",
    ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
       stderr=subprocess.DEVNULL, close_fds=True)
if mode == "timeout":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    time.sleep(30)
if mode != "lying":
    verified = mode in {
        "win", "win_unreal", "exact_real", "real_route_unreal",
        "one_sided_unreal", "missing_input_hash", "missing_artifact_hash",
        "missing_target_hash", "wrong_hash", "stdin_mismatch",
    }
    if mode in {"win_unreal", "real_route_unreal", "one_sided_unreal"}:
        verdict = "UNREALIZABLE"
    else:
        verdict = "REALIZABLE" if verified or mode == "unverified_claim" else "UNKNOWN"
    route = "direct-certified" if mode not in {"real_route_unreal", "one_sided_unreal"} else "unsupported"
    digest = hashlib.sha256(data).hexdigest()
    if mode == "stdin_mismatch":
        digest = hashlib.sha256(b"different spooled input").hexdigest()
    evidence = {
        "route": route,
        "target_verified": verified,
        "source_binding": {"input": {"sha256": digest}},
        "target_certificate": {
            "source_sha256": digest,
            "game_sha256": "1" * 64,
            "certificate_sha256": "2" * 64,
            "policy_sha256": "3" * 64,
            "certificate_side": "environment" if verdict == "UNREALIZABLE" else "system",
            "reduction_semantics": "exact",
            "checker_verdict": "VERIFIED",
        },
        "result": {
            "verdict": verdict,
            "stage": "target_check" if verified else "source_binding",
            "reason": "target_verified" if verified else (
                "eligibility_budget_exhausted" if mode == "eligibility_budget" else "declined"
            ),
        },
    }
    if mode == "missing_input_hash":
        del evidence["source_binding"]["input"]["sha256"]
    elif mode == "missing_artifact_hash":
        del evidence["target_certificate"]["certificate_sha256"]
    elif mode == "missing_target_hash":
        del evidence["target_certificate"]["source_sha256"]
    elif mode == "wrong_hash":
        evidence["target_certificate"]["source_sha256"] = "0" * 64
    path = pathlib.Path(args.evidence_out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(evidence), encoding="utf-8")
if mode in {"win_unreal", "real_route_unreal", "one_sided_unreal"}:
    sys.exit(1)
sys.exit(0 if verified or mode in {"unverified_claim", "lying"} else 2)
'''


FALLBACK_SCRIPT = r'''#!/usr/bin/env python3
import os

group_alive = None
group_path = os.environ.get("FAKE_PGID")
if group_path and os.path.exists(group_path):
    group = int(open(group_path, encoding="utf-8").read())
    try:
        os.killpg(group, 0)
        group_alive = True
    except ProcessLookupError:
        group_alive = False

import json
import pathlib
import sys

record = {
    "argv": sys.argv,
    "stdin": sys.stdin.buffer.read().decode(),
    "deadline": os.environ.get("ACACIA_OUTER_DEADLINE_MONOTONIC"),
    "group_alive": group_alive,
}
pathlib.Path(os.environ["FAKE_B_RECORD"]).write_text(json.dumps(record))
verdict = os.environ.get("FAKE_B_VERDICT", "UNREALIZABLE")
print(verdict)
sys.exit({"REALIZABLE": 0, "UNREALIZABLE": 1, "UNKNOWN": 2}[verdict])
'''


class WrapperTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = pathlib.Path(self.temporary.name)
        self.lift = self._script("lift.py", LIFT_SCRIPT)
        self.fallback = self._script("fallback.py", FALLBACK_SCRIPT)
        self.source = self.directory / "case.tlsf"
        self.source.write_text("INFO { TITLE: fake }\n", encoding="utf-8")
        self.route = self.directory / "route.json"
        self.b_record = self.directory / "b.json"
        self.counter = self.directory / "lift-count"
        self.lift_input = self.directory / "lift-input"

    def tearDown(self):
        self.temporary.cleanup()

    def _script(self, name: str, source: str) -> pathlib.Path:
        path = self.directory / name
        path.write_text(source, encoding="utf-8")
        path.chmod(0o755)
        return path

    def invoke(
        self,
        mode: str,
        *,
        tlsf: str | None = None,
        stdin: bytes = b"",
        deadline: float | None = None,
        lift_seconds: float = 0.4,
    ) -> subprocess.CompletedProcess[bytes]:
        tlsf = str(self.source) if tlsf is None else tlsf
        env = dict(
            os.environ,
            FAKE_LIFT_MODE=mode,
            FAKE_LIFT_COUNTER=str(self.counter),
            FAKE_LIFT_INPUT=str(self.lift_input),
            FAKE_B_RECORD=str(self.b_record),
            FAKE_PGID=str(self.directory / "pgid"),
            ACACIA_ROUTE_RECORD=str(self.route),
            ACACIA_OUTER_DEADLINE_MONOTONIC=repr(
                time.monotonic() + 3 if deadline is None else deadline
            ),
        )
        return subprocess.run(
            [
                sys.executable,
                str(WRAPPER),
                "--lift-entry",
                str(self.lift),
                "--lift-budget-seconds",
                str(lift_seconds),
                "--",
                str(self.fallback),
                "--unchanged",
                "two words",
                "-T",
                tlsf,
            ],
            input=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            check=False,
            timeout=8,
        )

    def test_verified_lift_emits_exact_acacia_verdict_and_exit(self):
        for mode, verdict, exit_code in (
            ("win", b"REALIZABLE\n", 0),
            ("win_unreal", b"UNREALIZABLE\n", 1),
            ("exact_real", b"REALIZABLE\n", 0),
        ):
            with self.subTest(mode=mode):
                result = self.invoke(mode)
                self.assertEqual(result.returncode, exit_code)
                self.assertEqual(result.stdout, verdict)
                self.assertEqual(result.stderr, b"")
                self.assertFalse(self.b_record.exists())
                route = json.loads(self.route.read_text())
                self.assertEqual(route["winner"], "lifting")
                self.assertEqual(route["lift_exit"], exit_code)
                self.assertTrue(route["evidence_sha256"])
                self.assertTrue({
                    "input_sha256",
                    "route",
                    "binding_reason",
                    "lift_argv",
                    "lift_elapsed",
                    "evidence_path",
                    "stage_censoring",
                    "fallback_start",
                    "fallback_remaining",
                } <= route.keys())

    def test_one_sided_evidence_cannot_emit_unrealizable(self):
        for mode in ("real_route_unreal", "one_sided_unreal"):
            with self.subTest(mode=mode):
                result = self.invoke(mode)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"UNREALIZABLE\n")
                self.assertTrue(self.b_record.exists())
                route = json.loads(self.route.read_text())
                self.assertEqual(route["winner"], "fallback-pending")
                self.b_record.unlink()

    def test_missing_input_binding_hash_falls_back(self):
        for mode in (
            "missing_input_hash",
            "missing_artifact_hash",
            "missing_target_hash",
        ):
            with self.subTest(mode=mode):
                result = self.invoke(mode)
                self.assertEqual(result.returncode, 1)
                self.assertTrue(self.b_record.exists())
                self.assertEqual(
                    json.loads(self.route.read_text())["winner"],
                    "fallback-pending",
                )
                self.b_record.unlink()

    def test_wrong_input_binding_hash_falls_back(self):
        result = self.invoke("wrong_hash")
        self.assertEqual(result.returncode, 1)
        self.assertTrue(self.b_record.exists())
        self.assertEqual(json.loads(self.route.read_text())["winner"], "fallback-pending")

    def test_spooled_stdin_hash_mismatch_falls_back(self):
        content = b"the actual spooled input\n"
        result = self.invoke("stdin_mismatch", tlsf="-", stdin=content)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.lift_input.read_bytes(), content)
        self.assertEqual(json.loads(self.b_record.read_text())["stdin"].encode(), content)
        self.assertEqual(json.loads(self.route.read_text())["winner"], "fallback-pending")

    def test_decline_execs_b_with_identical_argv_stdin_and_deadline(self):
        content = b"TLSF stdin is consumed exactly once\n"
        deadline = time.monotonic() + 3
        result = self.invoke("decline", tlsf="/dev/stdin", stdin=content, deadline=deadline)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"UNREALIZABLE\n")
        record = json.loads(self.b_record.read_text())
        self.assertEqual(
            record["argv"],
            [str(self.fallback), "--unchanged", "two words", "-T", "/dev/stdin"],
        )
        self.assertEqual(record["stdin"].encode(), content)
        self.assertEqual(float(record["deadline"]), deadline)
        self.assertEqual(self.lift_input.read_bytes(), content)
        self.assertEqual(self.counter.read_text().splitlines(), ["1"])
        route = json.loads(self.route.read_text())
        self.assertEqual(route["winner"], "fallback-pending")
        self.assertGreater(route["fallback_remaining"], 0)

    def test_lift_timeout_kills_the_process_group_before_b(self):
        result = self.invoke("timeout", lift_seconds=0.05)
        self.assertEqual(result.returncode, 1)
        record = json.loads(self.b_record.read_text())
        self.assertFalse(record["group_alive"])
        route = json.loads(self.route.read_text())
        self.assertTrue(route["lift_timed_out"])
        self.assertTrue(route["stage_censoring"]["wrapper_timeout"])

    def test_closed_pipe_grandchild_is_dead_before_b_first_instruction(self):
        wrapper = load_module(
            f"acacia_lift_portfolio_closed_pipe_{id(self)}", WRAPPER
        )
        grandchild_record = self.directory / "grandchild"
        lift = self._script(
            "closed-pipe-lift.py",
            r'''#!/usr/bin/env python3
import os
import pathlib
import signal
import time

ready_read, ready_write = os.pipe()
child = os.fork()
if child == 0:
    os.close(ready_read)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    os.close(1)
    os.close(2)
    pathlib.Path(os.environ["FAKE_GRANDCHILD_RECORD"]).write_text(
        f"{os.getpid()} {os.getpgrp()}", encoding="utf-8"
    )
    os.write(ready_write, b"1")
    os.close(ready_write)
    time.sleep(30)
    os._exit(0)

os.close(ready_write)
if os.read(ready_read, 1) != b"1":
    raise RuntimeError("grandchild did not become ready")
os.close(ready_read)
''',
        )
        grandchild_pid = None
        libc = ctypes.CDLL(None, use_errno=True)
        original_subreaper = ctypes.c_int()
        self.assertEqual(
            libc.prctl(37, ctypes.byref(original_subreaper), 0, 0, 0),
            0,
            "could not read child-subreaper state",
        )
        restore_subreaper = original_subreaper.value == 0
        if restore_subreaper:
            self.assertEqual(
                libc.prctl(36, 1, 0, 0, 0),
                0,
                "could not become a child subreaper",
            )

        def fake_b_group_alive(group: int) -> bool:
            try:
                os.killpg(group, 0)
            except ProcessLookupError:
                return False
            return True

        try:
            with mock.patch.dict(
                os.environ,
                {"FAKE_GRANDCHILD_RECORD": str(grandchild_record)},
            ):
                outcome = wrapper.run_lift([str(lift)], timeout=1.0)
            grandchild_pid, group = map(int, grandchild_record.read_text().split())
            self.assertEqual(outcome.returncode, 0)
            self.assertFalse(outcome.timed_out)
            self.assertFalse(
                fake_b_group_alive(group),
                "lifting process group was alive when fallback B started",
            )
            self.assertFalse(pathlib.Path(f"/proc/{grandchild_pid}").exists())
        finally:
            if grandchild_pid is None and grandchild_record.exists():
                grandchild_pid = int(grandchild_record.read_text().split()[0])
            if grandchild_pid is not None:
                with contextlib.suppress(ProcessLookupError):
                    os.kill(grandchild_pid, signal.SIGKILL)
                with contextlib.suppress(ChildProcessError):
                    os.waitpid(grandchild_pid, 0)
            if restore_subreaper:
                self.assertEqual(libc.prctl(36, 0, 0, 0, 0), 0)

    def test_claim_without_target_verified_falls_back(self):
        result = self.invoke("unverified_claim")
        self.assertEqual(result.returncode, 1)
        self.assertTrue(self.b_record.exists())
        self.assertEqual(json.loads(self.route.read_text())["winner"], "fallback-pending")

    def test_decisive_exit_without_evidence_falls_back(self):
        result = self.invoke("lying")
        self.assertEqual(result.returncode, 1)
        self.assertTrue(self.b_record.exists())
        self.assertEqual(json.loads(self.route.read_text())["binding_reason"], "evidence_missing")

    def test_eligibility_budget_is_fraction_capped_and_recorded(self):
        result = self.invoke("eligibility_budget")
        self.assertEqual(result.returncode, 1)
        route = json.loads(self.route.read_text())
        self.assertEqual(route["binding_reason"], "eligibility_budget_exhausted")
        self.assertTrue(self.b_record.exists())
        self.assertGreater(route["eligibility_budget_s"], 0)
        self.assertLessEqual(route["eligibility_budget_s"], 0.15)
        index = route["lift_argv"].index("--eligibility-budget-seconds")
        self.assertAlmostEqual(
            float(route["lift_argv"][index + 1]), route["eligibility_budget_s"],
            places=5,
        )

    def test_expired_outer_deadline_skips_lift_and_execs_b(self):
        result = self.invoke("win", deadline=time.monotonic() - 1)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.counter.exists())
        self.assertTrue(self.b_record.exists())
        route = json.loads(self.route.read_text())
        self.assertIsNone(route["lift_exit"])
        self.assertEqual(route["lift_elapsed"], 0.0)

    def test_route_record_replaces_atomically(self):
        wrapper = load_module("acacia_lift_portfolio_for_test", WRAPPER)
        destination = self.directory / "atomic.json"
        destination.write_text("old", encoding="utf-8")
        real_replace = os.replace

        def inspect_then_replace(source, target):
            self.assertEqual(destination.read_text(), "old")
            json.loads(pathlib.Path(source).read_text())
            real_replace(source, target)

        with mock.patch.object(wrapper.os, "replace", side_effect=inspect_then_replace):
            wrapper.atomic_write_json(destination, {"complete": True})
        self.assertEqual(json.loads(destination.read_text()), {"complete": True})
        self.assertEqual(list(self.directory.glob(".atomic.json.*.tmp")), [])

    def test_setup_failure_uses_non_contract_error_exit(self):
        wrapper = load_module("acacia_lift_portfolio_setup_failure", WRAPPER)
        arguments = [
            "--lift-entry",
            str(self.lift),
            "--cap",
            "1",
            "--",
            str(self.fallback),
            "-T",
            str(self.source),
        ]
        stderr = io.StringIO()
        with (
            mock.patch.object(
                wrapper.tempfile,
                "TemporaryDirectory",
                side_effect=OSError("injected setup failure"),
            ),
            contextlib.redirect_stderr(stderr),
        ):
            self.assertEqual(wrapper.main(arguments), wrapper.ERROR_EXIT)
        self.assertIn("injected setup failure", stderr.getvalue())
        self.assertFalse(self.b_record.exists())


class CoverageHookTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = pathlib.Path(self.temporary.name)
        self.coverage = load_module(
            f"coverage_hook_{id(self)}", COVERAGE
        )
        self.binary = self.directory / "solver"
        self.binary.write_text("#!/bin/sh\n", encoding="utf-8")
        self.binary.chmod(0o755)
        (self.directory / "case.tlsf").write_text("//STATUS: realizable\n")
        (self.directory / "list").write_text("case.ltl\n")
        (self.directory / "map").write_text("instance\ttlsf\ncase.ltl\tcase.tlsf\n")
        (self.directory / "exceptions").write_text(
            "instance\tannotated_status\tcorrected_status\tevidence\n"
        )

    def tearDown(self):
        self.temporary.cleanup()

    def arguments(self, *extra: str):
        return self.coverage.build_parser().parse_args([
            "--bin", str(self.binary),
            "--solver-label", "candidate",
            "--list", str(self.directory / "list"),
            "--tlsf-map", str(self.directory / "map"),
            "--tlsf-corpus", str(self.directory),
            "--status-exceptions", str(self.directory / "exceptions"),
            "--caps", "17",
            "--memory-max", "8G",
            "--memory-swap-max", "0",
            "--output", str(self.directory / "out.tsv"),
            "--acacia-sha", "frozen",
            *extra,
        ])

    def solved(self):
        return self.coverage.RunResult("REALIZABLE\n", "", 0, 0.1, False)

    def test_default_launch_argv_env_and_tsv_match_the_old_path(self):
        calls = []

        def scoped(command, **kwargs):
            calls.append((command, kwargs))
            return self.solved()

        with mock.patch.object(self.coverage, "run_systemd_scope", side_effect=scoped):
            self.assertEqual(self.coverage.run(self.arguments()), 0)
        command, kwargs = calls[0]
        self.assertEqual(
            command,
            [str(self.binary.resolve()), "-T", str(self.directory / "case.tlsf")],
        )
        self.assertEqual(
            kwargs,
            {
                "timeout": 17,
                "memory_max": "8G",
                "memory_swap_max": "0",
                "allowed_cpus": None,
                "cpu_quota": None,
                "unit_prefix": "acacia-syntcomp26-coverage",
                "env": None,
            },
        )
        with (self.directory / "out.tsv").open(newline="") as stream:
            self.assertEqual(csv.DictReader(stream, delimiter="\t").fieldnames,
                             self.coverage.OUTPUT_COLUMNS)

    def test_hook_passes_deadline_and_unique_record_through_systemd(self):
        calls = []

        def scoped(command, **kwargs):
            calls.append((command, kwargs))
            record = pathlib.Path(kwargs["scope_env"]["ACACIA_ROUTE_RECORD"])
            record.write_text(json.dumps({
                "winner": "fallback-pending",
                "lift_elapsed": 0.25,
                "fallback_start": 123.5,
            }))
            return self.solved()

        args = self.arguments("--route-records", str(self.directory / "routes"))
        before = time.monotonic()
        with mock.patch.object(self.coverage, "run_systemd_scope", side_effect=scoped):
            self.assertEqual(self.coverage.run(args), 0)
        _, kwargs = calls[0]
        scope_env = kwargs["scope_env"]
        self.assertEqual(kwargs["env"]["ACACIA_ROUTE_RECORD"],
                         scope_env["ACACIA_ROUTE_RECORD"])
        self.assertEqual(kwargs["env"]["ACACIA_OUTER_DEADLINE_MONOTONIC"],
                         scope_env["ACACIA_OUTER_DEADLINE_MONOTONIC"])
        self.assertGreaterEqual(float(scope_env["ACACIA_OUTER_DEADLINE_MONOTONIC"]),
                                before + 16.9)
        route = pathlib.Path(scope_env["ACACIA_ROUTE_RECORD"])
        self.assertTrue(route.is_relative_to((self.directory / "routes").resolve()))
        with (self.directory / "out.tsv").open(newline="") as stream:
            reader = csv.DictReader(stream, delimiter="\t")
            self.assertEqual(reader.fieldnames[-3:], self.coverage.ROUTE_COLUMNS)
            row, = reader
        self.assertEqual(row["winner"], "fallback-pending")
        self.assertEqual(row["lift_elapsed"], "0.25")
        self.assertEqual(row["fallback_start"], "123.5")
        args.resume = True
        with mock.patch.object(
            self.coverage,
            "run_systemd_scope",
            side_effect=AssertionError("a completed route row must not rerun"),
        ):
            self.assertEqual(self.coverage.run(args), 0)
        export = self.directory / "export.csv"
        self.coverage.export_cactus_csv(
            self.directory / "out-summary.tsv",
            self.directory / "out.tsv",
            self.directory / "list",
            17,
            export,
        )
        self.assertEqual(export.read_text().splitlines()[0], "instance,result,seconds,exit")
        self.assertEqual(
            export.with_suffix(".raw.tsv").read_text().splitlines()[0],
            "instance\tresult\texit_code\tseconds\tcap_s",
        )


class SystemdEnvironmentTests(unittest.TestCase):
    def test_explicit_child_environment_becomes_systemd_setenv_options(self):
        benchlib = load_module("benchlib_scope_env_for_test", BENCHMARKING / "benchlib.py")
        seen = {}

        class Captured(RuntimeError):
            pass

        def popen(command, *args, **kwargs):
            seen["command"] = command
            raise Captured

        with (
            mock.patch.object(benchlib.subprocess, "Popen", side_effect=popen),
            mock.patch.object(
                benchlib,
                "user_manager_controllers",
                return_value={"cpuset", "cpu", "memory"},
            ),
            self.assertRaises(Captured),
        ):
            benchlib.run_systemd_scope(
                ["solver"],
                17,
                "8G",
                unit_prefix="acacia-test",
                scope_env={
                    "ACACIA_ROUTE_RECORD": "/tmp/record with space.json",
                    "ACACIA_OUTER_DEADLINE_MONOTONIC": "123.5",
                },
            )
        command = seen["command"]
        record_option = "--setenv=ACACIA_ROUTE_RECORD=/tmp/record with space.json"
        deadline_option = "--setenv=ACACIA_OUTER_DEADLINE_MONOTONIC=123.5"
        self.assertIn(record_option, command)
        self.assertIn(deadline_option, command)
        self.assertLess(command.index(record_option), command.index("solver"))


if __name__ == "__main__":
    unittest.main()
