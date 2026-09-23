"""Explicit tool and Python-binding configuration for the GR(1) drivers."""

from __future__ import annotations

import argparse
import importlib
import json
import os
import pathlib
import subprocess
import sys
import textwrap
from collections.abc import Mapping
from dataclasses import dataclass


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TLSF_TOOLS = ROOT / "subprojects" / "tlsf-tools"
DEFAULT_TLSF_TOOLS_BUILD = TLSF_TOOLS / "build-oxidd"
DEFAULT_BINDINGS_PYTHON = (
    pathlib.Path("/usr/bin/python3.13")
    if pathlib.Path("/usr/bin/python3.13").exists()
    else pathlib.Path(os.sys.executable)
)
DEFAULT_BINDINGS_SITE = pathlib.Path("/usr/local/lib64/python3.13/site-packages")

ENV_TLSF_TOOLS_BUILD = "ACACIA_TLSF_TOOLS_BUILD"
ENV_BINDINGS_PYTHON = "ACACIA_BINDINGS_PYTHON"
ENV_BINDINGS_SITE = "ACACIA_BINDINGS_SITE"

REQUIRED_BINDING_FUNCTIONS = (
    "bdd_compose",
    "bdd_exist",
    "bdd_forall",
    "bdd_high",
    "bdd_init",
    "bdd_isrunning",
    "bdd_ite",
    "bdd_ithvar",
    "bdd_low",
    "bdd_nithvar",
    "bdd_nodecount",
    "bdd_not",
    "bdd_setmaxincrease",
    "bdd_setvarnum",
    "bdd_simplify",
    "bdd_support",
    "bdd_var",
)
REQUIRED_BINDING_CONSTANTS = ("bddfalse", "bddtrue")


class ProbeError(RuntimeError):
    """A precise configuration failure found without starting benchmark work."""


@dataclass(frozen=True)
class ToolConfiguration:
    tlsf_tools_build: pathlib.Path
    bindings_python: pathlib.Path
    bindings_site: pathlib.Path

    @property
    def tlsf_tools_root(self) -> pathlib.Path:
        return self.tlsf_tools_build.parent

    @property
    def monitor(self) -> pathlib.Path:
        return self.tlsf_tools_root / "scripts" / "gr1_monitor_game.py"

    @property
    def solver(self) -> pathlib.Path:
        return self.tlsf_tools_build / "tlsfsolve"

    @property
    def checker(self) -> pathlib.Path:
        return self.tlsf_tools_build / "tlsfcertcheck"

    @property
    def tlsf2tlsf(self) -> pathlib.Path:
        return self.tlsf_tools_build / "tlsf2tlsf"

    @property
    def tlsf2ltl(self) -> pathlib.Path:
        return self.tlsf_tools_build / "tlsf2ltl"

    @property
    def tlsfinfo(self) -> pathlib.Path:
        return self.tlsf_tools_build / "tlsfinfo"


def configuration_defaults(
    environ: Mapping[str, str] | None = None,
) -> ToolConfiguration:
    env = os.environ if environ is None else environ
    return ToolConfiguration(
        pathlib.Path(env.get(ENV_TLSF_TOOLS_BUILD, DEFAULT_TLSF_TOOLS_BUILD)),
        pathlib.Path(env.get(ENV_BINDINGS_PYTHON, DEFAULT_BINDINGS_PYTHON)),
        pathlib.Path(env.get(ENV_BINDINGS_SITE, DEFAULT_BINDINGS_SITE)),
    )


def add_configuration_arguments(parser: argparse.ArgumentParser) -> None:
    defaults = configuration_defaults()
    parser.add_argument(
        "--tlsf-tools-build",
        type=pathlib.Path,
        default=defaults.tlsf_tools_build,
        metavar="DIR",
        help=f"tlsf-tools build directory (env: {ENV_TLSF_TOOLS_BUILD})",
    )
    parser.add_argument(
        "--bindings-python",
        type=pathlib.Path,
        default=defaults.bindings_python,
        metavar="PATH",
        help=f"Python interpreter for the BuDDy bindings (env: {ENV_BINDINGS_PYTHON})",
    )
    parser.add_argument(
        "--bindings-site",
        type=pathlib.Path,
        default=defaults.bindings_site,
        metavar="DIR",
        help=f"site-packages directory containing buddy (env: {ENV_BINDINGS_SITE})",
    )
    parser.add_argument(
        "--probe",
        action="store_true",
        help="validate and describe configured tools/bindings without benchmark work",
    )


def configuration_from_args(args: argparse.Namespace) -> ToolConfiguration:
    return ToolConfiguration(
        args.tlsf_tools_build.expanduser().resolve(),
        args.bindings_python.expanduser().resolve(),
        args.bindings_site.expanduser().resolve(),
    )


def bindings_environment(config: ToolConfiguration) -> dict[str, str]:
    env = dict(os.environ)
    site = str(config.bindings_site)
    env["PYTHONPATH"] = site + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    env["PYTHONNOUSERSITE"] = "1"
    return env


def _binding_module_path(
    module: object, name: str, bindings_site: pathlib.Path
) -> pathlib.Path:
    raw_path = getattr(module, "__file__", None)
    if not raw_path:
        raise ProbeError(f"{name} has no import origin")
    path = pathlib.Path(raw_path).resolve()
    try:
        path.relative_to(bindings_site)
    except ValueError as error:
        raise ProbeError(
            f"{name} resolved outside configured bindings site "
            f"{bindings_site}: {path}"
        ) from error
    return path


def load_buddy_bindings(
    bindings_site: pathlib.Path,
) -> tuple[object, object, pathlib.Path, pathlib.Path]:
    """Import buddy and its extension only from the configured site directory."""
    site = bindings_site.expanduser().resolve()
    if not site.is_dir():
        raise ProbeError(f"bindings site directory not found: {site}")
    site_text = str(site)
    if site_text in sys.path:
        sys.path.remove(site_text)
    sys.path.insert(0, site_text)
    try:
        buddy = importlib.import_module("buddy")
        extension = importlib.import_module("_buddy")
    except ImportError as error:
        raise ProbeError(f"cannot import buddy/_buddy from {site}: {error}") from error
    binding_path = _binding_module_path(buddy, "buddy", site)
    extension_path = _binding_module_path(extension, "_buddy", site)
    return buddy, extension, binding_path, extension_path


def inspect_buddy_bindings(bindings_site: pathlib.Path) -> dict[str, object]:
    buddy, _extension, binding_path, extension_path = load_buddy_bindings(
        bindings_site
    )
    missing = [
        name
        for name in REQUIRED_BINDING_FUNCTIONS + REQUIRED_BINDING_CONSTANTS
        if not hasattr(buddy, name)
    ]
    if missing:
        raise ProbeError("buddy missing required API: " + ", ".join(missing))
    loaded = []
    maps = pathlib.Path("/proc/self/maps")
    if maps.is_file():
        for line in maps.read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if fields and fields[-1].startswith("/") and "bdd" in fields[-1].lower():
                loaded.append(fields[-1])
    return {
        "binding_module": str(binding_path),
        "extension_module": str(extension_path),
        "buddy_version_number": buddy.bdd_versionnum(),
        "buddy_version_string": buddy.bdd_versionstr(),
        "loaded_bdd_objects": sorted(set(loaded)),
        "python_executable": sys.executable,
        "python_version": sys.version,
        "required_functions": REQUIRED_BINDING_FUNCTIONS,
        "required_constants": REQUIRED_BINDING_CONSTANTS,
    }


def _require_file(path: pathlib.Path, description: str, executable: bool = False) -> None:
    if not path.is_file():
        raise ProbeError(f"{description} not found: {path}")
    if executable and not os.access(path, os.X_OK):
        raise ProbeError(f"{description} is not executable: {path}")


def _binary_version(path: pathlib.Path, description: str) -> str:
    try:
        result = subprocess.run(
            [str(path), "--version"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ProbeError(f"{description} version probe failed: {error}") from error
    output = (result.stdout or result.stderr).strip()
    if result.returncode != 0:
        detail = output or "no diagnostic"
        raise ProbeError(
            f"{description} --version exited {result.returncode}: {detail}"
        )
    if not output:
        raise ProbeError(f"{description} --version produced no version string")
    return output


def _probe_bindings(config: ToolConfiguration) -> dict[str, object]:
    script = textwrap.dedent(
        """
        import json
        import pathlib
        import sys

        sys.path.insert(0, sys.argv[1])
        from tool_config import ProbeError, inspect_buddy_bindings

        try:
            payload = inspect_buddy_bindings(pathlib.Path(sys.argv[2]))
        except ProbeError as error:
            print(error, file=sys.stderr)
            raise SystemExit(2) from error
        print(json.dumps(payload, sort_keys=True))
        """
    )
    try:
        result = subprocess.run(
            [
                str(config.bindings_python),
                "-s",
                "-c",
                script,
                str(HERE),
                str(config.bindings_site),
            ],
            env=bindings_environment(config),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ProbeError(f"BuDDy bindings probe failed: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip() or "no diagnostic"
        raise ProbeError(f"BuDDy bindings import/API probe failed: {detail}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ProbeError(f"BuDDy bindings probe returned invalid JSON: {error}") from error
    if not isinstance(payload, dict):
        raise ProbeError("BuDDy bindings probe returned a non-object JSON value")
    return payload


def probe_configuration(
    config: ToolConfiguration,
    *,
    monitor: pathlib.Path | None = None,
    solver: pathlib.Path | None = None,
    checker: pathlib.Path | None = None,
) -> dict[str, object]:
    effective_monitor = (monitor or config.monitor).expanduser().resolve()
    effective_solver = (solver or config.solver).expanduser().resolve()
    effective_checker = (checker or config.checker).expanduser().resolve()
    _require_file(effective_solver, "tlsfsolve", executable=True)
    _require_file(effective_checker, "tlsfcertcheck", executable=True)
    _require_file(effective_monitor, "gr1_monitor_game.py")
    _require_file(config.bindings_python, "bindings Python", executable=True)
    if not config.bindings_site.is_dir():
        raise ProbeError(f"bindings site directory not found: {config.bindings_site}")
    return {
        "tlsf_tools_build": str(config.tlsf_tools_build),
        "tools": {
            "gr1_monitor_game": str(effective_monitor),
            "tlsfsolve": {
                "path": str(effective_solver),
                "version": _binary_version(effective_solver, "tlsfsolve"),
            },
            "tlsfcertcheck": {
                "path": str(effective_checker),
                "version": _binary_version(effective_checker, "tlsfcertcheck"),
            },
        },
        "bindings_site": str(config.bindings_site),
        "bindings": _probe_bindings(config),
    }


def print_probe(
    config: ToolConfiguration,
    *,
    monitor: pathlib.Path | None = None,
    solver: pathlib.Path | None = None,
    checker: pathlib.Path | None = None,
) -> int:
    try:
        result = probe_configuration(
            config, monitor=monitor, solver=solver, checker=checker
        )
    except ProbeError as error:
        print(f"probe: {error}", file=os.sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0
