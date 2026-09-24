"""Run the generic direct-certified exact GR(1) route on an actual TLSF input."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import time

from acacia_lift.direct import Decline, run_exact_direct, sha256_file
from acacia_lift.tools import add_configuration_arguments, configuration_from_args


def _reason(value: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", value.lower()).strip("_")


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
    start = time.monotonic()
    evidence: dict = {"schema_version": 2, "path_kind": "direct-certified",
                      "route": "direct-certified", "target_verified": False,
                      "eligibility_budget_s": args.eligibility_budget_seconds}
    code = 2
    try:
        if args.budget <= 0 or args.eligibility_budget_seconds <= 0:
            raise Decline("configuration", "invalid_budget")
        source = args.tlsf.resolve()
        source_hash = sha256_file(source)
        evidence["source_binding"] = {"input": {"sha256": source_hash}}
        config = configuration_from_args(args)
        result = run_exact_direct(source, args.output_dir.resolve(), config,
                                  start + args.budget, args.eligibility_budget_seconds)
        if sha256_file(source) != source_hash:
            raise Decline("source_binding", "input_changed")
        evidence["target_certificate"] = {
            "source_sha256": source_hash, "game_sha256": result.game_sha256,
            "certificate_sha256": result.certificate_sha256,
            "policy_sha256": result.policy_sha256, "certificate_side": result.side,
            "reduction_semantics": "exact", "checker_verdict": "VERIFIED",
        }
        evidence["stages"] = result.stages
        evidence["target_verified"] = True
        evidence["result"] = {
            "verdict": result.verdict, "stage": "target_check", "reason": "verified",
            "certificate": str(result.certificate), "policy": str(result.policy),
            "proof_method": "certificate",
        }
        code = 0 if result.verdict == "REALIZABLE" else 1
    except Decline as error:
        evidence["result"] = {"verdict": "UNKNOWN", "stage": error.stage,
                              "reason": _reason(error.reason)}
    except Exception as error:
        evidence["result"] = {"verdict": "UNKNOWN", "stage": "internal_error",
                              "reason": _reason(type(error).__name__)}
    evidence["elapsed_s"] = time.monotonic() - start
    args.evidence_out.parent.mkdir(parents=True, exist_ok=True)
    args.evidence_out.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n",
                                 encoding="utf-8")
    print(evidence["result"]["verdict"], flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
