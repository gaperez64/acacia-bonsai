#!/usr/bin/env python3
"""Standalone regression tests for the index-aware GR(1) generalizer.

The complete suite performs real solver/checker round trips and therefore
takes a few minutes.  Run it directly; no pytest-only fixtures are required.
"""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DRIVER = HERE / "generalize_gr1.py"
CHECKER = ROOT / "subprojects" / "tlsf-tools" / "build-oxidd" / "tlsfcertcheck"
PYTHON = pathlib.Path("/usr/bin/python3.13")
sys.path.insert(0, str(HERE))
import generalize_gr1 as generalizer  # noqa: E402  pylint: disable=wrong-import-position

ROUND_TRIPS = {
    "arbiter": ((3, 4), 10),
    "prioritized_arbiter": ((3, 4), 5),
    "load_balancer": ((2, 3, 4), 5),
    "arbiter_with_cancel": ((2, 3, 4), 5),
    "collector_v1": ((3,), 4),
}


def _run(command: list[str], env: dict[str, str], timeout: float = 600.0):
    return subprocess.run(command, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, check=False)


def _rewrite_outputs(path: pathlib.Path, replacements: dict[int, int]) -> None:
    rows = path.read_text(encoding="utf-8").splitlines()
    header = list(map(int, rows[0].split()[1:]))
    _maximum, inputs, latches, outputs, _ands = header[:5]
    start = 1 + inputs + latches
    for output, literal in replacements.items():
        if not 0 <= output < outputs:
            raise IndexError(output)
        rows[start + output] = str(literal)
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def _shift_first_input(path: pathlib.Path) -> None:
    rows = path.read_text(encoding="utf-8").splitlines()
    header = list(map(int, rows[0].split()[1:]))
    _maximum, inputs, latches, outputs, ands = header[:5]
    if inputs < 2 or latches:
        raise ValueError("expected a combinational certificate with two inputs")
    output_start = 1 + inputs
    gate_start = output_start + outputs

    def shift(literal: int) -> int:
        if literal // 2 != 1:
            return literal
        return 4 | (literal & 1)

    for row in range(output_start, gate_start):
        rows[row] = str(shift(int(rows[row])))
    for row in range(gate_start, gate_start + ands):
        lhs, left, right = map(int, rows[row].split())
        rows[row] = f"{lhs} {shift(left)} {shift(right)}"
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def _swap_inputs(path: pathlib.Path, pairs: list[tuple[int, int]]) -> None:
    rows = path.read_text(encoding="utf-8").splitlines()
    header = list(map(int, rows[0].split()[1:]))
    _maximum, inputs, latches, outputs, ands = header[:5]
    if latches:
        raise ValueError("expected a combinational certificate")
    variables = {}
    for left, right in pairs:
        if not 0 <= left < inputs or not 0 <= right < inputs:
            raise IndexError((left, right))
        variables[left + 1] = right + 1
        variables[right + 1] = left + 1

    def swap(literal: int) -> int:
        return 2 * variables.get(literal // 2, literal // 2) | (literal & 1)

    output_start = 1 + inputs
    gate_start = output_start + outputs
    for row in range(output_start, gate_start):
        rows[row] = str(swap(int(rows[row])))
    for row in range(gate_start, gate_start + ands):
        lhs, left, right = map(int, rows[row].split())
        rows[row] = f"{lhs} {swap(left)} {swap(right)}"
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


class GeneralizeGr1Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._temporary = tempfile.TemporaryDirectory(prefix="generalize-gr1-test-")
        cls.root = pathlib.Path(cls._temporary.name)
        cls.env = dict(os.environ)
        cls.env["GENERALIZE_GR1_RESULTS"] = str(cls.root / "m4-results.tsv")
        cls.artifacts: dict[str, pathlib.Path] = {}
        cls.runs: dict[str, subprocess.CompletedProcess[str]] = {}
        for family, (seeds, target) in ROUND_TRIPS.items():
            out = cls.root / f"{family}-{target}"
            proc = _run([
                str(PYTHON), str(DRIVER), "--family", family,
                "--target", str(target), "--seeds", ",".join(map(str, seeds)),
                "--timeout", "180", "--check-method", "both",
                "--out", str(out),
            ], cls.env)
            cls.artifacts[family] = out
            cls.runs[family] = proc

    @classmethod
    def tearDownClass(cls) -> None:
        cls._temporary.cleanup()

    def test_round_trip_larger_than_seeds(self) -> None:
        for family, (seeds, target) in ROUND_TRIPS.items():
            with self.subTest(family=family):
                proc = self.runs[family]
                self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
                self.assertIn("VERIFIED", proc.stdout, proc.stderr + proc.stdout)
                self.assertGreater(target, max(seeds))
                check = json.loads((self.artifacts[family] /
                                    f"check-target-{target}.json").read_text(
                                        encoding="utf-8"))
                self.assertEqual(check["requested_method"], "both")
                self.assertEqual(check["verdict"], "VERIFIED", check)

    def test_out_of_scope_declines_name_arity_measurement(self) -> None:
        for family in ("round_robin_arbiter", "lift",
                       "amba_decomposed_arbiter"):
            with self.subTest(family=family):
                proc = _run([
                    str(PYTHON), str(DRIVER), "--family", family,
                    "--target", "6", "--out", str(self.root / f"decline-{family}"),
                ], self.env, 30)
                self.assertEqual(proc.returncode, 3, proc.stderr + proc.stdout)
                self.assertIn("UNKNOWN", proc.stdout)
                self.assertIn("arity measurement", proc.stdout)
                self.assertNotIn("VERIFIED", proc.stdout)

    def test_degenerate_seeds_are_refused(self) -> None:
        cases = (("arbiter", 3, 1),)
        for family, target, seed in cases:
            with self.subTest(family=family):
                proc = _run([
                    str(PYTHON), str(DRIVER), "--family", family,
                    "--target", str(target), "--seeds", str(seed),
                    "--out", str(self.root / f"degenerate-{family}"),
                ], self.env, 30)
                self.assertEqual(proc.returncode, 3, proc.stderr + proc.stdout)
                self.assertIn("UNKNOWN", proc.stdout)
                self.assertIn("stable_from", proc.stdout)
                self.assertNotIn("VERIFIED", proc.stdout)

    def test_unreal_direct_path_checks_environment_certificate(self) -> None:
        out = self.root / "round-robin-unreal2-3"
        proc = _run([
            str(PYTHON), str(DRIVER), "--family", "round_robin_arbiter_unreal2",
            "--target", "3", "--seeds", "", "--timeout", "120",
            "--out", str(out),
        ], self.env, 150)
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        self.assertIn("VERIFIED", proc.stdout)
        certificate = out / "round_robin_arbiter_unreal2_3.certificate.aag.json"
        metadata = json.loads(certificate.read_text(encoding="utf-8"))
        self.assertEqual(metadata["status"], "unrealizable")
        self.assertEqual(metadata["side"], "environment")
        self.assertEqual(metadata["reduction_semantics"], "exact")
        check = json.loads((out / "check-target-3.json").read_text(encoding="utf-8"))
        self.assertEqual(check["verdict"], "VERIFIED")

    def _corrupt_check(self, family: str, mutate) -> subprocess.CompletedProcess[str]:
        _seeds, target = ROUND_TRIPS[family]
        source = self.artifacts[family]
        corrupt = self.root / f"corrupt-{family}-{mutate.__name__}"
        corrupt.mkdir()
        stem = f"{family}_{target}"
        for suffix in (".certificate.aag", ".certificate.aag.json",
                       ".policy.aag", ".policy.aag.json", ".game.aag",
                       ".prov.json"):
            shutil.copy2(source / f"{stem}{suffix}", corrupt / f"{stem}{suffix}")
        mutate(corrupt / f"{stem}.certificate.aag",
               corrupt / f"{stem}.certificate.aag.json",
               corrupt / f"{stem}.prov.json")
        return _run([
            str(CHECKER), "--method", "certificate", "--timeout", "120",
            "--json-out", str(corrupt / "check.json"),
            "--certificate", str(corrupt / f"{stem}.certificate.aag"),
            "--certificate-json", str(corrupt / f"{stem}.certificate.aag.json"),
            str(corrupt / f"{stem}.game.aag"),
            str(corrupt / f"{stem}.policy.aag"),
        ], self.env, 150)

    def test_corrupted_generalizations_never_verify(self) -> None:
        def drop_conjunct(aag: pathlib.Path, _sidecar: pathlib.Path,
                          _prov: pathlib.Path) -> None:
            _rewrite_outputs(aag, {0: 1})

        def shift_index(aag: pathlib.Path, _sidecar: pathlib.Path,
                        _prov: pathlib.Path) -> None:
            _shift_first_input(aag)

        def swap_role_classes(_aag: pathlib.Path, sidecar_path: pathlib.Path,
                              prov_path: pathlib.Path) -> None:
            sidecar = json.loads(sidecar_path.read_text(encoding="utf-8"))
            prov = json.loads(prov_path.read_text(encoding="utf-8"))
            monitors = prov["monitors"]
            owner_one_records = [
                item for item in monitors
                if item["arity_kind"] == "local" and item["index_tuple"] == [1]
            ]
            owner_one_templates = {item["template"] for item in owner_one_records}
            # Client zero is load_balancer's measured special role.  Swap its
            # role-defining monitor with an ordinary-client monitor, rather
            # than swapping a symmetric template shared by both clients.
            owner_zero = next(
                item for item in monitors
                if item["arity_kind"] == "local" and item["index_tuple"] == [0]
                and item["template"] not in owner_one_templates)
            owner_one = owner_one_records[0]
            state = sidecar["variables"]["state"]
            pairs = []
            for bit in range(min(owner_zero["state_count"],
                                 owner_one["state_count"])):
                left_name = f"monitor_{owner_zero['monitor']}_state_{bit}"
                right_name = f"monitor_{owner_one['monitor']}_state_{bit}"
                left = next(item for item in state if item["name"] == left_name)
                right = next(item for item in state if item["name"] == right_name)
                pairs.append((left["certificate_input"],
                              right["certificate_input"]))
            _swap_inputs(_aag, pairs)

        cases = (("arbiter", drop_conjunct),
                 ("arbiter", shift_index),
                 ("load_balancer", swap_role_classes))
        for family, mutation in cases:
            with self.subTest(mutation=mutation.__name__):
                proc = self._corrupt_check(family, mutation)
                self.assertNotEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                self.assertNotIn("\nVERIFIED\n", "\n" + proc.stdout + "\n")

    def test_deterministic_outputs(self) -> None:
        outputs = []
        for run in range(2):
            out = self.root / f"determinism-{run}"
            proc = _run([
                str(PYTHON), str(DRIVER), "--family", "arbiter",
                "--target", "5", "--seeds", "3,4", "--timeout", "120",
                "--check-method", "both", "--out", str(out),
            ], self.env, 180)
            self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
            self.assertIn("VERIFIED", proc.stdout)
            outputs.append(out)
        names = ("arbiter_5.certificate.aag", "arbiter_5.certificate.aag.json",
                 "arbiter_5.policy.aag", "arbiter_5.policy.aag.json",
                 "evidence.json")
        for name in names:
            with self.subTest(file=name):
                self.assertEqual((outputs[0] / name).read_bytes(),
                                 (outputs[1] / name).read_bytes())

class GeneralizerUnitTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory(prefix="generalize-gr1-unit-")
        self.root = pathlib.Path(self._temporary.name)

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def test_per_predicate_arity_fallback_is_independent(self) -> None:
        seeds = [SimpleNamespace(n=4), SimpleNamespace(n=5)]

        def projection(_bdds, seed, name, arity, _goal=None):
            needed = 1 if name == "inv" else 2
            if arity < needed:
                raise generalizer.Decline(
                    "anti-unify", name, seed.n,
                    f"declared arity k={arity} does not reconstruct predicate")
            return {("role",): f"{name}-k{arity}-n{seed.n}"}

        with mock.patch.object(generalizer, "projection_templates",
                               side_effect=projection):
            _inv, inv_arity = generalizer._predicate_templates(
                None, seeds, "inv", 1)
            harder, harder_arity = generalizer._predicate_templates(
                None, seeds, "x_0_0_0", 1)
        self.assertEqual(inv_arity, 1)
        self.assertEqual(harder_arity, 2)
        self.assertEqual(harder[("role",)], "x_0_0_0-k2-n5")

    def test_full_bus_at_n2_pair_is_routed_by_actual_arity(self) -> None:
        pair = {
            "arity_kind": "bus_wide", "index_tuple": [0, 1],
            "support": {"buses": {"g": [0, 1], "r": [0, 1]}},
            "template": ("(r_i0 & r_i1) | (((g_i0 & r_i0) | "
                         "(!g_i0 & !r_i0)) & X(true))"),
        }
        self.assertEqual(generalizer._route_arity_kind(pair, 2), "local")
        symmetric_bus = dict(pair, symmetric=True)
        self.assertEqual(generalizer._route_arity_kind(symmetric_bus, 2),
                         "bus_wide")

    def test_checker_resource_retry_preserves_cert_failed(self) -> None:
        target = SimpleNamespace(n=5, game_path=self.root / "game.aag")
        cert = self.root / "candidate.aag"
        policy = self.root / "policy.aag"
        limits = generalizer.ProposerLimits(checker_nodes=1024)
        responses = [
            subprocess.CompletedProcess([], 3, "", "capacity"),
            subprocess.CompletedProcess([], 6, "", "proof failed"),
        ]
        with mock.patch.object(generalizer, "_run", side_effect=responses):
            result = generalizer.check_candidate(
                target, cert, policy, "auto", limits, "retry")
        self.assertEqual(result["verdict"], "CERT_FAILED")
        self.assertEqual(result["node_caps"], [1024, 2048])

    def test_capacity_defaults_scale_and_explicit_values_win(self) -> None:
        defaults = generalizer.ProposerLimits()
        self.assertEqual(defaults.seed_capacity(4), (1 << 25, 1 << 23))
        self.assertEqual(defaults.seed_capacity(5), (1 << 26, 1 << 24))
        self.assertEqual(defaults.check_capacity(5), 1 << 24)
        self.assertEqual(defaults.check_capacity(6), 1 << 25)

        explicit = generalizer.ProposerLimits(
            solver_nodes=12345, solver_cache=2345, checker_nodes=3456)
        self.assertEqual(explicit.seed_capacity(30), (12345, 2345))
        self.assertEqual(explicit.check_capacity(30), 3456)

    def test_target_game_timeout_keeps_canonicalize_stage(self) -> None:
        with mock.patch.object(
                generalizer.subprocess, "run",
                side_effect=subprocess.TimeoutExpired(["monitor"], 1)):
            with self.assertRaises(generalizer.Decline) as raised:
                generalizer.build_game(
                    "arbiter", 30, self.root, 1, stage="canonicalize")
        self.assertEqual(raised.exception.stage, "canonicalize")
        self.assertEqual(raised.exception.predicate, "monitor_game")


if __name__ == "__main__":
    unittest.main(verbosity=2)
