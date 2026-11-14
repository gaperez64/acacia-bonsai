#!/usr/bin/env python3
"""Shared Meson compile profiles and conservative build-directory reuse identity."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PROFILES = {
    "normal": ["--buildtype=release", "-Doptimization=3", "-Db_lto=true", "-Ddebug=false",
               "-Db_ndebug=true", "-Dacacia_compiler_profile=release"],
    "lowmem": ["--buildtype=plain", "-Doptimization=0", "-Ddebug=false", "-Db_lto=false",
               "-Db_ndebug=true", "-Dacacia_compiler_profile=lowmem"],
    "checked": ["--buildtype=debugoptimized", "-Ddebug=false", "-Db_lto=false",
                "-Dacacia_compiler_profile=debug"],
    "debug": [],
}
# Meson consumes some of these at setup time; GCC/Clang also read search paths
# on every invocation. Introspection alone still describes the old environment.
BUILD_ENV = (
    "CC", "CXX", "AR", "LD", "CC_LD", "CXX_LD", "CFLAGS", "CXXFLAGS", "CPPFLAGS",
    "LDFLAGS", "PKG_CONFIG", "PKG_CONFIG_PATH", "PKG_CONFIG_LIBDIR", "PATH",
    "RUSTC", "RUSTFLAGS", "CARGO", "CARGO_BUILD_TARGET", "CMAKE_PREFIX_PATH",
    "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
    "OBJCPLUS_INCLUDE_PATH", "LIBRARY_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX",
    "SDKROOT", "DEVELOPER_DIR", "INCLUDE", "LIB", "LIBPATH",
    "CCC_OVERRIDE_OPTIONS", "CCC_ADD_ARGS",
)


def file_identity(path):
    path = Path(path).resolve(strict=True)
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def command_identity(command):
    """Include wrappers, compilers and linkers, with their current binary bytes."""
    result = []
    for argument in command:
        executable = shutil.which(argument)
        if executable:
            result.append(file_identity(executable))
    if not result:
        raise ValueError(f"cannot resolve toolchain command: {command!r}")
    return {"command": command, "executables": result}


def effective_linker_identity(compiler, build_dir, arguments=()):
    """Meson's linker_exelist is a GCC/Clang driver, not the selected ld."""
    command = compiler.get("linker_exelist", compiler["exelist"])
    program = "ld"
    explicit_path = None
    for argument in [*command, *arguments]:
        if argument.startswith("-fuse-ld="):
            selection = argument.split("=", 1)[1]
            program = selection if os.path.dirname(selection) else f"ld.{selection}"
        elif argument.startswith("--ld-path="):
            explicit_path = argument.split("=", 1)[1]
    # Clang's -print-prog-name=ld ignores -fuse-ld. Query the selected name
    # explicitly; absolute linker overrides already identify the file directly.
    program = explicit_path or program
    if os.path.dirname(program):
        return file_identity(build_dir / program)
    linker = subprocess.check_output(
        [*command, *arguments, f"-print-prog-name={program}"],
        cwd=build_dir, text=True).strip()
    if not linker or "\n" in linker:
        raise ValueError(f"cannot identify effective linker: {command!r}")
    # Drivers can return an absolute path, a path relative to the build directory
    # (e.g. from -B), or a bare name to be searched on PATH. Fail closed if absent.
    search_path = os.pathsep.join(str((build_dir / part).resolve()) for part in os.get_exec_path())
    executable = (build_dir / linker if os.path.dirname(linker)
                  else shutil.which(linker, path=search_path))
    if not executable:
        raise ValueError(f"cannot resolve effective linker: {linker!r}")
    return file_identity(executable)


def build_identity(preset, profile, build_dir, native_file=None):
    info = build_dir / "meson-info"
    compilers = json.loads((info / "intro-compilers.json").read_text())
    # Target introspection includes Meson's final linker-selection arguments,
    # including native-file *_ld overrides absent from intro-compilers.json.
    target_info = info / "intro-targets.json"
    linker_args = {}
    for target in json.loads(target_info.read_text()) if target_info.exists() else []:
        for source in target.get("target_sources", []):
            if "linker" in source:
                linker_args.setdefault(tuple(source["linker"]), set()).add(
                    tuple(source["parameters"]))
    toolchains = {}
    for machine, languages in compilers.items():
        toolchains[machine] = {}
        for language, compiler in languages.items():
            toolchains[machine][language] = {
                key: command_identity(value) if key in {"exelist", "linker_exelist"} else value
                for key, value in compiler.items()
            }
            if compiler.get("id") in {"gcc", "clang", "apple-clang"}:
                command = tuple(compiler.get("linker_exelist", compiler["exelist"]))
                linkers = {}
                for arguments in linker_args.get(command, [()]):
                    linker = effective_linker_identity(compiler, build_dir, arguments)
                    linkers[linker["path"]] = linker
                toolchains[machine][language]["effective_linkers"] = [
                    linkers[path] for path in sorted(linkers)]
    helper = [sys.executable, os.environ.get("ACACIA_CONFIG_HELPER",
                                          str(ROOT / "scripts/acacia-config.py"))]
    options = json.loads(subprocess.check_output([*helper, "show", preset], text=True))
    # Native CPU flags depend on this machine even if the flags themselves match.
    cpu = Path("/proc/cpuinfo")
    cpu_features = sorted({line for line in cpu.read_text().splitlines()
                           if line.startswith(("model name", "flags", "Features"))}) if cpu.exists() else []
    return {
        "schema": 1,
        "source": str(ROOT),
        "acacia": options,
        "profile": profile,
        "setup_args": PROFILES[profile],
        "native_file": file_identity(native_file) if native_file else None,
        "environment": {name: os.environ.get(name) for name in BUILD_ENV},
        "machine": {"system": platform.system(), "machine": platform.machine(),
                    "cpu": cpu_features},
        "toolchains": toolchains,
        "meson_options": {option["name"]: option["value"] for option in json.loads(
            (info / "intro-buildoptions.json").read_text())},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    profile = sub.add_parser("profile-args")
    profile.add_argument("profile", choices=PROFILES)
    identity = sub.add_parser("identity")
    identity.add_argument("preset")
    identity.add_argument("--profile", choices=PROFILES, required=True)
    identity.add_argument("--build-dir", type=Path, required=True)
    identity.add_argument("--native-file", type=Path)
    args = parser.parse_args()
    if args.command == "profile-args":
        print(shlex.join(PROFILES[args.profile]))
    else:
        try:
            value = build_identity(args.preset, args.profile, args.build_dir, args.native_file)
        except (OSError, ValueError, subprocess.CalledProcessError) as exc:
            parser.exit(2, f"cannot identify Acacia build: {exc}\n")
        print(json.dumps(value, sort_keys=True, indent=2))


if __name__ == "__main__":
    main()
