#!/usr/bin/env python3
"""Capture bounded classic-backward operations with the existing scoped runner."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from benchlib import campaign_scope_guard, classify_run, load_tool, run_systemd_scope

M = load_tool("benchmarking/posets-matrix.py")
ROOT = M.ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Frozen diagnostics-enabled executable")
    parser.add_argument("--provenance", type=Path, required=True, help="Frozen source/build provenance JSON")
    parser.add_argument("--input", action="append", required=True, metavar="INSTANCE=TLSF")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = M.regular(args.binary).resolve()
    provenance = M.regular(args.provenance).resolve()
    M.frozen_identity(provenance, M.sha(binary))
    output = args.output.resolve()
    inputs = []
    for item in args.input:
        instance, path = item.split("=", 1)
        M.require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", instance), "Unsafe instance ID")
        source = M.regular(Path(path)).resolve()
        inputs.append((instance, source, M.sha(source)))
    M.require(len({instance for instance, _, _ in inputs}) == len(inputs), "Repeated instance ID")
    M.require(not output.exists(), "Output exists; choose a fresh ignored capture directory")
    M.require(subprocess.run(["git", "check-ignore", "-q", str(output)], cwd=ROOT).returncode == 0,
              "Capture evidence must start in an ignored directory")
    M.require(not any(name.startswith("ACACIA_ANTICHAIN_SNAPSHOT_") for name in os.environ),
              "Unset inherited snapshot variables; this helper declares its complete capture policy")
    os.chdir(ROOT)
    output.mkdir(parents=True)
    implementation = {str(ROOT / "benchmarking" / name): M.sha(ROOT / "benchmarking" / name)
                      for name in ("posets-capture.py", "posets-matrix.py", "benchlib.py", "scope_memory.py")}
    executed_implementation = dict(implementation)
    (output / "implementation").mkdir()
    for source in executed_implementation:
        shutil.copyfile(source, output / "implementation" / Path(source).name)
    implementation = {str(output / "implementation" / Path(source).name): expected
                      for source, expected in executed_implementation.items()}
    frozen = {"binary": {"path": str(binary), "sha256": M.sha(binary)},
              "frozen_provenance": {"path": str(provenance), "sha256": M.sha(provenance)},
              "runtime_libraries": M.libraries(binary), "implementation_sha256": implementation,
              "implementation_source_paths": {Path(source).name: source for source in executed_implementation}}
    invocations = []
    with campaign_scope_guard("posets-capture"):
        for instance, tlsf, input_sha256 in inputs:
            directory = output / instance
            snapshot = directory / "snapshots"
            snapshot.mkdir(parents=True)
            environment = {**os.environ, "ACACIA_DIAG": "1", "ACACIA_DIAG_INSTANCE": instance,
                           "ACACIA_DIAG_PROGRESS_EVERY": "0", "ACACIA_ANTICHAIN_SNAPSHOT_DIR": str(snapshot),
                           "ACACIA_ANTICHAIN_SNAPSHOT_CPRE": "1", "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX": "8",
                           "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS": "4096",
                           "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_FRONTIERS": "1",
                           "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS": "1",
                           "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS_MAX": "16"}
            command = [str(binary), "--arms", "real:small:backward", "--spot-fast", "off",
                       "--r-prepass", "off", "--equivariance", "off", "-T", str(tlsf)]
            manifest = {"schema_version": 1, "kind": "posets-operation-capture", "instance": instance,
                        **frozen, "input": {"path": str(tlsf), "sha256": input_sha256},
                        "command": command, "snapshot_directory": str(snapshot),
                        "environment": {name: value for name, value in environment.items()
                                        if name.startswith(("ACACIA_", "LD_", "MALLOC_", "GLIBC_", "OMP_"))},
                        "treatment": {"timeout": 17, "memory_max": "8G", "memory_swap_max": "0", "serial": True}}
            (directory / "invocation-start.json").write_text(json.dumps(manifest, indent=2) + "\n")
            result = run_systemd_scope(command, 17, "8G", "0", env=environment,
                                       unit_prefix="acacia-posets-capture", scratch_root=output / "scopes")
            manifest.update({"result": M.process_record(result), "classification": classify_run(result),
                             "logs_sha256": M.save_logs(directory, "run", result.stdout, result.stderr),
                             "snapshot_files_sha256": {str(file.relative_to(snapshot)): M.sha(file)
                                                       for file in sorted(snapshot.rglob("*")) if file.is_file()}})
            # Keep native stdout/stderr in files whose names the schema binds.
            (directory / "run.stdout").rename(directory / "stdout")
            (directory / "run.stderr").rename(directory / "stderr")
            path = directory / "invocation.json"
            path.write_text(json.dumps(manifest, indent=2) + "\n")
            M.capture_identity(path, instance)
            M.require(M.libraries(binary) == frozen["runtime_libraries"], "Capture runtime changed")
            M.require(all(M.sha(source) == expected for _, source, expected in inputs), "A declared TLSF input changed")
            M.require(all(M.sha(Path(source)) == expected for source, expected in executed_implementation.items()),
                      "Executed capture helper/support changed")
            invocations.append({"instance": instance, "manifest": str(path), "sha256": M.sha(path),
                                "classification": manifest["classification"],
                                "operation_files": sum(bool(re.fullmatch(r"(?:cpre|contains)-\d+\.tsv", Path(name).name))
                                                       for name in manifest["snapshot_files_sha256"])})
            (output / "captures.json").write_text(json.dumps({"invocations": invocations}, indent=2) + "\n")
    print(json.dumps({"output": str(output), "invocations": invocations}, indent=2))


if __name__ == "__main__":
    main()
