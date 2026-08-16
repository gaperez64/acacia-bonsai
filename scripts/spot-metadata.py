#!/usr/bin/env python3
"""Print the installed Spot version and acceptance-set limit as key/value metadata."""

from __future__ import annotations

import os
import pathlib
import re
import subprocess


def pkg_config_env() -> dict[str, str]:
    env = os.environ.copy()
    paths = [p for p in env.get("PKG_CONFIG_PATH", "").split(os.pathsep) if p]
    for candidate in ("/usr/local/lib/pkgconfig", "/usr/local/lib64/pkgconfig"):
        if candidate not in paths:
            paths.append(candidate)
    env["PKG_CONFIG_PATH"] = os.pathsep.join(paths)
    return env


def pkg_config(variable: str) -> str | None:
    cmd = ["pkg-config"]
    if variable == "version":
        cmd.append("--modversion")
    else:
        cmd.append(f"--variable={variable}")
    cmd.append("libspot")
    try:
        return subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
            env=pkg_config_env(),
        ).stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None


def header_metadata(header: pathlib.Path) -> tuple[str | None, str | None]:
    try:
        text = header.read_text()
    except OSError:
        return None, None
    version = re.search(r'^#define SPOT_PACKAGE_VERSION "([^"]+)"', text, re.MULTILINE)
    accsets = re.search(r"^#define SPOT_MAX_ACCSETS\s+(\d+)", text, re.MULTILINE)
    return (version.group(1) if version else None, accsets.group(1) if accsets else None)


def main() -> int:
    version = pkg_config("version")
    includedir = pkg_config("includedir")
    candidates = []
    if includedir:
        candidates.append(pathlib.Path(includedir) / "spot/misc/_config.h")
    candidates.extend(
        [
            pathlib.Path("/usr/local/include/spot/misc/_config.h"),
            pathlib.Path("/usr/include/spot/misc/_config.h"),
        ]
    )

    header_version = accsets = None
    for candidate in candidates:
        header_version, accsets = header_metadata(candidate)
        if header_version or accsets:
            break

    print(f"spot_version={version or header_version or 'unknown'}")
    print(f"spot_max_accsets={accsets or 'unknown'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
