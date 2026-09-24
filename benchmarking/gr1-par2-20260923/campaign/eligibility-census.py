#!/usr/bin/env python3
"""Serial, verdict-blind census of the production lifting source binder."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import pathlib
import sys
import time
from types import SimpleNamespace


ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "benchmarking"))
from acacia_lift import capabilities as binding  # noqa: E402
from acacia_lift import runner  # noqa: E402


def coverage_module():
    path = ROOT / "benchmarking" / "run-syntcomp26-coverage.py"
    spec = importlib.util.spec_from_file_location("syntcomp26_coverage", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", type=pathlib.Path,
                        default=ROOT / "tests/suites/benchmarks/syntcomp26/all.list")
    parser.add_argument("--tlsf-map", type=pathlib.Path,
                        default=ROOT / "tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv")
    parser.add_argument("--tlsf-corpus", type=pathlib.Path,
                        default=ROOT / "tlsf-corpus")
    parser.add_argument("--tlsf-tools-build", type=pathlib.Path,
                        default=pathlib.Path("/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b"))
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).with_name("eligibility.tsv"))
    args = parser.parse_args()
    coverage = coverage_module()
    instances = coverage.read_instance_list(args.list)
    if len(instances) != 1524 or len(set(instances)) != len(instances):
        raise ValueError("expected 1,524 distinct SYNTCOMP26 IDs")
    targets = coverage.resolve_targets(
        instances, coverage.read_tlsf_map(args.tlsf_map), args.tlsf_corpus
    )
    build = args.tlsf_tools_build.resolve()
    config = SimpleNamespace(tlsf2tlsf=build / "tlsf2tlsf",
                             tlsf2ltl=build / "tlsf2ltl", tlsfinfo=build / "tlsfinfo")
    for tool in (config.tlsf2tlsf, config.tlsf2ltl, config.tlsfinfo):
        if not tool.is_file():
            raise FileNotFoundError(tool)

    fields = ("id", "source_sha256", "decision", "capability", "parameters",
              "route_kind", "real_check", "route_enabled", "decline_reason",
              "tool_calls", "elapsed_s")
    temporary = args.output.with_name(args.output.name + ".tmp")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    original = binding._run_tool
    with temporary.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for index, instance in enumerate(instances, 1):
            source = targets[instance][1]
            calls = 0

            def counted(*call_args, **call_kwargs):
                nonlocal calls
                calls += 1
                return original(*call_args, **call_kwargs)

            binding._run_tool = counted
            start = time.monotonic()
            try:
                request = runner._resolve_source_request(
                    SimpleNamespace(tlsf=source, semantics="exact", family=None,
                                    target=None, seeds=None),
                    config, runner.Deadline.start(3600.0),
                )
            except runner.PipelineFailure as error:
                decision = "decline"
                capability = parameters = route_kind = real_check = route_enabled = ""
                source_binding = error.source_binding or {}
                bound_capability = source_binding.get("capability", {})
                if isinstance(bound_capability, dict):
                    capability = bound_capability.get("family", "")
                    route_kind = bound_capability.get("route_kind", "")
                    if bound_capability.get("route_enabled") is False:
                        route_enabled = "false"
                reason = error.reason
            else:
                decision = "eligible"
                capability = request.family
                assert request.source_request is not None
                parameters = ",".join(f"{name}={value}" for name, value in
                                      request.source_request.identity.parameters)
                route_kind = request.spec.route_kind
                real_check = request.spec.real_check
                route_enabled = "true"
                reason = ""
            finally:
                binding._run_tool = original
            writer.writerow(dict(id=instance,
                                 source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                                 decision=decision, capability=capability,
                                 parameters=parameters, route_kind=route_kind,
                                 real_check=real_check, route_enabled=route_enabled,
                                 decline_reason=reason,
                                 tool_calls=calls, elapsed_s=f"{time.monotonic() - start:.6f}"))
            stream.flush()
            if index % 100 == 0:
                print(f"{index}/{len(instances)}", flush=True)
    temporary.replace(args.output)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
