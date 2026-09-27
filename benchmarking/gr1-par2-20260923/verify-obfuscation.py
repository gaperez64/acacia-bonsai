#!/usr/bin/env python3
"""Compare lowered LTL before and after seeded TLSF signal renaming."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
SCRIPT = pathlib.Path(__file__).with_name("obfuscate-tlsf.py")
spec = importlib.util.spec_from_file_location("obfuscate_tlsf", SCRIPT)
assert spec and spec.loader
obfuscator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(obfuscator)


def sources() -> list[tuple[str, pathlib.Path]]:
    path = ROOT / "benchmarking/run-syntcomp26-coverage.py"
    module_spec = importlib.util.spec_from_file_location("coverage_module", path)
    assert module_spec and module_spec.loader
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    suite = ROOT / "tests/suites/benchmarks/syntcomp26"
    ids = module.read_instance_list(suite / "all.list")
    targets = module.resolve_targets(
        ids, module.read_tlsf_map(suite / "tlsf-sources.tsv"), ROOT / "tlsf-corpus")
    assert len(ids) == 1524
    return [(identifier, targets[identifier][1]) for identifier in ids]


def run(command: list[str], limit: float) -> str:
    result = subprocess.run(command, capture_output=True, text=True,
                            check=False, timeout=limit)
    if result.returncode != 0:
        raise RuntimeError(f"exit {result.returncode}: {result.stderr[-160:]}")
    return result.stdout.strip()


def expanded(tool: pathlib.Path, source: pathlib.Path, limit: float) -> list[str]:
    values = []
    for option in ("--expanded-ins", "--expanded-outs"):
        values.extend(value for value in run([str(tool), option, str(source)], limit).split(",")
                      if value)
    return values


def check_pair(source: pathlib.Path, target: pathlib.Path, build: pathlib.Path,
               limit: float) -> bool:
    ltl = build / "tlsf2ltl"
    info = build / "tlsfinfo"
    original_names = expanded(info, source, limit)
    obfuscated_names = expanded(info, target, limit)
    if len(original_names) != len(obfuscated_names):
        return False
    originals = run([str(ltl), "--format", "ltl", str(source)], limit)
    changed = run([str(ltl), "--format", "ltl", str(target)], limit)
    mapping = dict(zip(original_names, obfuscated_names, strict=True))
    normalized = re.sub(r"[A-Za-z_][A-Za-z_0-9]*",
                        lambda match: mapping.get(match.group(), match.group()), originals)
    return "".join(normalized.split()) == "".join(changed.split())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tlsf-tools-build", type=pathlib.Path,
                        default=ROOT / "subprojects/tlsf-tools/build-oxidd")
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).with_name("campaign") /
                        "obfuscation-invariance.tsv")
    parser.add_argument("--scratch", type=pathlib.Path,
                        default=ROOT / "build_scratch/stageA/obfuscation-corpus")
    parser.add_argument("--seed", type=int, default=20260924)
    parser.add_argument("--per-tool-timeout", type=float, default=12.0)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("id", "status", "detail"), delimiter="\t")
        writer.writeheader()
        for index, (identifier, source) in enumerate(sources(), 1):
            try:
                target, sidecar = obfuscator.write_obfuscated(
                    source, args.scratch, args.seed + index)
                mapping = json.loads(sidecar.read_text(encoding="utf-8"))["signals"]
                if (set(mapping) != set(obfuscator.signal_names(source.read_text())) or
                        set(mapping.values()) != set(obfuscator.signal_names(target.read_text()))):
                    raise ValueError("sidecar does not cover every declared signal")
                status = "equal" if check_pair(source, target, args.tlsf_tools_build,
                                                args.per_tool_timeout) else "mismatch"
                detail = ""
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
                status, detail = "error", str(error).replace("\n", " ")[:300]
            writer.writerow({"id": identifier, "status": status, "detail": detail})
            stream.flush()
            if index % 100 == 0:
                print(f"{index}/1524", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
