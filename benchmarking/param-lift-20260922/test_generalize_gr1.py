#!/usr/bin/env python3
"""Standalone regression tests for the index-aware GR(1) generalizer.

The complete suite performs real solver/checker round trips and therefore
takes a few minutes.  Run it directly; no pytest-only fixtures are required.
"""

from __future__ import annotations

import hashlib
import json
import itertools
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest import mock


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DRIVER = HERE / "generalize_gr1.py"
CAMPAIGN = HERE / "param-lift-campaign.py"
ADAPTER_BUILD = HERE / "native" / "build.py"
TLSF_TOOLS_BUILD = pathlib.Path(os.environ.get(
    "ACACIA_TLSF_TOOLS_BUILD",
    ROOT / "subprojects" / "tlsf-tools" / "build-oxidd",
))
CHECKER = TLSF_TOOLS_BUILD / "tlsfcertcheck"
PYTHON = pathlib.Path(os.environ.get("ACACIA_BINDINGS_PYTHON", "/usr/bin/python3.13"))
sys.path.insert(0, str(HERE))
import buddy_veccompose as adapter_module  # noqa: E402
import generalize_gr1 as generalizer  # noqa: E402  pylint: disable=wrong-import-position

ROUND_TRIPS = {
    "arbiter": ((3, 4), 10),
    "prioritized_arbiter": ((3, 4), 5),
    "load_balancer": ((2, 3, 4), 5),
    "arbiter_with_cancel": ((2, 3, 4), 5),
    "collector_v1": ((3,), 4),
}
REGION_ROUND_TRIPS = {
    **ROUND_TRIPS,
    # n=10 is a useful performance target, but region checking its eleven
    # scheduler modes exceeds this correctness suite's 180-second cap.  The
    # existing deterministic-output test already makes n=5 a suite fixture.
    "arbiter": ((3, 4), 5),
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


def _output_symbols(path: pathlib.Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for row in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"o(\d+) (.+)", row)
        if match is not None:
            symbols[match.group(2)] = int(match.group(1))
    return symbols


class GeneralizeGr1Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._temporary = tempfile.TemporaryDirectory(prefix="generalize-gr1-test-")
        cls.root = pathlib.Path(cls._temporary.name)
        cls.env = dict(os.environ)
        cls.env["GENERALIZE_GR1_RESULTS"] = str(cls.root / "m4-results.tsv")
        cls.artifacts: dict[str, pathlib.Path] = {}
        cls.runs: dict[str, subprocess.CompletedProcess[str]] = {}
        cls.region_artifacts: dict[str, pathlib.Path] = {}
        cls.region_runs: dict[str, subprocess.CompletedProcess[str]] = {}
        help_result = _run([str(CHECKER), "--help"], cls.env, 10)
        cls.region_supported = "|region" in (help_result.stdout + help_result.stderr)
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
            if cls.region_supported:
                region_seeds, region_target = REGION_ROUND_TRIPS[family]
                region_out = cls.root / f"region-{family}-{region_target}"
                command = [
                    str(PYTHON), str(DRIVER), "--family", family,
                    "--target", str(region_target),
                    "--seeds", ",".join(map(str, region_seeds)),
                    "--timeout", "180", "--real-check", "region",
                    "--out", str(region_out),
                ]
                if family == "arbiter":
                    command.extend((
                        "--diagnostics", str(cls.root / "region-diagnostics.json")
                    ))
                region_proc = _run(command, cls.env)
                cls.region_artifacts[family] = region_out
                cls.region_runs[family] = region_proc

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
                evidence = json.loads((self.artifacts[family] /
                                       "evidence.json").read_text(
                                           encoding="utf-8"))
                lifetime = evidence["manager_lifetime"]
                self.assertFalse(lifetime["supervisor_holds_bdd_manager"])
                self.assertTrue(lifetime["builder_exited_before_checker"])
                self.assertEqual(
                    lifetime["handoff"],
                    "validated_paths_hashes_and_schema_bundle",
                )
                self.assertTrue(evidence["target_verified"])
                self.assertFalse(evidence["schema_validated_on_probes"])
                self.assertEqual(evidence["claim_scope"], "requested_target_only")

    def test_region_round_trip_omits_only_target_policy(self) -> None:
        if not self.region_supported:
            self.skipTest("configured checker has no region method")
        for family, (seeds, target) in REGION_ROUND_TRIPS.items():
            with self.subTest(family=family):
                proc = self.region_runs[family]
                self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
                self.assertIn("VERIFIED", proc.stdout)
                self.assertNotIn("\npolicy:", "\n" + proc.stdout)
                out = self.region_artifacts[family]
                stem = f"{family}_{target}"
                self.assertTrue((out / f"{stem}.certificate.aag").is_file())
                self.assertFalse((out / f"{stem}.policy.aag").exists())
                check = json.loads(
                    (out / f"check-target-{target}.json").read_text(
                        encoding="utf-8"))
                self.assertEqual(check["format"], "tlsf-gr1-region-checkresult-v1")
                self.assertEqual(check["method"], "gr1-region-v1")
                self.assertEqual(check["verdict"], "REGION_VERIFIED")
                evidence = json.loads(
                    (out / "evidence.json").read_text(encoding="utf-8"))
                self.assertEqual(evidence["real_check"], "region")
                self.assertEqual(evidence["target_check_method"], "gr1-region-v1")
                self.assertEqual(evidence["target_check_result"], "REGION_VERIFIED")
                self.assertEqual(
                    evidence["stage_evidence"]["instantiate"]["exports"],
                    ["certificate"],
                )
                bundle = json.loads(
                    (out / "candidate-bundle.json").read_text(encoding="utf-8"))
                self.assertEqual(bundle["request_identity"]["real_check"], "region")
                self.assertNotIn("policy", bundle["artifacts"])
                self.assertNotIn("policy_metadata", bundle["artifacts"])

    def test_region_diagnostics_never_invoke_target_skolemization(self) -> None:
        if not self.region_supported:
            self.skipTest("configured checker has no region method")
        diagnostics = json.loads(
            (self.root / "region-diagnostics.json").read_text(encoding="utf-8"))
        phase = diagnostics["phases"].get(
            "policy_construction_skolemization", {"calls": 0}
        )
        self.assertEqual(phase["calls"], 0)

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
            "--real-check", "region",
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
        self.assertEqual(check["requested_method"], "certificate")
        self.assertTrue(
            (out / "round_robin_arbiter_unreal2_3.policy.aag").is_file())

    def test_campaign_exact_unreal_does_not_require_region_checker(self) -> None:
        checker = self.root / "tlsfcertcheck-without-region-contract"
        checker.write_text(
            """#!/usr/bin/env python3
import os
import sys

if "--help" in sys.argv[1:]:
    print("--method NAME  auto|certificate|closed-loop|both")
    raise SystemExit(0)
real = os.environ["REAL_TLSFCERTCHECK"]
os.execv(real, [real, *sys.argv[1:]])
""",
            encoding="utf-8",
        )
        checker.chmod(0o755)
        environment = dict(self.env)
        environment["REAL_TLSFCERTCHECK"] = str(CHECKER)
        help_result = _run([str(checker), "--help"], environment, 10)
        self.assertNotIn("region", help_result.stdout + help_result.stderr)

        evidence = self.root / "campaign-exact-no-region-evidence.json"
        proc = _run([
            str(PYTHON), str(CAMPAIGN),
            "--family", "round_robin_arbiter_unreal2",
            "--target", "2", "--seeds", "",
            "--real-check", "region", "--budget", "120",
            "--output-dir", str(self.root / "campaign-exact-no-region"),
            "--evidence-out", str(evidence),
            "--tlsf-tools-build", str(TLSF_TOOLS_BUILD),
            "--checker", str(checker),
        ], environment, 150)
        self.assertEqual(proc.returncode, 1, proc.stderr + proc.stdout)
        self.assertIn("UNREALIZABLE", proc.stdout)
        payload = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(
            payload["request"]["route_kind"], "exact-game-both-sides"
        )
        self.assertEqual(payload["result"]["proof_method"], "policy")
        self.assertNotEqual(
            payload["result"]["reason"], "checker_missing_method_region"
        )

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

    def test_region_mutations_never_establish_realizability(self) -> None:
        if not self.region_supported:
            self.skipTest("configured checker has no region method")
        family = "arbiter"
        _seeds, target = REGION_ROUND_TRIPS[family]
        source = self.region_artifacts[family]
        stem = f"{family}_{target}"

        def corrupt_region(path: pathlib.Path) -> None:
            symbols = _output_symbols(path)
            _rewrite_outputs(path, {symbols["inv"]: 0})

        def drop_rank_layer(path: pathlib.Path) -> None:
            symbols = _output_symbols(path)
            rank_names = [name for name in symbols if name.startswith("x_")]
            self.assertTrue(rank_names)
            last_level = max(int(name.split("_")[2]) for name in rank_names)
            dropped = {
                index: 0 for name, index in symbols.items()
                if (name.startswith("x_") or name.startswith("y_"))
                and int(name.split("_")[2]) == last_level
            }
            self.assertTrue(dropped)
            _rewrite_outputs(path, dropped)

        for mutation in (drop_rank_layer, corrupt_region):
            with self.subTest(mutation=mutation.__name__):
                corrupt = self.root / f"region-mutation-{mutation.__name__}"
                corrupt.mkdir()
                for suffix in (
                    ".certificate.aag", ".certificate.aag.json", ".game.aag"
                ):
                    shutil.copy2(
                        source / f"{stem}{suffix}", corrupt / f"{stem}{suffix}")
                certificate = corrupt / f"{stem}.certificate.aag"
                mutation(certificate)
                checked = _run([
                    str(CHECKER), "--method", "region", "--timeout", "120",
                    "--certificate", str(certificate),
                    "--certificate-json", str(certificate) + ".json",
                    str(corrupt / f"{stem}.game.aag"),
                ], self.env, 150)
                self.assertNotEqual(
                    checked.returncode, 0, checked.stdout + checked.stderr)
                self.assertNotIn("REGION_VERIFIED", checked.stdout)

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
                 "arbiter_5.policy.aag", "arbiter_5.policy.aag.json")
        for name in names:
            with self.subTest(file=name):
                self.assertEqual((outputs[0] / name).read_bytes(),
                                 (outputs[1] / name).read_bytes())
        evidence = []
        for output in outputs:
            payload = json.loads((output / "evidence.json").read_text(encoding="utf-8"))
            self.assertIn("cost_accounting", payload)
            del payload["cost_accounting"]
            del payload["manager_lifetime"]
            evidence.append(payload)
        self.assertEqual(evidence[0], evidence[1])

    def test_default_policy_proof_artifacts_are_byte_identical_to_head(self) -> None:
        head_source = subprocess.run(
            [
                "git", "show",
                "HEAD:benchmarking/param-lift-20260922/generalize_gr1.py",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        self.assertEqual(
            head_source.returncode, 0, head_source.stderr + head_source.stdout
        )
        head_out = self.root / "head-default-policy"
        head_env = dict(self.env)
        head_env["GENERALIZE_GR1_RESULTS"] = str(
            self.root / "head-default-policy-results.tsv"
        )
        # Keep the temporary HEAD driver beside the worktree driver so its
        # __file__-relative repository paths and its parent/child self-launch
        # are both authentic.  The file is removed even when the run fails.
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=HERE,
            prefix=".head-generalize-gr1-",
            suffix=".py",
            delete=False,
        ) as stream:
            stream.write(head_source.stdout)
            head_driver = pathlib.Path(stream.name)
        try:
            head_run = _run([
                str(PYTHON), str(head_driver),
                "--family", "arbiter", "--target", "5", "--seeds", "3,4",
                "--timeout", "120", "--check-method", "both",
                "--out", str(head_out),
            ], head_env, 180)
        finally:
            head_driver.unlink(missing_ok=True)
        self.assertEqual(
            head_run.returncode, 0, head_run.stderr + head_run.stdout
        )

        current_out = self.root / "current-default-policy"
        current_run = _run([
            str(PYTHON), str(DRIVER),
            "--family", "arbiter", "--target", "5", "--seeds", "3,4",
            "--timeout", "120", "--check-method", "both",
            "--out", str(current_out),
        ], self.env, 180)
        self.assertEqual(
            current_run.returncode, 0, current_run.stderr + current_run.stdout
        )
        for name in (
            "arbiter_5.game.aag",
            "arbiter_5.certificate.aag",
            "arbiter_5.policy.aag",
        ):
            with self.subTest(file=name):
                self.assertEqual(
                    (current_out / name).read_bytes(),
                    (head_out / name).read_bytes(),
                )

    def test_candidate_bundle_for_other_target_declines_before_checker(self) -> None:
        family = "arbiter"
        seeds, built_target = ROUND_TRIPS[family]
        out = self.artifacts[family]
        expected = generalizer._candidate_request_identity(
            family, built_target - 1, seeds, out,
            family_source=None, target_source=None,
            reduction_semantics="exact",
        )
        with mock.patch.object(generalizer, "check_candidate") as checker:
            with self.assertRaisesRegex(
                generalizer.Decline, "does not match the parent request"
            ):
                bundle, builder_evidence = generalizer._launch_candidate_builder(
                    ["/bin/true"], out / "candidate-bundle.json", out,
                    generalizer.AbsoluteDeadline.after(10), expected,
                )
                generalizer.run(
                    family, built_target - 1, seeds, out,
                    generalizer.ProposerLimits(),
                    prepared_bundle=bundle,
                    builder_evidence=builder_evidence,
                )
        checker.assert_not_called()

    def test_candidate_bundle_for_other_source_declines_before_checker(self) -> None:
        family = "arbiter"
        seeds, target = ROUND_TRIPS[family]
        out = self.artifacts[family]
        other_source = ROOT / "tlsf-corpus" / f"arbiter_pb_{target}_pe_.tlsf"
        expected = generalizer._candidate_request_identity(
            family, target, seeds, out,
            family_source=None, target_source=other_source,
            reduction_semantics="exact",
        )
        payload = json.loads(
            (out / "candidate-bundle.json").read_text(encoding="utf-8"))
        self.assertNotEqual(
            payload["request_identity"]["target_source_sha256"],
            expected.target_source_sha256,
        )
        with mock.patch.object(generalizer, "check_candidate") as checker:
            with self.assertRaisesRegex(
                generalizer.Decline, "does not match the parent request"
            ):
                bundle, builder_evidence = generalizer._launch_candidate_builder(
                    ["/bin/true"], out / "candidate-bundle.json", out,
                    generalizer.AbsoluteDeadline.after(10), expected,
                )
                generalizer.run(
                    family, target, seeds, out,
                    generalizer.ProposerLimits(),
                    target_source=other_source,
                    prepared_bundle=bundle,
                    builder_evidence=builder_evidence,
                )
        checker.assert_not_called()

    def test_five_round_trip_families_match_uncached_and_head_semantically(
            self) -> None:
        """Compare the five ROUND_TRIPS families with uncached and HEAD roots."""
        pairs = []
        reference_env = dict(self.env)
        reference_env["GENERALIZE_GR1_REFERENCE_COMPOSE"] = "1"
        reference_env["GENERALIZE_GR1_REFERENCE_AAG_CONTEXT"] = "1"
        for family, (seeds, target) in ROUND_TRIPS.items():
            out = self.root / f"reference-{family}-{target}"
            proc = _run([
                str(PYTHON), str(DRIVER), "--family", family,
                "--target", str(target), "--seeds", ",".join(map(str, seeds)),
                "--timeout", "180", "--check-method", "both",
                "--out", str(out),
            ], reference_env)
            self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
            stem = f"{family}_{target}"
            for suffix in (".certificate.aag", ".policy.aag"):
                pairs.append((self.artifacts[family] / f"{stem}{suffix}",
                              out / f"{stem}{suffix}"))

        head_source = subprocess.run(
            [
                "git", "show",
                "HEAD:benchmarking/param-lift-20260922/generalize_gr1.py",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        self.assertEqual(
            head_source.returncode, 0, head_source.stderr + head_source.stdout
        )
        head_env = dict(self.env)
        head_env["GENERALIZE_GR1_RESULTS"] = str(
            self.root / "head-semantic-results.tsv"
        )
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=HERE,
            prefix=".head-semantic-generalize-gr1-",
            suffix=".py",
            delete=False,
        ) as stream:
            stream.write(head_source.stdout)
            head_driver = pathlib.Path(stream.name)
        try:
            for family, (seeds, target) in ROUND_TRIPS.items():
                out = self.root / f"head-reference-{family}-{target}"
                proc = _run([
                    str(PYTHON), str(head_driver), "--family", family,
                    "--target", str(target), "--seeds",
                    ",".join(map(str, seeds)), "--timeout", "180",
                    "--check-method", "both", "--out", str(out),
                ], head_env)
                self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
                stem = f"{family}_{target}"
                for suffix in (".certificate.aag", ".policy.aag"):
                    pairs.append((
                        self.artifacts[family] / f"{stem}{suffix}",
                        out / f"{stem}{suffix}",
                    ))
        finally:
            head_driver.unlink(missing_ok=True)

        script = r"""
import pathlib
import sys
sys.path.insert(0, sys.argv[1])
import generalize_gr1 as generalizer

bdds = generalizer.Bdds()
arguments = iter(sys.argv[2:])
for optimized_path, reference_path in zip(arguments, arguments, strict=True):
    optimized = generalizer.Aag.read(pathlib.Path(optimized_path))
    reference = generalizer.Aag.read(pathlib.Path(reference_path))
    assert optimized.input_names == reference.input_names
    assert optimized.output_names == reference.output_names
    assert len(optimized.outputs) == len(reference.outputs)
    for optimized_root, reference_root in zip(
            optimized.outputs, reference.outputs, strict=True):
        optimized_bdd = bdds.from_aag_uncached(optimized, optimized_root)
        reference_bdd = bdds.from_aag_uncached(reference, reference_root)
        assert optimized_bdd == reference_bdd, (optimized_path, reference_path)
print("semantic artifact comparison passed")
"""
        flattened = [str(path) for pair in pairs for path in pair]
        comparison = _run(
            [str(PYTHON), "-s", "-c", script, str(HERE), *flattened],
            dict(os.environ), 180,
        )
        self.assertEqual(
            comparison.returncode, 0, comparison.stderr + comparison.stdout)
        self.assertIn("semantic artifact comparison passed", comparison.stdout)

class GeneralizerUnitTest(unittest.TestCase):
    _adapter_path: pathlib.Path | None = None

    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory(prefix="generalize-gr1-unit-")
        self.root = pathlib.Path(self._temporary.name)

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def _native_adapter(self) -> pathlib.Path:
        cls = type(self)
        if cls._adapter_path is None:
            configured = os.environ.get("ACACIA_BUDDY_ADAPTER")
            if configured:
                cls._adapter_path = pathlib.Path(configured).resolve()
                self.assertTrue(cls._adapter_path.is_file())
            else:
                build = _run(
                    [str(PYTHON), str(ADAPTER_BUILD)], dict(os.environ), 120
                )
                self.assertEqual(build.returncode, 0, build.stderr + build.stdout)
                cls._adapter_path = pathlib.Path(
                    build.stdout.strip().splitlines()[-1]
                ).resolve()
        return cls._adapter_path

    def test_per_predicate_arity_fallback_is_independent(self) -> None:
        seeds = [SimpleNamespace(n=4), SimpleNamespace(n=5)]

        def projection(_bdds, seed, name, arity, _goal=None,
                       _context=None, _cache_key=None):
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
            subprocess.CompletedProcess(
                [], 3, "", "OxiDD capacity while compiling certificate"
            ),
            subprocess.CompletedProcess([], 6, "", "proof failed"),
        ]
        with (
            mock.patch.object(generalizer, "_run", side_effect=responses),
            mock.patch.object(
                generalizer, "_memory_headroom_bytes", return_value=2 << 30
            ),
        ):
            result = generalizer.check_candidate(
                target, cert, policy, "auto", limits, "retry")
        self.assertEqual(result["verdict"], "CERT_FAILED")
        self.assertEqual(result["node_caps"], [1024, 2048])
        self.assertTrue(result["attempts"][0]["retry"])

    def test_checker_does_not_retry_generic_unknown_or_timeout(self) -> None:
        target = SimpleNamespace(n=5, game_path=self.root / "game.aag")
        cert = self.root / "candidate.aag"
        policy = self.root / "policy.aag"
        limits = generalizer.ProposerLimits(checker_nodes=1024)
        for response in (
            subprocess.CompletedProcess([], 3, "", "inconclusive"),
            subprocess.CompletedProcess([], 124, "", "timeout"),
        ):
            with self.subTest(returncode=response.returncode):
                with mock.patch.object(
                    generalizer, "_run", return_value=response
                ) as run:
                    result = generalizer.check_candidate(
                        target, cert, policy, "auto", limits, "no-retry"
                    )
                self.assertEqual(run.call_count, 1)
                self.assertEqual(result["node_caps"], [1024])

    def test_region_checker_is_policy_free_and_requires_versioned_result(self) -> None:
        target = SimpleNamespace(n=5, game_path=self.root / "game.aag")
        cert = self.root / "candidate.aag"
        limits = generalizer.ProposerLimits(checker_nodes=1024)

        def region_verified(command, _timeout):
            json_out = pathlib.Path(command[command.index("--json-out") + 1])
            json_out.write_text(json.dumps({
                "format": "tlsf-gr1-region-checkresult-v1",
                "method": "gr1-region-v1",
                "verdict": "REGION_VERIFIED",
                "exit_code": 0,
            }), encoding="utf-8")
            return subprocess.CompletedProcess(
                command, 0,
                "METHOD region REGION_VERIFIED version=gr1-region-v1\n"
                "REGION_VERIFIED\n",
                "",
            )

        with mock.patch.object(
            generalizer, "_run", side_effect=region_verified
        ) as run:
            result = generalizer.check_candidate(
                target, cert, None, "region", limits, "target-region")
        command = run.call_args.args[0]
        self.assertEqual(command[-1], str(target.game_path))
        self.assertNotIn("policy.aag", command)
        self.assertTrue(result["proof_verified"])
        self.assertEqual(result["proof_method"], "gr1-region-v1")
        self.assertEqual(result["result_string"], "REGION_VERIFIED")

        with mock.patch.object(
            generalizer, "_run",
            return_value=subprocess.CompletedProcess(
                [], 0, "REGION_VERIFIED\n", ""),
        ):
            malformed = generalizer.check_candidate(
                target, cert, None, "region", limits, "target-malformed")
        self.assertFalse(malformed["proof_verified"])

    def test_region_retry_is_capacity_only_and_obeys_resource_gates(self) -> None:
        target = SimpleNamespace(n=5, game_path=self.root / "game.aag")
        cert = self.root / "region-retry-candidate.aag"
        limits = generalizer.ProposerLimits(checker_nodes=1024)

        def response(verdict: str, returncode: int, peak_delta: int = 0):
            def run(command, _timeout):
                node_cap = int(command[command.index("--node-cap") + 1])
                json_out = pathlib.Path(command[command.index("--json-out") + 1])
                json_out.write_text(json.dumps({
                    "format": "tlsf-gr1-region-checkresult-v1",
                    "method": "gr1-region-v1",
                    "verdict": verdict,
                    "exit_code": returncode,
                    "peak_bdd_nodes": node_cap + peak_delta,
                }), encoding="utf-8")
                return subprocess.CompletedProcess(
                    command, returncode, f"{verdict}\n", ""
                )

            return run

        capacity_then_failed = [
            response("UNKNOWN", 3),
            response("REGION_FAILED", 6),
        ]
        with (
            mock.patch.object(
                generalizer, "_run",
                side_effect=lambda command, timeout: capacity_then_failed.pop(0)(
                    command, timeout
                ),
            ) as run,
            mock.patch.object(
                generalizer, "_memory_headroom_bytes", return_value=2 << 30
            ),
        ):
            retried = generalizer.check_candidate(
                target, cert, None, "region", limits, "region-capacity",
                generalizer.AbsoluteDeadline.after(30),
            )
        self.assertEqual(run.call_count, 2)
        self.assertEqual(retried["node_caps"], [1024, 2048])
        self.assertTrue(retried["attempts"][0]["retry"])

        non_capacity = (
            ("REGION_FAILED", 6, 0),
            ("UNKNOWN", 3, -1),
            ("UNKNOWN", 124, 0),
        )
        for verdict, returncode, peak_delta in non_capacity:
            with self.subTest(verdict=verdict, returncode=returncode):
                with mock.patch.object(
                    generalizer, "_run",
                    side_effect=response(verdict, returncode, peak_delta),
                ) as run:
                    result = generalizer.check_candidate(
                        target, cert, None, "region", limits,
                        f"region-no-retry-{returncode}",
                        generalizer.AbsoluteDeadline.after(30),
                    )
                self.assertEqual(run.call_count, 1)
                self.assertEqual(result["node_caps"], [1024])

        gate_cases = (
            (
                "deadline",
                generalizer.AbsoluteDeadline.after(0.5),
                2 << 30,
                "insufficient_absolute_deadline",
            ),
            (
                "memory",
                generalizer.AbsoluteDeadline.after(30),
                (1 << 30) - 1,
                "insufficient_memory_headroom",
            ),
        )
        for label, deadline, headroom, expected_reason in gate_cases:
            with self.subTest(gate=label):
                with (
                    mock.patch.object(
                        generalizer, "_run",
                        side_effect=response("UNKNOWN", 3),
                    ) as run,
                    mock.patch.object(
                        generalizer, "_memory_headroom_bytes",
                        return_value=headroom,
                    ),
                ):
                    result = generalizer.check_candidate(
                        target, cert, None, "region", limits,
                        f"region-{label}-gate", deadline,
                    )
                self.assertEqual(run.call_count, 1)
                self.assertFalse(result["attempts"][0]["retry"])
                self.assertEqual(
                    result["attempts"][0]["retry_reason"], expected_reason
                )

    def test_checker_capacity_retry_requires_deadline_and_memory(self) -> None:
        deadline = generalizer.AbsoluteDeadline.after(30)
        with mock.patch.object(
            generalizer, "_memory_headroom_bytes", return_value=(1 << 30) - 1
        ):
            allowed, reason = generalizer._checker_retry_allowed(deadline, 0.1)
        self.assertFalse(allowed)
        self.assertEqual(reason, "insufficient_memory_headroom")
        expired = generalizer.AbsoluteDeadline(time.monotonic() - 1)
        with mock.patch.object(
            generalizer, "_memory_headroom_bytes", return_value=2 << 30
        ):
            allowed, reason = generalizer._checker_retry_allowed(expired, 0.1)
        self.assertFalse(allowed)
        self.assertEqual(reason, "insufficient_absolute_deadline")

    def test_cegis_seed_bundle_solves_only_new_sizes(self) -> None:
        old = SimpleNamespace(n=2)
        new = SimpleNamespace(n=3)
        existing = generalizer.SeedBundle("arbiter", {2: old}, (), 1.0)
        limits = generalizer.ProposerLimits(checker_timeout_s=30)
        with mock.patch.object(
            generalizer, "solve_seed", return_value=new
        ) as solve:
            bundle = generalizer.acquire_small_instances(
                "arbiter", (2, 3), self.root, limits,
                generalizer.AbsoluteDeadline.after(30), existing=existing,
            )
        solve.assert_called_once()
        self.assertEqual(solve.call_args.args[1], 3)
        self.assertEqual(bundle.newly_solved, (3,))
        self.assertIs(bundle.instances[2], old)
        self.assertIs(bundle.instances[3], new)

    def test_target_can_never_enter_seed_bundle(self) -> None:
        seed = SimpleNamespace(n=5)
        seeds = generalizer.SeedBundle("arbiter", {5: seed}, (), 0.0)
        schema = generalizer.SchemaBundle(
            "arbiter", seeds, 2, 1, (frozenset(),)
        )
        with self.assertRaisesRegex(generalizer.Decline, "never be used as a seed"):
            generalizer.instantiate(
                schema, 5, self.root, generalizer.ProposerLimits(),
                generalizer.AbsoluteDeadline.after(30),
            )

    def test_expired_deadline_stops_between_orchestration_stages(self) -> None:
        seeds = generalizer.SeedBundle(
            "arbiter", {3: SimpleNamespace(n=3)}, (), 0.0
        )
        with self.assertRaisesRegex(generalizer.Decline, "absolute deadline"):
            generalizer.learn_schema(
                seeds,
                generalizer.ProposerLimits(),
                generalizer.AbsoluteDeadline(time.monotonic() - 1),
            )

    def test_diagnostic_checker_probe_is_skipped_or_deadline_clamped(self) -> None:
        expired = generalizer.AbsoluteDeadline(time.monotonic() - 1)
        with mock.patch.object(generalizer.subprocess, "run") as run:
            self.assertIsNone(generalizer._probe_checker_stats(expired))
        run.assert_not_called()

        live = generalizer.AbsoluteDeadline.after(0.2)
        response = subprocess.CompletedProcess([], 0, "--stats FILE\n", "")
        with mock.patch.object(
            generalizer.subprocess, "run", return_value=response
        ) as run:
            self.assertEqual(
                generalizer._probe_checker_stats(live), "file_separate")
        timeout = run.call_args.kwargs["timeout"]
        self.assertGreater(timeout, 0)
        self.assertLessEqual(timeout, 0.2)

    def test_adapter_hash_cache_reuses_and_invalidates_by_file_identity(self) -> None:
        payload = self.root / "payload.bin"
        payload.write_bytes(b"first")
        adapter_module._clear_hash_cache_for_testing()
        first = adapter_module._sha256(payload)
        self.assertEqual(adapter_module._sha256(payload), first)
        stats = adapter_module.hash_cache_diagnostics()
        self.assertEqual((stats["misses"], stats["hits"]), (1, 1))
        original = payload.stat()
        payload.write_bytes(b"other")
        os.utime(
            payload,
            ns=(original.st_atime_ns, original.st_mtime_ns),
        )
        self.assertEqual(payload.stat().st_size, original.st_size)
        self.assertEqual(payload.stat().st_mtime_ns, original.st_mtime_ns)
        self.assertNotEqual(adapter_module._sha256(payload), first)
        self.assertEqual(adapter_module.hash_cache_diagnostics()["misses"], 2)

    def test_adapter_sidecar_is_content_bound_across_checkouts(self) -> None:
        adapter = self.root / "adapter.so"
        library = self.root / "libbddx.so"
        extension = self.root / "_buddy.so"
        runtime_source = self.root / "other-checkout" / "adapter.cc"
        runtime_source.parent.mkdir()
        runtime_source.write_bytes(adapter_module.SOURCE.read_bytes())
        adapter.write_bytes(b"adapter")
        library.write_bytes(b"library")
        extension.write_bytes(b"extension")

        def digest(path: pathlib.Path) -> str:
            return hashlib.sha256(path.read_bytes()).hexdigest()

        sidecar = {
            "schema": adapter_module.SIDECAR_SCHEMA,
            "adapter_sha256": digest(adapter),
            "source_sha256": digest(runtime_source),
            "compiler": "g++",
            "compiler_version": "test compiler",
            "flags": ["-shared"],
            "libbddx_sha256": digest(library),
            "binding_extension_sha256": digest(extension),
            "bdd_header_sha256": "1" * 64,
        }
        adapter_module.adapter_sidecar_path(adapter).write_text(
            json.dumps(sidecar), encoding="utf-8"
        )
        adapter_module._clear_hash_cache_for_testing()
        with mock.patch.object(adapter_module, "SOURCE", runtime_source):
            validated = adapter_module._validate_sidecar(
                adapter, library, extension
            )
        self.assertEqual(validated, sidecar)
        self.assertFalse(any(key.endswith("_path") for key in sidecar))

        def replace_preserving_mtime(path: pathlib.Path, content: bytes) -> None:
            original = path.stat()
            self.assertEqual(len(content), original.st_size)
            path.write_bytes(content)
            os.utime(path, ns=(original.st_atime_ns, original.st_mtime_ns))

        replace_preserving_mtime(adapter, b"ADAPTER")
        with (
            mock.patch.object(adapter_module, "SOURCE", runtime_source),
            self.assertRaisesRegex(
                adapter_module.BuddyAdapterError, "adapter_sha256 mismatch"
            ),
        ):
            adapter_module._validate_sidecar(adapter, library, extension)

        replace_preserving_mtime(adapter, b"adapter")
        replace_preserving_mtime(library, b"LIBRARY")
        with (
            mock.patch.object(adapter_module, "SOURCE", runtime_source),
            self.assertRaisesRegex(
                adapter_module.BuddyAdapterError, "libbddx_sha256 mismatch"
            ),
        ):
            adapter_module._validate_sidecar(adapter, library, extension)

    def test_native_substitution_is_simultaneous_and_errors_are_safe(self) -> None:
        script = r"""
import sys
sys.path.insert(0, sys.argv[1])
import generalize_gr1 as generalizer

bdds = generalizer.Bdds(var_count=128)
assert bdds.compose_route == "native_veccompose"
buddy = bdds.buddy
variables = [0, 1, 2]
temporaries = [32, 33, 34]
x, y, z, outside = [buddy.bdd_ithvar(index) for index in range(4)]
neg = buddy.bdd_not
cases = {
    "swap": ((x & neg(y)) | z, [0, 1], [y, x], [32, 33],
             (y & neg(x)) | z),
    "cycle": ((x & y) | (neg(y) & z), variables, [y, z, x],
              temporaries, None),
    "dependent": (x ^ y, [0, 1], [y & z, x | z], [32, 33], None),
    "non-injective": (x ^ y, [0, 1], [z, z], [32, 33], buddy.bddfalse),
    "constants": (x ^ y, [0, 1], [buddy.bddtrue, buddy.bddfalse],
                  [32, 33], buddy.bddtrue),
    "outside support": (x & y, [0, 3], [z, buddy.bddfalse], [32, 35], z & y),
}
for name, (function, source, replacements, scratch, expected) in cases.items():
    native = bdds.substitute_variables(function, source, replacements, scratch)
    oracle = bdds.substitute_variables_two_pass(
        function, source, replacements, scratch)
    assert native == oracle, name
    if expected is not None:
        assert native == expected, name

function = (x & neg(y)) | (z & outside)
replacements = [y | z, x & outside, neg(y)]
expected = bdds.substitute_variables_two_pass(
    function, variables, replacements, temporaries)
for _iteration in range(25):
    assert bdds.substitute_variables(
        function, variables, replacements, temporaries) == expected
    bdds.collect_garbage_for_testing()

bdds._veccompose.force_failure_for_testing()
try:
    bdds.substitute_variables(function, variables, replacements, temporaries)
except RuntimeError as error:
    assert "resource failure" in str(error)
else:
    raise AssertionError("forced resource failure returned a BDD")
assert bdds.substitute_variables(
    function, variables, replacements, temporaries) == expected

try:
    bdds._veccompose.compose(
        function, [bdds._veccompose.variable_count()], [x])
except ValueError as error:
    assert "must be in" in str(error)
else:
    raise AssertionError("Python accepted an out-of-range BuDDy variable")

try:
    bdds._veccompose.trigger_buddy_error_for_testing()
except RuntimeError as error:
    assert "out-of-range bdd_setbddpair test" in str(error)
else:
    raise AssertionError("a real BuDDy error returned a BDD")
assert bdds._veccompose.prior_handler_restored_for_testing()
assert bdds.substitute_variables(
    function, variables, replacements, temporaries) == expected
assert bdds._veccompose.max_variable_count == 2_097_150
assert bdds._veccompose._library.p2a_bdd_max_variable_count() == 2_097_150
assert bdds._veccompose._library.p2a_bdd_setvarnum_checked(2_097_151) != 0
try:
    bdds._ensure_variables(2_097_151)
except OverflowError as error:
    assert "invalid BDD variable requirement" in str(error)
else:
    raise AssertionError("backend-max plus one reached BuDDy")
print("native substitution checks passed")
"""
        env = dict(os.environ)
        env["ACACIA_BUDDY_ADAPTER"] = str(self._native_adapter())
        proc = _run(
            [str(PYTHON), "-s", "-c", script, str(HERE)], env, 120)
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        self.assertIn("native substitution checks passed", proc.stdout)

    def test_native_relabel_routes_and_appex_are_exact(self) -> None:
        script = r"""
import random
import sys
sys.path.insert(0, sys.argv[1])
import buddy_veccompose as adapter_module
import generalize_gr1 as generalizer

bdds = generalizer.Bdds(var_count=128)
buddy = bdds.buddy
adapter = bdds._veccompose
assert adapter is not None
rng = random.Random(20260924)
variables = [buddy.bdd_ithvar(index) for index in range(8)]
routes = []
native_relabel = adapter.relabel_variables

def record_route(function, sources, targets, *, use_replace):
    routes.append("bdd_replace" if use_replace else "bdd_veccompose")
    return native_relabel(
        function, sources, targets, use_replace=use_replace)

adapter.relabel_variables = record_route

def random_function():
    result = buddy.bddfalse
    for _iteration in range(16):
        left, right = rng.sample(variables, 2)
        term = left & buddy.bdd_not(right)
        result = (result ^ term) | (left & right)
    return result

permutations = [
    [1, 0, 2, 3, 4, 5, 6, 7],
    [1, 2, 0, 3, 4, 5, 6, 7],
    [7, 2, 3, 4, 5, 6, 0, 1],
]
for _iteration in range(32):
    permutation = list(range(8))
    rng.shuffle(permutation)
    permutations.append(permutation)

for iteration, permutation in enumerate(permutations):
    function = random_function()
    sources = list(range(8))
    replace = adapter.relabel_variables(
        function, sources, permutation, use_replace=True)
    composed = adapter.relabel_variables(
        function, sources, permutation, use_replace=False)
    assert replace == composed, (iteration, permutation)
    before = len(routes)
    production = bdds.relabel(
        function, dict(zip(sources, permutation, strict=True)))
    assert production == composed, (iteration, permutation)
    assert routes[before:] == ["bdd_replace"]
    if iteration % 7 == 0:
        bdds.collect_garbage_for_testing()

x, y, z = variables[:3]
collision_function = x ^ y
collision_mapping = {0: 2, 1: 2}
fallback = adapter.relabel_variables(
    collision_function, [0, 1], [2, 2], use_replace=False)
before = len(routes)
production = bdds.relabel(collision_function, collision_mapping)
assert production == fallback == buddy.bddfalse
assert routes[before:] == ["bdd_veccompose"]

# A caller hint can be empty, partial, or stale; only the root's actual
# support determines coverage and the replace route.
for hint in ([], {0}, {0, 1, 2}):
    before = len(routes)
    production = bdds.relabel(
        collision_function, {0: 1, 1: 0}, support=hint)
    reference = native_relabel(
        collision_function, [0, 1], [1, 0], use_replace=False)
    assert production == reference
    assert routes[before:] == ["bdd_replace"]

for hint in ([], {0}, {0, 1, 2}):
    before = len(routes)
    try:
        bdds.relabel(collision_function, {0: 1}, support=hint)
    except KeyError as error:
        assert "no relabelling" in str(error)
    else:
        raise AssertionError("an unmapped support variable was accepted")
    assert len(routes) == before

# The map 0->1 is injective, but variable 1 is still in the root.
# Simultaneous substitution makes x ^ y false. Native replace must reject
# the omitted source cleanly, and the high-level route maps 1 explicitly.
reference = native_relabel(
    collision_function, [0], [1], use_replace=False)
assert reference == buddy.bddfalse
try:
    native_relabel(collision_function, [0], [1], use_replace=True)
except adapter_module.BuddyAdapterError as error:
    assert "every root support variable" in str(error)
else:
    raise AssertionError("native replace accepted an untouched support variable")
before = len(routes)
production = bdds.relabel(
    collision_function, {0: 1, 1: 1}, support={0})
assert production == reference
assert routes[before:] == ["bdd_veccompose"]
assert bdds.relabel_route_counts == {}

# The support cube has variable indices, independent of current BDD levels.
adapter.set_variable_order_for_testing(list(reversed(range(128))))
reordered = variables[0] ^ variables[1]
before = len(routes)
production = bdds.relabel(reordered, {0: 1, 1: 0}, support=[])
reference = native_relabel(
    reordered, [0, 1], [1, 0], use_replace=False)
assert production == reference
assert routes[before:] == ["bdd_replace"]
before = len(routes)
production = bdds.relabel(reordered, {0: 1, 1: 1}, support={0})
reference = native_relabel(reordered, [0, 1], [1, 1], use_replace=False)
assert production == reference == buddy.bddfalse
assert routes[before:] == ["bdd_veccompose"]

try:
    bdds.relabel(x & y, {0: 2})
except KeyError as error:
    assert "no relabelling" in str(error)
else:
    raise AssertionError("an incomplete relabel map was accepted")

for _iteration in range(40):
    left = random_function()
    right = random_function()
    quantified = sorted(rng.sample(range(8), rng.randrange(0, 6)))
    cube = bdds.cube(quantified)
    fused = bdds.and_exist(left, right, cube)
    reference = buddy.bdd_exist(left & right, cube)
    assert fused == reference, quantified

def forbidden_snapshot(_bdds):
    raise AssertionError("diagnostic snapshot ran with diagnostics disabled")

generalizer._diagnostic_bdd_snapshot = forbidden_snapshot
assert bdds.relabel(reordered, {0: 1, 1: 0}) == reordered
assert bdds.cube([0, 1]) == variables[0] & variables[1]
assert generalizer._support(bdds, reordered) == {0, 1}

print("native relabel and appex checks passed")
"""
        env = dict(os.environ)
        env["ACACIA_BUDDY_ADAPTER"] = str(self._native_adapter())
        proc = _run(
            [str(PYTHON), "-s", "-c", script, str(HERE)], env, 120)
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        self.assertIn("native relabel and appex checks passed", proc.stdout)

    def test_owner_index_matches_reference_at_client_boundaries(self) -> None:
        rng = random.Random(20260923)
        for client_count in (0, 1, 63, 64, 65):
            clients = list(range(client_count))
            chosen = tuple(clients[index] for index in range(
                min(4, client_count)))
            variables = [
                generalizer.VarInfo(1000, ("shared",), frozenset()),
            ]
            if clients:
                variables.extend((
                    generalizer.VarInfo(7, ("single",), frozenset((clients[0],))),
                    generalizer.VarInfo(
                        1_000_003, ("last",), frozenset((clients[-1],))),
                ))
            if client_count >= 2:
                variables.append(generalizer.VarInfo(
                    91, ("multi",), frozenset((clients[0], clients[-1]))))
            rng.shuffle(variables)
            index = generalizer.OwnerIndex(variables)
            self.assertTrue(all(isinstance(key, tuple)
                                for key in index.owner_groups))
            self.assertTrue(all(
                list(public_ids) == sorted(public_ids)
                for public_ids in index.owner_groups.values()
            ))
            for subset in ((), chosen, tuple(reversed(chosen))):
                selected = frozenset(subset)
                reference = tuple(sorted(
                    (item for item in variables
                     if not item.owners or item.owners <= selected),
                    key=lambda item: item.index,
                ))
                self.assertEqual(index.keep_items(subset), reference)

            # Rename every semantic client ID. Owner tuples remain provenance
            # IDs, while sparse public variable IDs and emitted order do not.
            permutation = clients[:]
            rng.shuffle(permutation)
            rename = dict(zip(clients, permutation, strict=True))
            renamed = [
                generalizer.VarInfo(
                    item.index, item.key,
                    frozenset(rename[owner] for owner in item.owners),
                )
                for item in variables
            ]
            renamed_subset = tuple(rename[client] for client in chosen)
            self.assertEqual(
                [item.index for item in generalizer.OwnerIndex(
                    renamed).keep_items(renamed_subset)],
                [item.index for item in index.keep_items(chosen)],
            )

    def test_support_restricted_projection_is_mapping_order_and_gc_safe(self) -> None:
        script = r"""
import gc
import pathlib
import sys
import tempfile
from types import SimpleNamespace
sys.path.insert(0, sys.argv[1])
import generalize_gr1 as generalizer

def target(root, name, variables):
    game_path = root / f"{name}.game"
    prov_path = root / f"{name}.prov"
    game_path.write_text(name, encoding="utf-8")
    prov_path.write_text(name, encoding="utf-8")
    return SimpleNamespace(
        family=name, n=2, game_path=game_path, prov_path=prov_path,
        cert_path=None, cert=None, variables=variables, goals=[],
        game=SimpleNamespace(latches=[], inputs=[]),
        role_by_client={0: "role", 1: "role"},
    )

bdds = generalizer.Bdds(var_count=128)
buddy = bdds.buddy
variables = [
    generalizer.VarInfo(7, ("shared",), frozenset()),
    generalizer.VarInfo(91, ("owned-0",), frozenset((0,))),
    generalizer.VarInfo(1_000_003, ("owned-both",), frozenset((0, 1))),
]
with tempfile.TemporaryDirectory(prefix="p2b-support-") as temporary:
    root_path = pathlib.Path(temporary)
    instance = target(root_path, "first", variables)
    # Reserve the same canonical keys with a compact real attempt ABI.  The
    # projection fixture then supplies intentionally sparse semantic IDs,
    # which are independent of the BDD coordinate layout under test here.
    layout_instance = target(root_path, "layout", [
        generalizer.VarInfo(0, ("shared",), frozenset()),
        generalizer.VarInfo(1, ("owned-0",), frozenset((0,))),
        generalizer.VarInfo(2, ("owned-both",), frozenset((0, 1))),
    ])
    context = bdds.begin_attempt([layout_instance], layout_instance)
    # Sparse public IDs deliberately map non-monotonically to semantic BDD
    # variables.  Sorted/positional mapping bugs cannot preserve this formula.
    public_to_bdd = {7: 12, 91: 5, 1_000_003: 9}
    rebuilt_root = (
        buddy.bdd_ithvar(public_to_bdd[7])
        & buddy.bdd_ithvar(public_to_bdd[91])
    ) | buddy.bdd_ithvar(public_to_bdd[1_000_003])
    root = rebuilt_root

    # Reorder levels before support extraction. Public IDs continue to map to
    # semantic BDD variables, never to the changing levels.
    bdds._veccompose.set_variable_order_for_testing(
        list(reversed(range(bdds._veccompose.variable_count()))))
    support = context.exact_public_support(root, public_to_bdd)
    assert support.public_variables == frozenset((7, 91, 1_000_003))
    assert context.cached_bdd_support(root) == frozenset((5, 9, 12))
    extract = generalizer._support
    def forbidden_extraction(_bdds, _root):
        raise AssertionError("root-bound support cache was bypassed")
    generalizer._support = forbidden_extraction
    try:
        assert bdds.relabel(root, {5: 5, 9: 9, 12: 12}, support=[]) == root
    finally:
        generalizer._support = extract

    for subset in ((), (0,), (1,), (1, 0)):
        metadata = context.subset_metadata(instance, subset)
        drop, cube = context.projection_drop(
            instance, subset, root, public_to_bdd, metadata["keep"], support)
        reference_keep = frozenset(
            item.index for item in variables
            if not item.owners or item.owners <= frozenset(subset))
        reference_drop = support.public_variables - reference_keep
        # Rebuild the reference cube independently instead of reusing the
        # production cube helper exercised by projection_drop.
        reference_cube = buddy.bddtrue
        for public in sorted(reference_drop):
            reference_cube &= buddy.bdd_ithvar(public_to_bdd[public])
        projected = buddy.bdd_exist(root, cube) if drop else root
        reference = (
            buddy.bdd_exist(rebuilt_root, reference_cube)
            if reference_drop else rebuilt_root
        )
        assert drop == reference_drop
        assert projected == reference

        projected_support = generalizer._support(bdds, projected)
        normalized_variables = {
            variable: 80 + position
            for position, variable in enumerate(sorted(projected_support))
        }
        normalized = bdds.relabel(projected, normalized_variables)
        rebuilt = bdds.relabel(
            normalized,
            {normal: concrete for concrete, normal in normalized_variables.items()})
        assert rebuilt == projected

    # Isolate the support cache as the sole owner before collection: remove
    # projection-owned roots and every caller-owned BDD proxy, then recover the
    # root from the cache only after Python and BuDDy GC have both run.
    support_key = next(
        key for key, value in context._support_cache.items()
        if value is support
    )
    context._projection_metadata.clear()
    context._projection_metadata_weights.clear()
    context._projection_metadata_bytes = 0
    del root, rebuilt_root, support, cube, projected, reference
    del reference_cube, normalized, rebuilt
    gc.collect()
    bdds.collect_garbage_for_testing()
    cached_support = context._support_cache[support_key]
    cached_root = cached_support.root
    assert context.exact_public_support(
        cached_root, public_to_bdd) is cached_support
    assert generalizer._support(bdds, cached_root) == {5, 9, 12}

    # The same root under a different registered semantic mapping must not
    # alias the first cache entry.
    other_mapping = {107: 12, 191: 5, 2_000_003: 9}
    other = context.exact_public_support(cached_root, other_mapping)
    assert other.public_variables == frozenset((107, 191, 2_000_003))
    assert other is not cached_support
    context.release()

    # A second attempt in the same process-wide manager gets an independent
    # layout/cache and may use a different public ABI.
    second = target(root_path, "second", [
        generalizer.VarInfo(2, ("second",), frozenset()),
    ])
    other_context = bdds.begin_attempt([second], second)
    assert other_context.layout is not context.layout
    assert other_context.subset_metadata(second, ())["keep"] == frozenset((2,))
    other_context.release()
print("support projection checks passed")
"""
        env = dict(os.environ)
        env["ACACIA_BUDDY_ADAPTER"] = str(self._native_adapter())
        proc = _run(
            [str(PYTHON), "-s", "-c", script, str(HERE)], env, 120)
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        self.assertIn("support projection checks passed", proc.stdout)

    def test_variable_layout_checks_limits_overlap_and_large_counters(self) -> None:
        backend_maximum = generalizer.BDD_COORDINATE_LIMIT
        for required in (backend_maximum - 1, backend_maximum):
            layout = generalizer.VariableLayout.plan(
                public_variables=required,
                public_states=0, public_letters=0,
                composition_states=0, composition_letters=0,
                policy_counters=0, canonical_templates=0,
            )
            self.assertEqual(layout.required_variables, required)
            self.assertEqual(layout.public.end, required)
        with self.assertRaises(OverflowError):
            generalizer.VariableLayout.plan(
                public_variables=backend_maximum + 1,
                public_states=0, public_letters=0,
                composition_states=0, composition_letters=0,
                policy_counters=0, canonical_templates=0,
            )
        with self.assertRaises(OverflowError):
            generalizer.VariableLayout.plan(
                public_variables=8, public_states=5, public_letters=3,
                composition_states=5, composition_letters=3,
                policy_counters=4, canonical_templates=6, variable_limit=20,
            )
        with self.assertRaises(OverflowError):
            generalizer.VariableLayout.plan(
                public_variables=generalizer.BDD_COORDINATE_LIMIT,
                public_states=0, public_letters=0,
                composition_states=1, composition_letters=0,
                policy_counters=0, canonical_templates=0,
            )
        with self.assertRaises(ValueError):
            generalizer.VariableLayout(
                public=generalizer.VariableBlock("public", 0, 4),
                composition=generalizer.VariableBlock("composition", 3, 2),
                policy_counter=generalizer.VariableBlock("policy_counter", 5, 1),
                policy_game=generalizer.VariableBlock("policy_game", 6, 4),
                canonical_templates=generalizer.VariableBlock("canonical", 10, 1),
                public_state_count=2, public_letter_count=2,
                composition_state_count=1, composition_letter_count=0,
            )

        layout = generalizer.VariableLayout.plan(
            public_variables=8, public_states=5, public_letters=3,
            composition_states=5, composition_letters=3,
            policy_counters=600, canonical_templates=20, variable_limit=2048,
        )
        self.assertEqual(layout.policy_counter.size, 600)
        self.assertGreater(layout.policy_counter.coordinate(599), 512)
        blocks = (layout.public, layout.composition, layout.policy_counter,
                  layout.policy_game, layout.canonical_templates)
        coordinates = [
            set(range(block.start, block.end)) for block in blocks
        ]
        for left, right in itertools.combinations(coordinates, 2):
            self.assertFalse(left & right)

    def test_attempt_caches_are_bounded_source_and_manager_local(self) -> None:
        script = r"""
import gc
import pathlib
import sys
import tempfile
import weakref
sys.path.insert(0, sys.argv[1])
import generalize_gr1 as generalizer

def instance(root, name, byte):
    game_path = root / f"{name}.game.aag"
    prov_path = root / f"{name}.prov.json"
    cert_path = root / f"{name}.cert.aag"
    game_path.write_bytes(bytes([byte]))
    prov_path.write_text(f'{{"source": "{name}"}}', encoding="utf-8")
    cert_path.write_text(f"certificate {name}\n", encoding="utf-8")
    game = generalizer.Aag(
        game_path, 3, [2, 4, 6], [], [], [], [], [], [], [],
        ["x0", "x1", "x2"], [], [], [], [])
    cert = generalizer.Aag(
        cert_path, 3, [2, 4, 6], [], [2], [], [], [], [], [],
        ["x0", "x1", "x2"], [], ["inv"], [], [])
    variables = [
        generalizer.VarInfo(index, ("state", "x", (index,), 0),
                            frozenset((index,)))
        for index in range(3)
    ]
    return generalizer.Instance(
        "fixture", 3, game_path, prov_path, cert_path, game, {}, cert, {},
        variables, [], {0: "role", 1: "role", 2: "role"}, ())

class Payload:
    pass

bdds = generalizer.Bdds(var_count=128)
with tempfile.TemporaryDirectory(prefix="attempt-cache-") as temporary:
    root = pathlib.Path(temporary)
    source_a = instance(root, "source-a", 1)
    source_b = instance(root, "source-b", 2)
    context = generalizer.GeneralizationAttemptContext(
        bdds, [source_a, source_b], source_a)

    key_a = context.predicate_cache_key(
        [source_a], "inv", 1, [None], ["inv"])
    key_b = context.predicate_cache_key(
        [source_b], "inv", 1, [None], ["inv"])
    assert key_a != key_b
    context.template_cache_put(
        key_a, (generalizer.TemplateSet({(): "source-a"}), 1))
    assert context.template_cache_get(key_b) is None

    context.template_cache_limit = 2
    payload = Payload()
    reference = weakref.ref(payload)
    evicted_key = context.predicate_cache_key(
        [source_a], "evicted", 1, [None], ["inv"])
    context.template_cache_put(
        evicted_key, (generalizer.TemplateSet({(): payload}), 1))
    del payload
    for name in ("live-1", "live-2"):
        key = context.predicate_cache_key(
            [source_a], name, 1, [None], ["inv"])
        context.template_cache_put(
            key, (generalizer.TemplateSet({(): name}), 1))
    gc.collect()
    assert reference() is None
    assert context.template_cache_get(evicted_key) is None
    context.template_cache_put(
        evicted_key, (generalizer.TemplateSet({(): "recomputed"}), 1))
    assert context.template_cache_get(evicted_key)[0][()] == "recomputed"

    context.subset_metadata_limit = 2
    first = context.subset_metadata(source_a, (0,))
    first_values = dict(first)
    context.subset_metadata(source_a, (1,))
    context.subset_metadata(source_a, (2,))
    first_again = context.subset_metadata(source_a, (0,))
    assert dict(first_again) == first_values

    marker = Payload()
    marker_reference = weakref.ref(marker)
    context.subset_metadata_limit = 1
    context._subset_cache_put(("marker",), {"owned": marker})
    del marker
    context._subset_cache_put(("replacement",), {"owned": None})
    gc.collect()
    assert marker_reference() is None

    # Each BDD-owning cache evicts on retained payload bytes while its entry
    # ceiling is still 256.  The roots have equal-shaped but distinct cache
    # keys so the second insertion must cross the one-entry byte budget.
    buddy = bdds.buddy
    bdd_root = (
        buddy.bdd_ithvar(0) & buddy.bdd_ithvar(1)
        & buddy.bdd_ithvar(2)
    )
    public_to_bdd = {0: 0, 1: 1, 2: 2}
    support = context.exact_public_support(bdd_root, public_to_bdd)
    support_first_key = next(reversed(context._support_cache))
    context.support_cache_byte_limit = context._support_cache_bytes
    context.exact_public_support(bdd_root, {10: 0, 11: 1, 12: 2})
    assert len(context._support_cache) < 2
    assert support_first_key not in context._support_cache
    assert context._support_cache_bytes <= context.support_cache_byte_limit

    context.projection_drop(
        source_a, (0,), bdd_root, public_to_bdd, frozenset((0,)), support)
    projection_first_key = next(reversed(context._projection_metadata))
    cube_first_key = next(reversed(context._cube_cache))
    context.projection_metadata_byte_limit = context._projection_metadata_bytes
    context.cube_cache_byte_limit = context._cube_cache_bytes
    context.projection_drop(
        source_a, (1,), bdd_root, public_to_bdd, frozenset((1,)), support)
    assert len(context._projection_metadata) < 2
    assert projection_first_key not in context._projection_metadata
    assert (
        context._projection_metadata_bytes
        <= context.projection_metadata_byte_limit
    )
    assert len(context._cube_cache) < 2
    assert cube_first_key not in context._cube_cache
    assert context._cube_cache_bytes <= context.cube_cache_byte_limit

    first_manager_key = key_a
    context.release()
    saved_lifetime = bdds.manager_lifetime
    bdds.manager_lifetime = object()
    other_manager = generalizer.GeneralizationAttemptContext(
        bdds, [source_a], source_a)
    other_manager_key = other_manager.predicate_cache_key(
        [source_a], "inv", 1, [None], ["inv"])
    assert first_manager_key != other_manager_key
    assert other_manager.template_cache_get(first_manager_key) is None
    other_manager.release()
    bdds.manager_lifetime = saved_lifetime
print("attempt cache checks passed")
"""
        proc = _run(
            [str(PYTHON), "-s", "-c", script, str(HERE)],
            dict(os.environ), 120,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        self.assertIn("attempt cache checks passed", proc.stdout)

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
