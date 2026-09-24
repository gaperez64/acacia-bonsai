#!/usr/bin/env python3
"""Build the BuDDy adapter once and bind it to all native inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
PARAM_LIFT = HERE.parent
ROOT = PARAM_LIFT.parents[1]
SOURCE = HERE / "buddy_veccompose_adapter.cc"
DEFAULT_OUTPUT_DIR = ROOT / "build_scratch" / "buddy-adapter"
SCHEMA = "acacia-buddy-veccompose-adapter-v2"
DEFAULT_BINDINGS_PYTHON = pathlib.Path(
    os.environ.get(
        "ACACIA_BINDINGS_PYTHON",
        "/usr/bin/python3.13" if pathlib.Path("/usr/bin/python3.13").exists()
        else sys.executable,
    )
)
DEFAULT_BINDINGS_SITE = pathlib.Path(
    os.environ.get(
        "ACACIA_BINDINGS_SITE",
        "/usr/local/lib64/python3.13/site-packages",
    )
)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def binding_inputs(
    interpreter: pathlib.Path, site: pathlib.Path
) -> tuple[pathlib.Path, pathlib.Path]:
    script = r"""
import json
import pathlib
import sys
sys.path.insert(0, sys.argv[1])
import buddy
import _buddy
paths = sorted({
    str(pathlib.Path(fields[-1]).resolve())
    for line in pathlib.Path("/proc/self/maps").read_text(encoding="utf-8").splitlines()
    if (fields := line.split()) and fields[-1].startswith("/")
    and pathlib.Path(fields[-1]).name.startswith("libbddx.so")
})
if len(paths) != 1:
    raise SystemExit(f"expected exactly one mapped libbddx, found {paths}")
print(json.dumps({"extension": str(pathlib.Path(_buddy.__file__).resolve()),
                  "libbddx": paths[0]}))
"""
    env = dict(os.environ)
    env["PYTHONPATH"] = str(site)
    env["PYTHONNOUSERSITE"] = "1"
    result = subprocess.run(
        [str(interpreter), "-s", "-c", script, str(site)],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "cannot resolve configured BuDDy binding: "
            + (result.stderr or result.stdout).strip()
        )
    payload = json.loads(result.stdout)
    return pathlib.Path(payload["extension"]), pathlib.Path(payload["libbddx"])


def compiler_version(stderr: str, stdout: str) -> str:
    lines = [line.strip() for line in (stderr + "\n" + stdout).splitlines()
             if line.strip()]
    for marker in ("gcc version ", "clang version "):
        for line in lines:
            if marker in line.lower():
                return line
    if not lines:
        raise RuntimeError("compiler -v produced no version output")
    return lines[0]


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--output-dir", type=pathlib.Path,
                        default=DEFAULT_OUTPUT_DIR)
    result.add_argument("--bindings-python", type=pathlib.Path,
                        default=DEFAULT_BINDINGS_PYTHON)
    result.add_argument("--bindings-site", type=pathlib.Path,
                        default=DEFAULT_BINDINGS_SITE)
    result.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    interpreter = args.bindings_python.expanduser().resolve()
    site = args.bindings_site.expanduser().resolve()
    if not interpreter.is_file():
        raise RuntimeError(f"binding interpreter not found: {interpreter}")
    if not site.is_dir():
        raise RuntimeError(f"bindings site not found: {site}")
    _extension, library = binding_inputs(interpreter, site)
    library = library.resolve()
    header = library.parent.parent / "include" / "bddx.h"
    if not library.is_file():
        raise RuntimeError(f"resolved libbddx not found: {library}")
    if not header.is_file():
        raise RuntimeError(f"BuDDy header not found: {header}")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError(f"compiler not found: {args.compiler}")
    compiler_path = pathlib.Path(compiler).resolve()

    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / "libbuddy_veccompose_adapter.so"
    temporary = output.with_name(f".{output.name}.{os.getpid()}.tmp")
    flags = [
        "-std=c++17",
        "-O2",
        "-fPIC",
        "-fvisibility=hidden",
        "-shared",
        "-Wl,-z,defs",
        f"-Wl,-rpath,{library.parent}",
        f"-I{header.parent}",
        "-v",
    ]
    command = [
        str(compiler_path), *flags, str(SOURCE), str(library),
        "-o", str(temporary),
    ]
    try:
        # This is deliberately the build step's only compiler invocation.
        result = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"adapter compile failed with exit {result.returncode}: "
                + (result.stderr or result.stdout).strip()
            )
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)

    payload = {
        "schema": SCHEMA,
        "adapter_sha256": sha256(output),
        "source_sha256": sha256(SOURCE),
        "compiler": compiler_path.name,
        "compiler_version": compiler_version(result.stderr, result.stdout),
        "flags": flags,
        "libbddx_sha256": sha256(library),
        "binding_extension_sha256": sha256(_extension),
        # This installation names its bdd.h-equivalent bddx.h.
        "bdd_header_sha256": sha256(header),
    }
    sidecar = output.with_suffix(output.suffix + ".json")
    temporary_sidecar = sidecar.with_name(f".{sidecar.name}.{os.getpid()}.tmp")
    temporary_sidecar.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary_sidecar.replace(sidecar)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
