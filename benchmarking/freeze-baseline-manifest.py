#!/usr/bin/env python3
"""Freeze the observable identity of a campaign baseline.

This records the current checkout, requested builds and presets, and measurement
environment; it does not rebuild binaries or infer their source revision from
the current HEAD. Missing metadata remains explicitly unavailable. Relative
build, harness and output paths are relative to the caller's working directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shlex
import subprocess
import sys
from datetime import datetime, timezone

import benchlib


def unavailable(reason: str) -> str:
    return f"unavailable: {reason}"


def collection_error(context: str, error: Exception) -> str:
    detail = str(error).splitlines()
    return unavailable(f"{context}: {type(error).__name__}"
                       + (f": {detail[0]}" if detail else ""))


def command(argv: list[str], *, env=None) -> str:
    """Keep command failures as data, including on minimally provisioned hosts."""
    try:
        result = subprocess.run(
            argv, cwd=benchlib.ROOT, env=env, capture_output=True, text=True,
            timeout=15, check=False,
        )
    except Exception as error:
        return collection_error(argv[0], error)
    if result.returncode:
        detail = result.stderr.strip().splitlines()
        return unavailable(f"{argv[0]} exited {result.returncode}"
                           + (f": {detail[0]}" if detail else ""))
    return result.stdout


def identity(argv: list[str], *, env=None) -> str:
    return command(argv, env=env).strip() or unavailable(f"{argv[0]} returned no output")


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except Exception as error:
        return collection_error(str(path), error)


def json_object(raw: str) -> dict | str:
    if raw.startswith("unavailable:"):
        return raw
    try:
        value = json.loads(raw)
    except Exception as error:
        return collection_error("invalid JSON metadata", error)
    return value if isinstance(value, dict) else unavailable("metadata is not a JSON object")


def sha256(path: pathlib.Path) -> str:
    try:
        with path.open("rb") as handle:
            return hashlib.file_digest(handle, "sha256").hexdigest()
    except Exception as error:
        return collection_error(str(path), error)


def git_metadata() -> dict:
    status = command(["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"])
    dirty_paths = status
    if not status.startswith("unavailable:"):
        try:
            paths = set()
            entries = iter(status.split("\0"))
            for entry in entries:
                if not entry:
                    continue
                paths.add(entry[3:])
                if "R" in entry[:2] or "C" in entry[:2]:
                    paths.add(next(entries))  # porcelain -z includes the original path too
            dirty_paths = sorted(paths)
        except Exception as error:
            dirty_paths = collection_error("git status", error)
    submodule_status = command(["git", "submodule", "status", "--recursive"])
    submodules = submodule_status
    if not submodule_status.startswith("unavailable:"):
        submodules = []
        for line in submodule_status.splitlines():
            try:
                sha, path = line[1:].split(" ", 1)
                path = path.rsplit(" (", 1)[0]
                state = {" ": "matches index", "+": "differs from index",
                         "-": "uninitialized", "U": "conflicted"}.get(
                             line[0], unavailable("unrecognized submodule status"))
                submodules.append({
                    "path": path,
                    "sha": sha if line[0] in " +" else unavailable(state),
                    "status": state,
                    "status_line": line,
                })
            except Exception as error:
                submodules.append({"status_line": line,
                                   "status": collection_error("git submodule status", error)})
    branch = command(["git", "symbolic-ref", "--quiet", "--short", "HEAD"]).strip()
    if branch == "unavailable: git exited 1":
        branch = unavailable("detached HEAD")
    return {
        "head": identity(["git", "rev-parse", "HEAD"]),
        "branch": branch or unavailable("empty branch name"),
        "dirty": bool(dirty_paths) if isinstance(dirty_paths, list) else dirty_paths,
        "dirty_paths": dirty_paths,
        "submodules": submodules,
    }


def corpus_metadata(build_dir=None) -> dict:
    reason = unavailable("no TLSF corpus resolved by benchlib.tlsf_corpus_dir")
    try:
        corpus = benchlib.tlsf_corpus_dir(build_dir=build_dir)
    except Exception as error:
        corpus = None
        reason = collection_error("TLSF corpus resolution", error)
    if corpus is None:
        return {"directory": reason, "marker_path": reason, "marker_contents": reason,
                "entries": reason, "manifest_sha256": reason}
    marker = corpus / benchlib.CORPUS_MARKER
    contents = read_text(marker)
    metadata = json_object(contents)
    fields = {}
    for key in ("entries", "manifest_sha256"):
        if isinstance(metadata, dict):
            value = metadata.get(key)
            fields[key] = value if value is not None else unavailable(f"marker lacks {key}")
        else:
            fields[key] = metadata
    return {
        "directory": str(corpus),
        "marker_path": str(marker),
        "marker_contents": contents,
        **fields,
    }


def build_metadata(build: pathlib.Path) -> dict:
    try:
        build = build.expanduser().resolve()
        directory = str(build)
    except Exception as error:
        directory = collection_error(str(build), error)
    binary = build / "src/acacia-bonsai"
    binary_absent = False
    try:
        stat = binary.stat()
        mtime_ns = stat.st_mtime_ns
    except Exception as error:
        binary_absent = isinstance(error, FileNotFoundError)
        mtime_ns = collection_error(str(binary), error)
    mtime = mtime_ns
    if isinstance(mtime_ns, int):
        try:
            mtime = datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat()
        except Exception as error:
            mtime = collection_error("binary mtime", error)
    binary_hash = sha256(binary)
    options = {}
    for name in ("buildtype", "optimization", "b_lto"):
        try:
            value = benchlib.build_option(build, name)
        except Exception as error:
            value = collection_error(f"Meson option {name}", error)
        options[name] = value if value is not None else unavailable(
            f"{name} missing or unreadable in meson-info/intro-buildoptions.json")
    # Meson also records acacia options when self-benchmark.sh's config file is
    # absent. Preserve both records so a stale .acacia-config.json stays visible.
    try:
        intro = json.loads((build / "meson-info/intro-buildoptions.json").read_text())
        if not isinstance(intro, list):
            raise ValueError("expected a list of Meson build options")
        acacia_names = sorted({option["name"] for option in intro if isinstance(option, dict)
                               and str(option.get("name", "")).startswith("acacia_")})
        meson_acacia = {name: benchlib.build_option(build, name) for name in acacia_names}
        meson_acacia = {name: value if value is not None else unavailable(f"{name} lacks a value")
                        for name, value in meson_acacia.items()}
        if not meson_acacia:
            meson_acacia = unavailable("no acacia options in Meson metadata")
    except Exception as error:
        meson_acacia = collection_error("Meson acacia options", error)
    provenance = {}
    for name in (".acacia-config.json", "meson-info/intro-buildoptions.json"):
        try:
            options_mtime_ns = (build / name).stat().st_mtime_ns
        except Exception as error:
            options_mtime_ns = collection_error(name, error)
        if binary_absent:
            association = unavailable("binary absent")
        elif binary_hash.startswith("unavailable:"):
            association = unavailable("binary could not be hashed")
        elif not isinstance(mtime_ns, int):
            association = unavailable("binary mtime unavailable")
        elif not isinstance(options_mtime_ns, int):
            association = options_mtime_ns
        elif options_mtime_ns > mtime_ns:
            association = "recorded options may not describe this binary"
        else:
            association = ("options file is not newer than binary; "
                           "association with hashed binary is unverified")
        provenance[name] = {"mtime_ns": options_mtime_ns, "binary_association": association}
    return {
        "directory": directory,
        "binary": {"path": str(binary), "sha256": binary_hash,
                   "mtime_utc": mtime, "mtime_ns": mtime_ns},
        "recorded_configuration": {
            "acacia_options": json_object(read_text(build / ".acacia-config.json")),
            "meson_acacia_options": meson_acacia,
            "meson_options": options,
        },
        "recorded_configuration_provenance": provenance,
        "tlsf_corpus": corpus_metadata(build),
    }


def toolchain_metadata() -> dict:
    compiler = os.environ.get("CXX", "c++")
    try:
        argv = shlex.split(compiler)
        version = identity([*argv, "--version"]).splitlines()[0] if argv else unavailable(
            "CXX is empty")
    except Exception as error:
        version = collection_error("invalid CXX", error)

    spot_output = command([sys.executable, str(benchlib.ROOT / "scripts/spot-metadata.py")])
    spot = dict(line.split("=", 1) for line in spot_output.splitlines() if "=" in line)
    spot_version = spot.get("spot_version")
    spot_source = "scripts/spot-metadata.py"
    if not spot_version or spot_version == "unknown":
        spot_version = identity(["pkg-config", "--modversion", "libspot"])
        spot_source = "pkg-config --modversion libspot"
    accsets = spot.get("spot_max_accsets")

    # Spot installs its BuDDy fork as libbddx, often under /usr/local, just as
    # spot-metadata.py's pkg-config discovery accounts for that prefix.
    env = os.environ.copy()
    paths = [p for p in env.get("PKG_CONFIG_PATH", "").split(os.pathsep) if p]
    for candidate in ("/usr/local/lib/pkgconfig", "/usr/local/lib64/pkgconfig"):
        if candidate not in paths:
            paths.append(candidate)
    env["PKG_CONFIG_PATH"] = os.pathsep.join(paths)
    buddy = []
    for package in ("libbddx", "bdd", "buddy", "libbdd"):
        found = identity(["pkg-config", "--modversion", package], env=env)
        if not found.startswith("unavailable:"):
            buddy.append({"package": package, "version": found})
    return {
        "compiler": {"command": compiler, "version": version},
        "spot": {"version": spot_version, "source": spot_source,
                 "max_accsets": accsets if accsets and accsets != "unknown"
                 else unavailable("Spot acceptance-set limit not discovered")},
        "buddy": buddy or unavailable("pkg-config found no libbddx, bdd, buddy or libbdd"),
    }


def host_metadata() -> dict:
    meminfo = read_text(pathlib.Path("/proc/meminfo"))
    memory = dict(line.split(":", 1) for line in meminfo.splitlines() if ":" in line)
    memtotal = memory.get("MemTotal", unavailable("MemTotal unreadable in /proc/meminfo")).strip()
    swaps = read_text(pathlib.Path("/proc/swaps"))
    swap_enabled = swaps if swaps.startswith("unavailable:") else bool(swaps.splitlines()[1:])
    uid = os.getuid()
    user_slice = pathlib.Path(f"/sys/fs/cgroup/user.slice/user-{uid}.slice/cgroup.controllers")
    slice_contents = read_text(user_slice)
    slice_controllers = (slice_contents if slice_contents.startswith("unavailable:")
                         else sorted(slice_contents.split()))
    manager_path = user_slice.parent / f"user@{uid}.service/cgroup.controllers"
    missing = unavailable(f"cannot read {manager_path}")
    try:
        delegated = benchlib.user_manager_controllers()
    except Exception as error:
        delegated = None
        missing = collection_error(str(manager_path), error)
    return {
        "nproc": identity(["nproc"]),
        "MemTotal": memtotal,
        "swap_enabled": swap_enabled,
        "user_slice_controllers_path": str(user_slice),
        "user_slice_controllers": slice_controllers,
        "user_manager_controllers_path": str(manager_path),
        "user_manager_controllers": sorted(delegated) if delegated is not None else missing,
        "cpu_delegated": "cpu" in delegated if delegated is not None else missing,
        "memory_delegated": "memory" in delegated if delegated is not None else missing,
    }


def harness_metadata(path: pathlib.Path) -> dict:
    try:
        path = path.expanduser().resolve()
        name = str(path)
    except Exception as error:
        name = collection_error(str(path), error)
    return {"path": name, "sha256": sha256(path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, action="append", default=[], metavar="DIR")
    parser.add_argument("--preset", action="append", default=[], metavar="NAME")
    parser.add_argument("--harness", type=pathlib.Path, action="append", default=[], metavar="PATH")
    parser.add_argument("--output", type=pathlib.Path,
                        help="default: stdout; '-' also means stdout")
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    args = parser.parse_args()

    presets = []
    config_script = str(benchlib.ROOT / "scripts/acacia-config.py")
    for name in args.preset:
        presets.append({
            "name": name,
            "resolved_options": json_object(command([sys.executable, config_script, "show", name])),
            "hash": identity([sys.executable, config_script, "hash", name]),
        })
    harnesses = [benchlib.ROOT / "benchmarking" / name for name in
                 ("benchlib.py", "run-syntcomp26-coverage.py")]
    harnesses = [harness_metadata(p) for p in [*harnesses, *args.harness]]
    harnesses = {entry["path"]: entry for entry in harnesses}
    manifest = {
        "repository": str(benchlib.ROOT),
        "git": git_metadata(),
        "builds": [build_metadata(build) for build in args.build],
        "presets": presets,
        "toolchain": toolchain_metadata(),
        "tlsf_corpus": corpus_metadata(),
        "harnesses": [harnesses[path] for path in sorted(harnesses)],
        "host": host_metadata(),
    }
    rendered = json.dumps(manifest, indent=2, ensure_ascii=True) + "\n"
    if args.format == "markdown":
        rendered = ("# Campaign baseline identity\n\n"
                    "Observed checkout and environment; "
                    "build provenance is not inferred from HEAD.\n"
                    "An empty builds/presets list means none were requested.\n\n"
                    "```json\n" + rendered + "```\n")
    try:
        if args.output and str(args.output) != "-":
            args.output.write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
    except OSError as error:
        print(f"cannot write manifest: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
