#!/usr/bin/env python3
"""Fast source-binding regressions using real SYNTCOMP 2026 family files."""

from __future__ import annotations

import dataclasses
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "subprojects" / "tlsf-tools" / "build-oxidd"
CAMPAIGN = HERE / "param-lift-campaign.py"
sys.path.insert(0, str(HERE))
from request import (  # noqa: E402
    CAPABILITIES,
    BindingDeclined,
    LoweringTools,
    bind_source_request,
)


class SourceRequestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tools = LoweringTools(
            BUILD / "tlsf2tlsf", BUILD / "tlsf2ltl", BUILD / "tlsfinfo"
        )

    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory(prefix="source-binding-test-")
        self.root = pathlib.Path(self._temporary.name)
        self.arbiter = ROOT / "tlsf-corpus" / "arbiter_pb_6_pe_.tlsf"

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def bind(
        self,
        path: pathlib.Path,
        family: str = "arbiter",
        target: int | None = None,
        capabilities=None,
    ):
        kwargs = {}
        if capabilities is not None:
            kwargs["capabilities"] = capabilities
        return bind_source_request(
            path,
            self.tools,
            "exact",
            family_hint=family,
            target_hint=target,
            **kwargs,
        )

    def mutated(self, old: str, new: str) -> pathlib.Path:
        path = self.root / self.arbiter.name
        source = self.arbiter.read_text(encoding="utf-8")
        self.assertIn(old, source)
        path.write_text(source.replace(old, new, 1), encoding="utf-8")
        return path

    def assert_declines(self, path: pathlib.Path, **kwargs) -> None:
        with self.assertRaises(BindingDeclined):
            self.bind(path, **kwargs)

    def test_real_corpus_instances_bind_by_content(self) -> None:
        cases = (
            ("arbiter", "arbiter_pb_6_pe_.tlsf"),
            ("prioritized_arbiter", "prioritized_arbiter_pb_6_pe_.tlsf"),
            ("load_balancer", "load_balancer_pb_6_pe_.tlsf"),
        )
        for family, name in cases:
            with self.subTest(family=family):
                request = self.bind(ROOT / "tlsf-corpus" / name, family)
                self.assertEqual(request.family, family)
                self.assertEqual(request.target_size, 6)
                self.assertEqual(
                    request.match_method, "content-verified-template-instantiation"
                )
                self.assertEqual(
                    len(request.io_mapping),
                    len(request.identity.inputs) + len(request.identity.outputs),
                )

    def test_same_filename_with_modified_guarantee_declines(self) -> None:
        path = self.mutated(
            "every_req_is_granted(r[i], g[i])", "G g[i]"
        )
        self.assertEqual(path.name, self.arbiter.name)
        self.assert_declines(path)

    def test_changed_parameter_cannot_use_requested_target(self) -> None:
        path = self.mutated("n = 6;", "n = 7;")
        corrected = self.bind(path)
        self.assertEqual(corrected.target_size, 7)
        self.assert_declines(path, target=6)

    def test_changed_target_semantics_declines(self) -> None:
        path = self.mutated("TARGET:      Mealy", "TARGET:      Moore")
        self.assert_declines(path)

    def test_changed_io_ownership_declines(self) -> None:
        path = self.mutated(
            "INPUTS  { r[n]; }\n  OUTPUTS { g[n]; }",
            "INPUTS  { g[n]; }\n  OUTPUTS { r[n]; }",
        )
        self.assert_declines(path)

    def test_substituted_template_declines(self) -> None:
        replacement = CAPABILITIES["load_balancer"].source_path
        capability = dataclasses.replace(
            CAPABILITIES["arbiter"],
            source=str(replacement),
            template_sha256=hashlib.sha256(replacement.read_bytes()).hexdigest(),
        )
        self.assert_declines(
            self.arbiter, capabilities={"arbiter": capability}
        )

    def test_duplicate_and_missing_ap_decline(self) -> None:
        duplicate = self.mutated(
            "INPUTS  { r[n]; }", "INPUTS  { r[n]; r[n]; }"
        )
        self.assert_declines(duplicate)
        missing = self.mutated("INPUTS  { r[n]; }", "INPUTS  { r[n - 1]; }")
        self.assert_declines(missing)

    def test_stale_capability_manifest_declines(self) -> None:
        stale = dataclasses.replace(
            CAPABILITIES["arbiter"], template_sha256="0" * 64
        )
        with self.assertRaisesRegex(BindingDeclined, "stale_capability_template"):
            self.bind(self.arbiter, capabilities={"arbiter": stale})

    def test_mutation_invalidates_bound_request(self) -> None:
        path = self.root / self.arbiter.name
        path.write_bytes(self.arbiter.read_bytes())
        request = self.bind(path)
        path.write_text(
            path.read_text(encoding="utf-8").replace(
                "every_req_is_granted(r[i], g[i])", "G g[i]", 1
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(BindingDeclined, "stale_source_binding"):
            request.validate_current()
        self.assert_declines(path)

    def test_n6_artifact_binding_does_not_license_n7(self) -> None:
        request6 = self.bind(self.arbiter)
        request7 = self.bind(
            ROOT / "tlsf-corpus" / "arbiter_pb_7_pe_.tlsf", target=7
        )
        binding6 = request6.artifact_binding()
        self.assertTrue(request6.accepts_artifact_binding(binding6))
        self.assertFalse(request7.accepts_artifact_binding(binding6))

    def test_exact_capability_discovers_either_checked_side(self) -> None:
        spec = importlib.util.spec_from_file_location("param_lift_campaign", CAMPAIGN)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        campaign = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = campaign
        spec.loader.exec_module(campaign)
        capability = CAPABILITIES["round_robin_arbiter_unreal2"]
        answer, side = campaign._certified_answer(
            capability,
            {"status": "realizable", "side": "system"},
            {"side": "system"},
        )
        self.assertEqual((answer, side), ("REALIZABLE", "system"))
        answer, side = campaign._certified_answer(
            capability,
            {
                "status": "unrealizable",
                "side": "environment",
                "reduction_semantics": "exact",
                "environment_counter_strategy_exported": True,
            },
            {"side": "environment", "reduction_semantics": "exact"},
        )
        self.assertEqual((answer, side), ("UNREALIZABLE", "environment"))
        with self.assertRaisesRegex(
            campaign.PipelineFailure, "one_sided_route_returned_unreal"
        ):
            campaign._certified_answer(
                CAPABILITIES["arbiter"],
                {
                    "status": "unrealizable",
                    "side": "environment",
                    "reduction_semantics": "exact",
                    "environment_counter_strategy_exported": True,
                },
                {"side": "environment", "reduction_semantics": "exact"},
            )

    def test_source_mode_never_opens_experiment_tables(self) -> None:
        evidence = self.root / "evidence.json"
        missing_generalizer = self.root / "missing-generalizer"
        proc = subprocess.run(
            [
                sys.executable,
                str(CAMPAIGN),
                "--request-mode", "source",
                "--family", "arbiter",
                "--target", "6",
                "-T", str(self.arbiter),
                "--instances", str(self.root / "must-not-open-instances.tsv"),
                "--census", str(self.root / "must-not-open-census.tsv"),
                "--generalizer", str(missing_generalizer),
                "--evidence-out", str(evidence),
                "--budget", "3",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=5,
        )
        self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
        payload = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(payload["request_mode"], "source")
        self.assertEqual(payload["request"]["route_kind"], "real-proposal")
        self.assertNotIn("status_120s", payload["request"])
        self.assertEqual(
            payload["source_binding"]["match"]["how"],
            "content-verified-template-instantiation",
        )

    def test_default_mode_keeps_historical_basename_reproducer(self) -> None:
        evidence = self.root / "reproducer-evidence.json"
        proc = subprocess.run(
            [
                sys.executable,
                str(CAMPAIGN),
                "--family", "arbiter",
                "--target", "6",
                "-T", str(self.arbiter),
                "--generalizer", str(self.root / "missing-generalizer"),
                "--evidence-out", str(evidence),
                "--budget", "3",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=5,
        )
        self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
        payload = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(payload["request_mode"], "reproducer")
        self.assertEqual(payload["request"]["status_120s"], "unsolved")
        self.assertEqual(
            payload["source_binding"]["match"],
            "basename-to-m0-instances-row",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
