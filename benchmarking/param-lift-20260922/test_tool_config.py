#!/usr/bin/env python3
"""Unit tests for explicit GR(1) campaign tool configuration and probing."""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GENERALIZER = HERE / "generalize_gr1.py"
CAMPAIGN = HERE / "param-lift-campaign.py"
sys.path.insert(0, str(HERE))
import tool_config  # noqa: E402


class ToolConfigurationTest(unittest.TestCase):
    def test_unset_environment_reproduces_historical_defaults(self) -> None:
        config = tool_config.configuration_defaults({})
        self.assertEqual(
            config.tlsf_tools_build,
            ROOT / "subprojects" / "tlsf-tools" / "build-oxidd",
        )
        expected_python = pathlib.Path("/usr/bin/python3.13")
        if not expected_python.exists():
            expected_python = pathlib.Path(sys.executable)
        self.assertEqual(config.bindings_python, expected_python)
        self.assertEqual(
            config.bindings_site,
            pathlib.Path("/usr/local/lib64/python3.13/site-packages"),
        )
        self.assertIsNone(config.buddy_adapter)

    def test_environment_fallbacks_are_used(self) -> None:
        config = tool_config.configuration_defaults({
            tool_config.ENV_TLSF_TOOLS_BUILD: "/configured/build",
            tool_config.ENV_BINDINGS_PYTHON: "/configured/python",
            tool_config.ENV_BINDINGS_SITE: "/configured/site",
            tool_config.ENV_BUDDY_ADAPTER: "/configured/adapter.so",
        })
        self.assertEqual(config.tlsf_tools_build, pathlib.Path("/configured/build"))
        self.assertEqual(config.bindings_python, pathlib.Path("/configured/python"))
        self.assertEqual(config.bindings_site, pathlib.Path("/configured/site"))
        self.assertEqual(
            config.buddy_adapter, pathlib.Path("/configured/adapter.so")
        )

    def test_flags_override_environment(self) -> None:
        with mock.patch.dict(os.environ, {
            tool_config.ENV_TLSF_TOOLS_BUILD: "/environment/build",
            tool_config.ENV_BINDINGS_PYTHON: "/environment/python",
            tool_config.ENV_BINDINGS_SITE: "/environment/site",
            tool_config.ENV_BUDDY_ADAPTER: "/environment/adapter.so",
        }, clear=False):
            parser = argparse.ArgumentParser()
            tool_config.add_configuration_arguments(parser)
            args = parser.parse_args([
                "--tlsf-tools-build", "/flag/build",
                "--bindings-python", "/flag/python",
                "--bindings-site", "/flag/site",
                "--buddy-adapter", "/flag/adapter.so",
            ])
        config = tool_config.configuration_from_args(args)
        self.assertEqual(config.tlsf_tools_build, pathlib.Path("/flag/build"))
        self.assertEqual(config.bindings_python, pathlib.Path("/flag/python"))
        self.assertEqual(config.bindings_site, pathlib.Path("/flag/site"))
        self.assertEqual(config.buddy_adapter, pathlib.Path("/flag/adapter.so"))

    def test_missing_binary_has_precise_error(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tool-config-missing-") as temporary:
            root = pathlib.Path(temporary)
            config = tool_config.ToolConfiguration(
                root / "build", pathlib.Path(sys.executable), root / "site"
            )
            with self.assertRaisesRegex(
                tool_config.ProbeError,
                rf"tlsfsolve not found: {config.solver}",
            ):
                tool_config.probe_configuration(config)

    def test_probe_only_requests_tool_versions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tool-config-probe-") as temporary:
            root = pathlib.Path(temporary)
            build = root / "tlsf-tools" / "build-L0"
            scripts = build.parent / "scripts"
            site = root / "site"
            build.mkdir(parents=True)
            scripts.mkdir()
            site.mkdir()
            (scripts / "gr1_monitor_game.py").write_text("# test fixture\n", encoding="utf-8")
            config = tool_config.ToolConfiguration(build, pathlib.Path(sys.executable), site)
            calls: list[tuple[pathlib.Path, str]] = []

            def version(path: pathlib.Path, description: str) -> str:
                calls.append((path, description))
                return f"{description} test-version"

            with (
                mock.patch.object(tool_config, "_binary_version", side_effect=version),
                mock.patch.object(tool_config, "_probe_bindings", return_value={"api": "ok"}),
                mock.patch.object(pathlib.Path, "is_file", return_value=True),
                mock.patch.object(os, "access", return_value=True),
            ):
                result = tool_config.probe_configuration(config)
            self.assertEqual(
                calls,
                [(config.solver, "tlsfsolve"), (config.checker, "tlsfcertcheck")],
            )
            self.assertEqual(result["bindings"], {"api": "ok"})
            self.assertIsNone(result["buddy_adapter"])

    def test_probe_passes_configured_adapter_to_binding_probe(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tool-config-adapter-") as temporary:
            root = pathlib.Path(temporary)
            build = root / "tlsf-tools" / "build-L0"
            scripts = build.parent / "scripts"
            site = root / "site"
            adapter = root / "adapter.so"
            build.mkdir(parents=True)
            scripts.mkdir()
            site.mkdir()
            config = tool_config.ToolConfiguration(
                build, pathlib.Path(sys.executable), site, adapter
            )
            with (
                mock.patch.object(tool_config, "_binary_version", return_value="v1"),
                mock.patch.object(
                    tool_config,
                    "_probe_bindings",
                    return_value={"adapter": {"path": str(adapter)}},
                ) as probe,
                mock.patch.object(pathlib.Path, "is_file", return_value=True),
                mock.patch.object(os, "access", return_value=True),
            ):
                result = tool_config.probe_configuration(config)
            probe.assert_called_once_with(config)
            self.assertEqual(result["buddy_adapter"], str(adapter))
            self.assertEqual(result["bindings"]["adapter"]["path"], str(adapter))

    def test_probe_rejects_bindings_outside_configured_site(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tool-config-bindings-") as temporary:
            root = pathlib.Path(temporary)
            fallback = root / "fallback"
            fallback.mkdir()
            buddy_fixture = """
bddfalse = 0
bddtrue = 1
def __getattr__(name):
    if name == "bdd_versionnum":
        return lambda: 23
    if name == "bdd_versionstr":
        return lambda: "BuDDy test binding"
    return lambda *args, **kwargs: None
""".lstrip()
            (fallback / "buddy.py").write_text(buddy_fixture, encoding="utf-8")
            (fallback / "_buddy.py").write_text("# test fixture\n", encoding="utf-8")
            cases = (
                ("buddy", r"buddy resolved outside.*fallback/buddy.py"),
                ("_buddy", r"_buddy resolved outside.*fallback/_buddy.py"),
            )
            for outside_module, message in cases:
                with self.subTest(outside_module=outside_module):
                    configured = root / f"configured-{outside_module}"
                    configured.mkdir()
                    if outside_module == "_buddy":
                        (configured / "buddy.py").write_text(
                            buddy_fixture, encoding="utf-8"
                        )
                    config = tool_config.ToolConfiguration(
                        root / "build", pathlib.Path(sys.executable), configured
                    )
                    with (
                        mock.patch.dict(
                            os.environ, {"PYTHONPATH": str(fallback)}, clear=False
                        ),
                        self.assertRaisesRegex(tool_config.ProbeError, message),
                    ):
                        tool_config._probe_bindings(config)
                    self.assertEqual(
                        tool_config.bindings_environment(config)["PYTHONNOUSERSITE"],
                        "1",
                    )

    def test_probe_honors_missing_solver_override_at_both_entry_points(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tool-config-override-") as temporary:
            missing = pathlib.Path(temporary) / "missing-solver"
            for entry_point in (GENERALIZER, CAMPAIGN):
                with self.subTest(entry_point=entry_point.name):
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(entry_point),
                            "--probe",
                            "--solver",
                            str(missing),
                        ],
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        check=False,
                        timeout=10,
                    )
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn(f"tlsfsolve not found: {missing}", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
