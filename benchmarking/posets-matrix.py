#!/usr/bin/env python3
"""Run the existing Posets harness' semantic matrix; preserve every rejected cell."""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODES = ("compatible", "native", "max")
CASES = ("vectors", "nonempty", "empty", "zero")


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def provenance(build, output):
    commands = json.loads((build / "compile_commands.json").read_text())
    modes, aliases, seen = {}, {}, {}
    for mode in MODES:
        binary = build / "tests" / f"downset-matrix-{mode}"
        metadata = json.loads(subprocess.check_output([str(binary), "--metadata"], text=True))
        assembly = subprocess.check_output(
            ["objdump", "--disassemble=posets_matrix_compare", str(binary)], text=True)
        (output / f"{mode}-compare.asm").write_text(assembly)
        instructions = [match[1] for line in assembly.splitlines()
                        if (match := re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)", line))]
        require(bool(instructions), "Missing emitted SIMD comparison kernel")
        instruction_hash = hashlib.sha256(bytes.fromhex(" ".join(instructions))).hexdigest()
        key = (metadata["simd_type"], metadata["alignment"], instruction_hash)
        aliases[mode] = seen.setdefault(key, mode)
        selected = [row for row in commands if f"downset-matrix-{mode}.p/" in row.get("output", "")]
        require(len(selected) == 1, f"Missing/ambiguous compiler command for {mode}")
        require(len(metadata["backends"]) == len(set(metadata["backends"])) == 14,
                "The requested14-backend inventory changed")
        modes[mode] = {**metadata, "binary": str(binary), "binary_sha256": sha(binary),
                       "comparison_instructions_sha256": instruction_hash, "compiler_command": selected[0]}
    source = ROOT / "subprojects/posets"
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                                    cwd=source, text=True).splitlines()
    return {"modes": modes, "aliases": aliases,
            "posets_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
            "source_sha256": {name: sha(source / name) for name in names if (source / name).is_file()},
            "posets_dirty": subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True),
            "driver_sha256": sha(Path(__file__).resolve()), "affinity": sorted(os.sched_getaffinity(0)),
            "cpu": subprocess.check_output(["lscpu", "-J"], text=True)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case-timeout", type=float, default=15)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output.resolve()
    require(not output.exists(), "Output exists; preserve it and choose a new directory")
    require(subprocess.run(["git", "check-ignore", "-q", str(output)], cwd=ROOT).returncode == 0,
            "Raw matrix evidence must start in an ignored output directory")
    output.mkdir(parents=True)
    metadata = provenance(build, output)
    rows = []
    for mode, entry in metadata["modes"].items():
        if metadata["aliases"][mode] != mode:
            continue
        for backend in entry["backends"]:
            # Scalar is an independent semantic control, not a promise about the
            # compiler's automatic vectorization of ordinary integer loops.
            for vector in ("vector_backed", "simd_vector_backed"):
                for case in CASES:
                    command = [entry["binary"], "--check=" + case, backend, vector + "<test_value_type>"]
                    prefix = f"{mode}-{backend}-{vector}-{case}"
                    try:
                        result = subprocess.run(command, capture_output=True, text=True,
                                                timeout=args.case_timeout, preexec_fn=no_core)
                        status, code = ("pass" if result.returncode == 0 else "rejected"), result.returncode
                        stdout, stderr = result.stdout, result.stderr
                    except subprocess.TimeoutExpired as error:
                        status, code = "timeout", None
                        stdout, stderr = error.stdout or b"", error.stderr or b""
                    for suffix, text in (("stdout", stdout), ("stderr", stderr)):
                        (output / (prefix + "." + suffix)).write_bytes(text.encode() if isinstance(text, str) else text)
                    rows.append({"mode": mode, "backend": backend, "vector": vector,
                                 "case": case, "status": status, "exit": code, "log_prefix": prefix})
    require(all(sha(Path(row["binary"])) == row["binary_sha256"] for row in metadata["modes"].values()),
            "A harness executable changed during the semantic screen")
    report = {"scope": "Semantic screen only; no operation timings sampled",
              "cases": "empty downset, zero-dimensional singleton and all-minus-one bottom are distinct",
              "policy": "Every rejection/crash/timeout is retained. Constructor preconditions, empty results and zero-dimensional domains stay distinct; actual-trace eligibility is checked separately.",
              "provenance": metadata, "rows": rows,
              "counts": dict(Counter(row["status"] for row in rows)), "performance_measurements": 0}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"output": str(output), "aliases": metadata["aliases"], "counts": report["counts"]}, indent=2))


if __name__ == "__main__":
    main()
