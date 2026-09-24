"""Run source-bound online lifting, then the generic exact direct route."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import time

from acacia_lift.direct import Decline, run_exact_direct, sha256_file
from acacia_lift.lifting import provenance, schema, source
from acacia_lift.lifting import settings
from acacia_lift.tools import add_configuration_arguments, configuration_from_args


def _reason(value: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", value.lower()).strip("_")


def _write_evidence(path: pathlib.Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                         encoding="utf-8")
    os.replace(temporary, path)


def _success(evidence: dict, source_hash: str, result,
             route: str, *, arities: dict | None = None,
             seeds: list | None = None, version: int | None = None) -> int:
    evidence["route"] = route
    evidence["path_kind"] = route
    evidence["target_verified"] = True
    evidence["target_certificate"] = {
        "source_sha256": source_hash, "game_sha256": result.game_sha256,
        "certificate_sha256": result.certificate_sha256,
        "policy_sha256": result.policy_sha256,
        "certificate_side": "system" if route == "lifted-certified" else result.side,
        "reduction_semantics": result.reduction_semantics,
        "checker_verdict": ("REGION_VERIFIED" if result.proof_method == "gr1-region-v1"
                            else "VERIFIED"),
    }
    evidence["result"] = {
        "verdict": "REALIZABLE" if route == "lifted-certified" else result.verdict,
        "stage": "target_check", "reason": "verified",
        "certificate": str(result.certificate),
        "policy": str(result.policy) if result.policy is not None else None,
        "proof_method": result.proof_method,
    }
    if route == "lifted-certified":
        evidence["seeds"] = seeds
        evidence["predicate_arities"] = arities
        evidence["provenance_format_version"] = version
    evidence["stages"].update(result.stages)
    return 0 if evidence["result"]["verdict"] == "REALIZABLE" else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request-mode", choices=("source",), default="source")
    parser.add_argument("-T", "--tlsf", required=True, type=pathlib.Path)
    parser.add_argument("--budget", type=float, default=120.0)
    parser.add_argument("--eligibility-budget-seconds", type=float, default=1.0)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--evidence-out", required=True, type=pathlib.Path)
    add_configuration_arguments(parser)
    args = parser.parse_args(argv)
    started = time.monotonic()
    deadline = started + args.budget
    evidence: dict = {"schema_version": 3, "path_kind": "direct-certified",
                      "route": "direct-certified", "target_verified": False,
                      "eligibility_budget_s": args.eligibility_budget_seconds,
                      "global_knobs": {
                          "max_sizes_per_axis": settings.MAX_SIZES_PER_AXIS,
                          "max_predicate_arity": settings.MAX_PREDICATE_ARITY,
                          "max_subsets_per_predicate": settings.MAX_SUBSETS_PER_PREDICATE,
                          "move_schema_seconds": settings.MOVE_SCHEMA_SECONDS,
                          "discovery_share": settings.DISCOVERY_SHARE,
                          "policy_proof_fraction": settings.POLICY_PROOF_FRACTION,
                          "route_order": settings.ROUTE_ORDER},
                      "stages": {}}
    code = 2
    def checkpoint(stage: str, status: str = "active") -> None:
        now = time.monotonic()
        previous = evidence.get("generalizer_progress", {})
        active = previous.get("active_stage")
        if status == "declined" and active and active not in evidence["stages"]:
            evidence["stages"][active] = {
                "elapsed_s": max(0.0, now - previous["updated_monotonic_s"]),
                "censored": True}
        evidence["generalizer_progress"] = {
            "stage": stage, "active_stage": stage if status == "active" else None,
            "status": status, "updated_monotonic_s": now}
        _write_evidence(args.evidence_out, evidence)

    try:
        if args.budget <= 0 or args.eligibility_budget_seconds <= 0:
            raise Decline("configuration", "invalid_budget")
        config = configuration_from_args(args)
        original = args.tlsf.resolve()
        checkpoint("source_binding")
        original_hash = sha256_file(original)
        output = args.output_dir.resolve()
        snapshot, source_hash = source.snapshot(original, output)
        if original_hash != source_hash:
            raise Decline("source_binding", "input_changed_during_snapshot")
        evidence["source_binding"] = {"input": {"sha256": source_hash}}
        checkpoint("source_binding", "complete")
        target = None
        try:
            checkpoint("target_reduction")
            stage_started = time.monotonic()
            reduction_deadline = min(deadline, started + args.eligibility_budget_seconds)
            try:
                target = source.lower(snapshot, output / "target", config,
                                      reduction_deadline)
            except Decline as exact_error:
                evidence["exact_reduction_failure"] = {
                    "stage": exact_error.stage, "reason": exact_error.reason}
                target = source.lower(snapshot, output / "target_strict", config,
                                      reduction_deadline, semantics="strict")
            evidence["stages"]["target_reduction"] = {
                "elapsed_s": time.monotonic() - stage_started,
                "game_sha256": sha256_file(target.game),
                "provenance_sha256": sha256_file(target.provenance)}
            checkpoint("target_reduction", "complete")
        except Decline as error:
            evidence["target_reduction_failure"] = {"stage": error.stage,
                                                    "reason": error.reason}
            checkpoint("target_reduction", "declined")
        is_parametric = bool(provenance.parameters(target)) if target is not None else False
        if is_parametric and target is not None:
            try:
                discovery_deadline = min(deadline,
                                         started + args.budget * settings.DISCOVERY_SHARE)
                checkpoint("seed_discovery")
                stage_started = time.monotonic()
                version = provenance.frontend_version(snapshot, output, config,
                                                       discovery_deadline)
                window = provenance.discover(snapshot, target, output / "seeds",
                                             config, discovery_deadline)
                evidence["seeds"] = [dict(window.instances[i].overrides)
                                     for i in range(len(window.values))]
                evidence["stages"]["seed_discovery"] = {
                    "elapsed_s": time.monotonic() - stage_started,
                    "axis_values": list(window.values)}
                checkpoint("seed_discovery", "complete")
                stage_started = time.monotonic()
                checkpoint("seed_solves")
                certificates = [source.solve_seed(instance, output / f"seed_solve_{i}",
                                                       config, discovery_deadline)
                                for i, instance in enumerate(window.instances)]
                evidence["stages"]["seed_solves"] = {
                    "elapsed_s": time.monotonic() - stage_started}
                checkpoint("seed_solves", "complete")
                stage_started = time.monotonic()
                checkpoint("schema")
                seeds, target_instance = schema.prepare(window, target, certificates)
                bdds = schema.Bdds(config, max(len(item.variables) for item in
                                               [*seeds, target_instance]))
                predicates, depths, arities = schema.learn_certificate(
                    bdds, seeds, target_instance, discovery_deadline)
                evidence["stages"]["schema"] = {
                    "elapsed_s": time.monotonic() - stage_started,
                    "rank_depths": depths}
                evidence["predicate_arities"] = arities
                checkpoint("schema", "complete")
                stage_started = time.monotonic()
                checkpoint("target_proof")
                from acacia_lift.lifting.proof import prove
                lifted = prove(bdds, target_instance, predicates, depths,
                               output, config, deadline)
                evidence["stages"]["target_proof"] = {
                    "elapsed_s": time.monotonic() - stage_started,
                    "method": lifted.proof_method}
                checkpoint("target_proof", "complete")
                if sha256_file(snapshot) != source_hash or sha256_file(original) != original_hash:
                    raise Decline("source_binding", "input_changed")
                code = _success(evidence, source_hash, lifted, "lifted-certified",
                                arities=arities, seeds=evidence["seeds"], version=version)
            except Decline as error:
                evidence["lifting_failure"] = {"stage": error.stage,
                                               "reason": _reason(error.reason)}
                checkpoint(error.stage, "declined")
            except (ValueError, KeyError, IndexError, OverflowError, RuntimeError,
                    MemoryError) as error:
                evidence["lifting_failure"] = {"stage": "schema_or_proof",
                                               "reason": _reason(type(error).__name__)}
                checkpoint("schema_or_proof", "declined")
        if code == 2:
            try:
                stage_started = time.monotonic()
                checkpoint("direct")
                direct = run_exact_direct(snapshot, output / "direct", config,
                                          deadline, max(0.001, min(
                                              args.eligibility_budget_seconds,
                                              deadline - time.monotonic())))
                evidence["stages"]["direct"] = {"elapsed_s": time.monotonic() - stage_started}
                checkpoint("direct", "complete")
                if sha256_file(snapshot) != source_hash or sha256_file(original) != original_hash:
                    raise Decline("source_binding", "input_changed")
                # CheckedResult uses a fixed certificate method.
                direct = DirectEvidenceAdapter(direct)
                code = _success(evidence, source_hash, direct, "direct-certified")
            except Decline as error:
                evidence["result"] = {"verdict": "UNKNOWN", "stage": error.stage,
                                      "reason": _reason(error.reason)}
                checkpoint(error.stage, "declined")
    except Decline as error:
        evidence["result"] = {"verdict": "UNKNOWN", "stage": error.stage,
                              "reason": _reason(error.reason)}
    except Exception as error:
        evidence["result"] = {"verdict": "UNKNOWN", "stage": "internal_error",
                              "reason": _reason(type(error).__name__)}
    evidence["elapsed_s"] = time.monotonic() - started
    checkpoint(evidence["result"]["stage"], "complete" if code in (0, 1) else "declined")
    print(evidence["result"]["verdict"], flush=True)
    return code


class DirectEvidenceAdapter:
    """Match the lifted result's small evidence interface."""

    def __init__(self, result):
        self.verdict = result.verdict
        self.side = result.side
        self.game_sha256 = result.game_sha256
        self.certificate = result.certificate
        self.certificate_sha256 = result.certificate_sha256
        self.policy = result.policy
        self.policy_sha256 = result.policy_sha256
        self.proof_method = "certificate"
        self.reduction_semantics = "exact"
        self.stages = result.stages


if __name__ == "__main__":
    raise SystemExit(main())
