#!/usr/bin/env python3
"""Focused S0 diagnostic regressions; no build step is required."""

from __future__ import annotations

import contextlib
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
import types
import unittest
from typing import Any
from unittest import mock

import generalize_gr1
from s0_diagnostics import Diagnostics


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DRIVER = HERE / "generalize_gr1.py"
SCHEMA = ROOT / "benchmarking/gr1-par2-20260923/s0/diagnostics.schema.json"
BUILD = ROOT / "subprojects/tlsf-tools/build-L0"
BINDINGS_PYTHON = pathlib.Path("/usr/bin/python3.13")
BINDINGS_SITE = pathlib.Path("/usr/local/lib64/python3.13/site-packages")

CAMPAIGN_SPEC = importlib.util.spec_from_file_location(
    "s0_test_param_lift_campaign", HERE / "param-lift-campaign.py")
assert CAMPAIGN_SPEC is not None and CAMPAIGN_SPEC.loader is not None
campaign = importlib.util.module_from_spec(CAMPAIGN_SPEC)
sys.modules[CAMPAIGN_SPEC.name] = campaign
CAMPAIGN_SPEC.loader.exec_module(campaign)


def _json_type(value: Any, expected: str) -> bool:
    types = {
        "object": lambda item: isinstance(item, dict),
        "array": lambda item: isinstance(item, list),
        "string": lambda item: isinstance(item, str),
        "number": lambda item: isinstance(item, (int, float))
        and not isinstance(item, bool),
        "integer": lambda item: isinstance(item, int) and not isinstance(item, bool),
        "boolean": lambda item: isinstance(item, bool),
        "null": lambda item: item is None,
    }
    return types[expected](value)


def _validate(value: Any, schema: dict[str, Any], root: dict[str, Any],
              location: str = "$") -> None:
    if "$ref" in schema:
        target: Any = root
        for part in schema["$ref"].removeprefix("#/").split("/"):
            target = target[part]
        _validate(value, target, root, location)
        return
    if "const" in schema:
        if value != schema["const"]:
            raise AssertionError(f"{location}: expected {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise AssertionError(f"{location}: {value!r} is outside enum")
    expected = schema.get("type")
    if expected is not None:
        choices = expected if isinstance(expected, list) else [expected]
        if not any(_json_type(value, choice) for choice in choices):
            raise AssertionError(f"{location}: expected type {choices}, got {value!r}")
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            raise AssertionError(f"{location}: value is below minimum")
    if isinstance(value, dict):
        for key in schema.get("required", []):
            if key not in value:
                raise AssertionError(f"{location}: missing required property {key}")
        properties = schema.get("properties", {})
        additional = schema.get("additionalProperties", True)
        for key, item in value.items():
            if key in properties:
                _validate(item, properties[key], root, f"{location}.{key}")
            elif isinstance(additional, dict):
                _validate(item, additional, root, f"{location}.{key}")
            elif additional is False:
                raise AssertionError(f"{location}: unexpected property {key}")
    if isinstance(value, list) and "items" in schema:
        for index, item in enumerate(value):
            _validate(item, schema["items"], root, f"{location}[{index}]")


class S0DiagnosticsTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory(prefix="gr1-s0-test-")
        self.root = pathlib.Path(self._temporary.name)
        self.env = dict(os.environ)
        self.env["GENERALIZE_GR1_RESULTS"] = str(self.root / "results.tsv")

    def tearDown(self) -> None:
        generalize_gr1._DIAGNOSTICS_ENABLED = False
        generalize_gr1._DIAGNOSTICS = None
        campaign._DIAGNOSTICS_ENABLED = False
        campaign._DIAGNOSTICS = None
        self._temporary.cleanup()

    def _local_diagnostics(self, path: pathlib.Path) -> Diagnostics:
        diagnostics = Diagnostics("generalize_gr1", path)
        generalize_gr1._DIAGNOSTICS_ENABLED = True
        generalize_gr1._DIAGNOSTICS = diagnostics
        generalize_gr1._DIAGNOSTIC_INSTANCES.clear()
        generalize_gr1._DIAGNOSTIC_CLIENT_COUNTS.clear()
        generalize_gr1._DIAGNOSTIC_OWNER_ARITIES.clear()
        generalize_gr1._DIAGNOSTIC_SUBSETS.clear()
        generalize_gr1._DIAGNOSTIC_SUPPORTS.clear()
        generalize_gr1._DIAGNOSTIC_AAG_CONES.clear()
        generalize_gr1._DIAGNOSTIC_MASK_WORDS.clear()
        generalize_gr1._DIAGNOSTIC_MODES.clear()
        return diagnostics

    def _command(self, output: pathlib.Path) -> list[str]:
        return [
            str(BINDINGS_PYTHON), str(DRIVER),
            "--family", "prioritized_arbiter", "--target", "5",
            "--seeds", "3,4", "--timeout", "120", "--out", str(output),
            "--tlsf-tools-build", str(BUILD),
            "--bindings-python", str(BINDINGS_PYTHON),
            "--bindings-site", str(BINDINGS_SITE),
        ]

    def test_diagnostics_preserve_generated_artifacts_and_validate(self) -> None:
        off = self.root / "off"
        on = self.root / "on"
        diagnostic = self.root / "tiny-diagnostic.json"
        first = subprocess.run(
            self._command(off), cwd=ROOT, env=self.env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False, timeout=150,
        )
        second = subprocess.run(
            [*self._command(on), "--diagnostics", str(diagnostic)],
            cwd=ROOT, env=self.env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False, timeout=150,
        )
        self.assertEqual(first.returncode, 0, first.stderr + first.stdout)
        self.assertEqual(second.returncode, 0, second.stderr + second.stdout)
        artifacts = (
            "prioritized_arbiter_5.game.aag",
            "prioritized_arbiter_5.prov.json",
            "prioritized_arbiter_5.certificate.aag",
            "prioritized_arbiter_5.certificate.aag.json",
            "prioritized_arbiter_5.policy.aag",
            "prioritized_arbiter_5.policy.aag.json",
        )
        for name in artifacts:
            with self.subTest(artifact=name):
                self.assertEqual((off / name).read_bytes(), (on / name).read_bytes())

        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        payload = json.loads(diagnostic.read_text(encoding="utf-8"))
        _validate(payload, schema, schema)
        self.assertGreater(payload["phases"]["from_aag"]["calls"], 0)
        self.assertGreater(payload["counters"]["bdd_to_aig_walks"], 0)

    def test_cancelled_stage_is_censored(self) -> None:
        output = self.root / "cancelled"
        diagnostic = self.root / "cancelled.json"
        progress = self.root / "progress.json"
        env = dict(self.env)
        env["GENERALIZE_GR1_PROGRESS"] = str(progress)
        command = [
            str(BINDINGS_PYTHON), str(DRIVER),
            "--family", "collector_v1", "--target", "11",
            "--seeds", "3", "--timeout", "120", "--out", str(output),
            "--diagnostics", str(diagnostic),
            "--tlsf-tools-build", str(BUILD),
            "--bindings-python", str(BINDINGS_PYTHON),
            "--bindings-site", str(BINDINGS_SITE),
        ]
        proc = subprocess.Popen(
            command, cwd=ROOT, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            start_new_session=True,
        )
        deadline = time.monotonic() + 10
        while not progress.is_file() and proc.poll() is None:
            if time.monotonic() >= deadline:
                os.killpg(proc.pid, signal.SIGKILL)
                self.fail("generalizer did not publish progress before cancellation")
            time.sleep(0.01)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
        stdout, stderr = proc.communicate(timeout=10)
        self.assertTrue(diagnostic.is_file(), stderr + stdout)
        payload = json.loads(diagnostic.read_text(encoding="utf-8"))
        self.assertEqual(payload["status"], "cancelled")
        self.assertTrue(payload["censored"], payload)
        self.assertTrue(all(item["elapsed_lower_bound_s"] >= 0
                            for item in payload["censored"]))

    def test_new_checker_stats_stream_becomes_structured_json(self) -> None:
        payload = generalize_gr1._parse_stream_json(
            "TLSFCERTCHECK_STATS aig_gates_visited=12 "
            "setup_seconds=0.25 final_status=VERIFIED\n")
        self.assertEqual(payload, {
            "aig_gates_visited": 12,
            "setup_seconds": 0.25,
            "final_status": "VERIFIED",
        })

    def test_unreal_fixture_counts_environment_checker_modes_only(self) -> None:
        output = self.root / "unreal-modes.json"
        self._local_diagnostics(output)
        seed = types.SimpleNamespace(
            family="load_balancer_unreal2",
            n=5,
            game_path=self.root / "load_balancer_unreal2_5.game.aag",
            variables=[],
            goals=[object()] * 29,
            fairness=3,
        )
        target = types.SimpleNamespace(
            family="load_balancer_unreal2",
            n=6,
            game_path=self.root / "load_balancer_unreal2_6.game.aag",
            variables=[],
            goals=[object()] * 34,
            fairness=3,
        )
        # Loading the seed records structural distributions, but must not claim
        # a checker attempt. The environment check has all-zero plus one
        # mode per fairness assumption: max(3, 1) + 1 = 4, not 34 + 1.
        generalize_gr1._diagnostic_register_instance(seed, "seed")
        mode_count = generalize_gr1._diagnostic_record_checker_attempt(
            target, "environment", "target-6", 1 << 26)
        generalize_gr1._diagnostic_finish("completed", {
            "family": target.family,
            "target": target.n,
            "verdict": "VERIFIED",
            "answer": "UNREALIZABLE",
        })
        payload = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(mode_count, 4)
        self.assertEqual(
            payload["distributions"]["mode_count_basis"],
            "actual_checker_attempts",
        )
        self.assertEqual(
            payload["distributions"]["mode_count_histogram"], {"4": 1})
        self.assertNotIn("mode_count", payload["instances"][0])

    def test_late_timeout_uses_active_stage_elapsed_lower_bound(self) -> None:
        diagnostics = Diagnostics("param-lift-campaign", self.root / "timeout.json")
        campaign._DIAGNOSTICS_ENABLED = True
        campaign._DIAGNOSTICS = diagnostics
        progress_path = self.root / "late-progress.json"
        with mock.patch.dict(
            os.environ,
            {"GENERALIZE_GR1_PROGRESS": str(progress_path)},
            clear=False,
        ):
            generalize_gr1._set_active_progress_stage(
                "target_check", time.monotonic() - 0.2)
        progress = json.loads(progress_path.read_text(encoding="utf-8"))
        generalize_gr1._ACTIVE_STAGE = None
        evidence = {"elapsed_s": 119.5, "generalizer_progress": progress}
        campaign._diagnostic_censor_generalizer_timeout(evidence)
        self.assertEqual(len(diagnostics.censored), 1)
        record = diagnostics.censored[0]
        self.assertEqual(record["stage"], "target_check")
        self.assertGreaterEqual(record["elapsed_lower_bound_s"], 0.19)
        self.assertLess(record["elapsed_lower_bound_s"], 2.0)

    def test_stale_progress_sample_uses_supervisor_observation_time(self) -> None:
        now = time.monotonic()
        evidence = {
            "generalizer_progress": {
                "active_stage": "policy_construction_skolemization",
                "active_stage_started_monotonic_s": now - 12.0,
                "sampled_monotonic_s": now - 11.99,
                "active_stage_elapsed_s": 0.01,
            }
        }
        lower_bound = campaign._active_stage_elapsed_lower_bound(evidence)
        self.assertGreaterEqual(lower_bound, 11.9)
        self.assertLess(lower_bound, 13.0)

    def test_unwritable_diagnostics_path_preserves_verdict_and_exit(self) -> None:
        result = {
            "family": "arbiter",
            "target": 6,
            "seeds": (3, 4),
            "verdict": "VERIFIED",
            "reason": "",
        }
        common = [
            "--family", "arbiter", "--target", "6", "--seeds", "3,4",
            "--out", str(self.root / "out"),
            "--tlsf-tools-build", str(self.root / "build-L0"),
            "--bindings-python", str(pathlib.Path(sys.executable).resolve()),
            "--bindings-site", str(self.root / "site"),
        ]
        stdout = io.StringIO()
        with (
            mock.patch.object(
                generalize_gr1, "_launch_candidate_builder",
                return_value=(mock.sentinel.bundle, {}),
            ),
            mock.patch.object(generalize_gr1, "run", return_value=result),
            mock.patch.object(
                generalize_gr1, "_probe_checker_stats", return_value=None),
            contextlib.redirect_stdout(stdout),
        ):
            without_diagnostics = generalize_gr1.main(common)
            generalize_gr1._DIAGNOSTICS_ENABLED = False
            generalize_gr1._DIAGNOSTICS = None
            with_diagnostics = generalize_gr1.main([
                *common,
                "--diagnostics", "/proc/acacia-s0-unwritable/diagnostic.json",
            ])
        self.assertEqual(without_diagnostics, 0)
        self.assertEqual(with_diagnostics, without_diagnostics)
        self.assertEqual(stdout.getvalue().count("VERIFIED family=arbiter"), 2)
        self.assertIsNotNone(generalize_gr1._DIAGNOSTICS)
        self.assertTrue(any(
            item["operation"].startswith("write")
            for item in generalize_gr1._DIAGNOSTICS.errors
        ))

        campaign_common = [
            "--probe",
            "--tlsf-tools-build", str(self.root / "build-L0"),
            "--bindings-python", str(pathlib.Path(sys.executable).resolve()),
            "--bindings-site", str(self.root / "site"),
        ]
        with mock.patch.object(campaign, "print_probe", return_value=0):
            campaign_without = campaign.main(campaign_common)
            campaign_with = campaign.main([
                *campaign_common,
                "--diagnostics", "/proc/acacia-s0-unwritable/campaign.json",
            ])
        self.assertEqual(campaign_without, 0)
        self.assertEqual(campaign_with, campaign_without)
        self.assertIsNotNone(campaign._DIAGNOSTICS)
        self.assertTrue(any(
            item["operation"].startswith("write")
            for item in campaign._DIAGNOSTICS.errors
        ))


if __name__ == "__main__":
    unittest.main(verbosity=2)
