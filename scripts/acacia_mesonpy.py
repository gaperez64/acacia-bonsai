"""Use the first shipped preset for meson-python builds, including sdists."""

from pathlib import Path
import shlex
import subprocess
import sys

import mesonpy
from mesonpy import (  # noqa: F401 -- PEP 517/660 hooks need no Meson setup arguments
    get_requires_for_build_editable,
    get_requires_for_build_sdist,
    get_requires_for_build_wheel,
)


def preset_settings(config_settings=None):
    settings = dict(config_settings or {})
    helper = [sys.executable, str(Path(__file__).with_name("acacia-config.py"))]
    first = subprocess.check_output(
        [*helper, "list-group", "docker_default"], text=True,
    ).splitlines()[0]
    preset_args = shlex.split(subprocess.check_output(
        [*helper, "meson-args", first], text=True,
    ))
    extra = settings.get("setup-args", [])
    if isinstance(extra, str):
        extra = [extra]
    # Meson's release buildtype still supplies -O3. The Acacia release profile
    # adds -march=native; keep the compiler target baseline for portable wheels.
    # Pin tlsf-tools too: its CPU option can independently enable ISA extensions.
    # Thus the preset's native SIMD width also resolves to that baseline ABI.
    settings["setup-args"] = [
        *preset_args, *extra, "-Dacacia_compiler_profile=debug", "-Dtlsf-tools:cpu=baseline",
    ]
    return settings


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    return mesonpy.build_wheel(
        wheel_directory, preset_settings(config_settings), metadata_directory,
    )


def build_sdist(sdist_directory, config_settings=None):
    return mesonpy.build_sdist(sdist_directory, preset_settings(config_settings))


def build_editable(wheel_directory, config_settings=None, metadata_directory=None):
    return mesonpy.build_editable(
        wheel_directory, preset_settings(config_settings), metadata_directory,
    )
