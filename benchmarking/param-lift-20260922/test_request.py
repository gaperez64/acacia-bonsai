#!/usr/bin/env python3
"""Fast source-binding regressions using real SYNTCOMP 2026 family files."""

from __future__ import annotations

import dataclasses
import hashlib
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "subprojects" / "tlsf-tools" / "build-oxidd"
CAMPAIGN = HERE / "param-lift-campaign.py"
sys.path.insert(0, str(HERE))
import request as request_module  # noqa: E402
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

    def test_non_parametric_source_declines_after_one_tool_call(self) -> None:
        source = self.root / "plain.tlsf"
        source.write_text("INFO { TITLE: plain }\n", encoding="utf-8")
        with mock.patch.object(
            request_module, "_run_tool", return_value=""
        ) as run_tool:
            with self.assertRaisesRegex(
                BindingDeclined, "unsupported_parameter_signature"
            ):
                bind_source_request(source, self.tools, "exact")
        self.assertEqual(run_tool.call_count, 1)
        self.assertEqual(run_tool.call_args.args[0][1:], ["--parameters"])

    def test_parameter_bucket_skips_other_template_signatures(self) -> None:
        unrelated = dataclasses.replace(
            CAPABILITIES["arbiter"],
            family="unrelated_m",
            source=str(self.root / "must-not-be-read.tlsf"),
            parameters=("m",),
        )
        request = self.bind(
            self.arbiter,
            family=None,
            capabilities={
                "arbiter": CAPABILITIES["arbiter"],
                "unrelated_m": unrelated,
            },
        )
        self.assertEqual(request.family, "arbiter")

    def test_unrelated_n_parametric_source_instantiates_only_n_bucket(self) -> None:
        unrelated_source = self.mutated(
            "every_req_is_granted(r[i], g[i])", "G g[i]"
        )
        unrelated_m = dataclasses.replace(
            CAPABILITIES["arbiter"],
            family="unrelated_m",
            source=str(self.root / "must-not-be-read.tlsf"),
            parameters=("m",),
        )
        arbiter_template = CAPABILITIES["arbiter"].source_path.read_text(
            encoding="utf-8")
        instantiated: list[str] = []
        identity = request_module._identity

        def record_identity(tools, source, parameters, deadline):
            if source == arbiter_template:
                instantiated.append("arbiter")
            return identity(tools, source, parameters, deadline)

        with mock.patch.object(
            request_module, "_identity", side_effect=record_identity
        ):
            with self.assertRaisesRegex(
                BindingDeclined, "source_not_content_verified_for_capability"
            ):
                bind_source_request(
                    unrelated_source,
                    self.tools,
                    "exact",
                    capabilities={
                        "arbiter": CAPABILITIES["arbiter"],
                        "unrelated_m": unrelated_m,
                    },
                )
        self.assertEqual(instantiated, ["arbiter"])

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
            CAPABILITIES["arbiter"],
            {"status": "realizable", "side": "system"},
            None,
        )
        self.assertEqual((answer, side), ("REALIZABLE", "system"))
        region_result = campaign.PipelineResult(
            "REALIZABLE", "target_check", "target_verified",
            self.root / "certificate.aag", None, "gr1-region-v1",
        )
        self.assertIsNone(region_result.policy)
        with self.assertRaisesRegex(ValueError, "only establish REALIZABLE"):
            campaign.PipelineResult(
                "UNREALIZABLE", "target_check", "target_verified",
                self.root / "certificate.aag", None, "gr1-region-v1",
            )
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

    def test_source_mode_deadline_covers_lowering_and_reaps_descendants(self) -> None:
        real_build = self.tools.tlsfinfo.parent
        fake_build = self.root / "slow-lowering-build"
        fake_build.mkdir()
        wrapper = """#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import time

name = pathlib.Path(sys.argv[0]).name
mode = os.environ["SLOW_LOWERING_MODE"]
first = name == "tlsfinfo" and sys.argv[1:] == ["--parameters"]
later = name == "tlsf2tlsf" and "--basic" not in sys.argv[1:]
if (mode == "first" and first) or (mode == "later" and later):
    child = subprocess.Popen([
        sys.executable, "-c", "import time; time.sleep(30)"
    ])
    pathlib.Path(os.environ["SLOW_CHILD_PID"]).write_text(
        str(child.pid), encoding="utf-8"
    )
    time.sleep(30)
real = pathlib.Path(os.environ["REAL_LOWERING_BUILD"]) / name
os.execv(str(real), [str(real), *sys.argv[1:]])
"""
        for name in ("tlsfinfo", "tlsf2tlsf", "tlsf2ltl"):
            tool = fake_build / name
            tool.write_text(wrapper, encoding="utf-8")
            tool.chmod(0o755)

        for mode in ("first", "later"):
            with self.subTest(mode=mode):
                child_pid = self.root / f"{mode}-child.pid"
                evidence = self.root / f"{mode}-deadline-evidence.json"
                env = dict(os.environ)
                env.update({
                    "REAL_LOWERING_BUILD": str(real_build),
                    "SLOW_LOWERING_MODE": mode,
                    "SLOW_CHILD_PID": str(child_pid),
                })
                started = time.monotonic()
                proc = subprocess.run(
                    [
                        sys.executable,
                        str(CAMPAIGN),
                        "--request-mode", "source",
                        "--family", "arbiter",
                        "--target", "6",
                        "-T", str(self.arbiter),
                        "--tlsf-tools-build", str(fake_build),
                        "--generalizer", str(self.root / "must-not-run"),
                        "--evidence-out", str(evidence),
                        "--budget", "0.35",
                    ],
                    cwd=ROOT,
                    env=env,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                    timeout=4,
                )
                elapsed = time.monotonic() - started
                self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
                self.assertLess(elapsed, 1.5, proc.stdout + proc.stderr)
                self.assertIn(
                    "UNKNOWN source_binding absolute_deadline_exhausted",
                    proc.stdout,
                )
                self.assertTrue(child_pid.is_file(), proc.stdout + proc.stderr)
                pid = int(child_pid.read_text(encoding="utf-8"))
                process_path = pathlib.Path(f"/proc/{pid}")
                cleanup_deadline = time.monotonic() + 1.0
                while process_path.exists() and time.monotonic() < cleanup_deadline:
                    time.sleep(0.01)
                self.assertFalse(
                    process_path.exists(),
                    f"lowering descendant {pid} survived {mode} timeout",
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
