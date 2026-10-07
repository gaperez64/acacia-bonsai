#!/usr/bin/env python3
"""Generate the attribution cause census from explicit production call arguments."""
from __future__ import annotations

import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "benchmarking/coverage-first/decline-causes.md"
EXPECTATIONS = ROOT / "tests/attribution-cause-expectations.json"
SOURCES = (
    "src/native_support.cc", "src/native_proof_binding.cc",
    "src/native_gr1_arm.cc", "src/native_gr1_lift_arm.cc", "src/native_param_lift_arm.cc",
    "src/solver/closure_buchi_provider.cc", "src/solver/spot_state_ids.hh",
    "src/solver/spot_rows.hh", "src/solver/spot_letter_oracle.hh",
    "src/solver/spot_lazy_game.hh", "src/solver/spot_guarded_forward_safety.hh",
    "src/solver/solver_invoker.cc",
    "src/solver/spot_lazy_buchi_view.hh", "src/solver/spot_lazy_worker.hh",
    "subprojects/tlsf-tools/src/lib/pipeline.c",
    "subprojects/tlsf-tools/src/lib/pipeline_source.c",
    "subprojects/tlsf-tools/src/lib/gr1_shared.hh",
    "subprojects/tlsf-tools/src/lib/gr1_reduction.cc",
    "subprojects/tlsf-tools/src/lib/gr1_service.c",
    "subprojects/tlsf-tools/src/lib/gr1_lift.cc",
    "subprojects/tlsf-tools/src/lib/gr1_env_lift.cc",
    "subprojects/tlsf-tools/src/lib/gr1_check.c",
    "subprojects/tlsf-tools/src/lib/gr1_oxidd.c",
    "subprojects/tlsf-tools/src/lib/oxidd_common.c",
    "subprojects/tlsf-tools/src/lib/oxidd_order.c",
)



def arguments(source: str, start: int) -> tuple[int, list[str]]:
    depth = 0
    quote = ""
    escaped = False
    args = []
    part = start
    for pos in range(start, len(source)):
        char = source[pos]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in "\"'":
            quote = char
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            if char == ")" and depth == 0:
                args.append(source[part:pos].strip())
                return pos + 1, args
            depth -= 1
        elif char == "," and depth == 0:
            args.append(source[part:pos].strip())
            part = pos + 1
    raise ValueError("unterminated call")


def compact(value: str) -> str:
    return re.sub(r"\s+", " ", value).replace("|", "\\|")


def origins() -> list[dict[str, str | int]]:
    """Enumerate origins as well as boolean validator checks below status adapters."""
    rows = []
    calls = (r"\b(decline|Declined|Failure|ResourceLimit|fail|pipeline_status|pipeline_error|"
             r"oxidd_record_failure|check_failure|native_diagnostic|native_arm_diagnostic|"
             r"worker_decline|worker_stopped|require)\s*\(")
    for name in SOURCES:
        source = (ROOT / name).read_text()
        for match in re.finditer(calls, source):
            _, args = arguments(source, match.end())
            call = match[1]
            if not args or any(value in args[0] for value in (
                    "FailureCause ", "Tlsf", "std::string_view ", "FailureKind ",
                    "const Tlsf", "Oxidd", "bool condition", "void*", "void *")):
                continue
            if call == "Failure":
                if len(args) != 4:
                    raise ValueError(f"{name}: untyped failure: {args}")
                legacy, cause, stage, message = args
            elif call == "decline":
                if len(args) != 3:
                    raise ValueError(f"{name}: untyped decline: {args}")
                cause, stage, message = args
                legacy = "DECLINED"
            elif call == "Declined":
                if len(args) != 2:
                    raise ValueError(f"{name}: untyped provider decline: {args}")
                cause, message = args
                legacy, stage = "DECLINED", "provider"
            elif call == "ResourceLimit":
                cause, message, legacy, stage = "resource", args[-1], "UNKNOWN", "provider"
            elif call in {"native_diagnostic", "native_arm_diagnostic"}:
                if len(args) < 4:
                    continue
                stage, legacy, message = args[1:4]
                cause = args[4] if len(args) == 5 else "error"
            elif call == "pipeline_status":
                if len(args) != 4:
                    continue
                legacy, stage, message = args[1:]
                cause = legacy
            elif call == "pipeline_error":
                if len(args) != 5:
                    continue
                legacy, stage, message = args[2:]
                cause = legacy
            elif call == "check_failure":
                if len(args) != 4:
                    continue
                legacy, stage, message = args[1:]
                cause = legacy
            elif call == "oxidd_record_failure":
                if len(args) != 6:
                    continue
                cause, stage, message = args[1:4]
                legacy = "solver inconclusive"
            elif call == "fail":
                if len(args) == 3 and args[1].startswith("OXIDD_FAILURE_"):
                    cause, stage, message, legacy = args[1], "variable order", args[2], "UNKNOWN"
                elif args[0].startswith("FailureKind::"):
                    cause, stage, message, legacy = args[0], "closure provider", "typed failure", "UNKNOWN"
                else:
                    continue
            elif call == "require":
                cause, stage, message, legacy = "invalid_query", "verifier", args[0], "UNKNOWN"
            elif call in {"worker_decline", "worker_stopped"}:
                cause, stage, message, legacy = call, "worker", args[0], "UNKNOWN"
            else:
                continue
            if legacy.endswith("_OK"):
                continue
            rows.append(dict(file=name, line=source.count("\n", 0, match.start()) + 1,
                             call=call, legacy=compact(legacy), cause=compact(cause),
                             stage=compact(stage), message=compact(message)))
        for match in re.finditer(r"\bthrow\s+(std::\w+|(?:detail::)?Failure|AdapterFailure)\s*([({])", source):
            if match[2] == "(" and match[1] == "Failure":
                continue  # already enumerated above
            if match[2] == "(":
                _, args = arguments(source, match.end())
                message = compact(args[0])
                cause = match[1]
            else:
                end = source.find(";", match.end())
                message = compact(source[match.end():end].rstrip("}"))
                cause = message
            rows.append(dict(file=name, line=source.count("\n", 0, match.start()) + 1,
                             call="throw " + match[1], legacy="UNKNOWN", cause=cause,
                             stage="provider", message=message))
        # Boolean validators/contract predicates are counted independently of their
        # enclosing status adapter. False helpers may also denote logical mismatch.
        if name.endswith(("gr1_check.c", "gr1_service.c", "native_proof_binding.cc", "pipeline_source.c")):
            for match in re.finditer(r"\breturn\s+(false|CHECK_\w+)\s*;", source):
                if match[1] == "CHECK_VERIFIED":
                    continue
                prefix = source[:match.start()]
                masked = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/',
                                lambda m: " " * len(m[0]), source, flags=re.S)
                depth, levels = 0, []
                for char in masked:
                    levels.append(depth)
                    depth += (char == "{") - (char == "}")
                header = [h for h in re.finditer(
                    r"^([\w &:<>*]+)\b(\w+)\s*\([^;{}]*\)\s*\{", prefix, re.M)
                    if levels[h.start()] == (0 if name.endswith(".c") else 1)]
                function = header[-1][2] if header else "validator"
                context = compact(" ".join(prefix.splitlines()[-3:]))
                rows.append(dict(file=name, line=source.count("\n", 0, match.start()) + 1,
                                 call="return", legacy=match[1], cause=function,
                                 stage=function, message=context))
    occurrences = {}
    for row in rows:
        key = " | ".join(str(row[field]) for field in
                         ("file", "call", "legacy", "stage", "message"))
        row["occurrence"] = occurrences.get(key, 0)
        occurrences[key] = row["occurrence"] + 1
    return rows


def identity(row: dict) -> str:
    return " | ".join(str(row[key]) for key in ("file", "call", "legacy", "stage", "message", "occurrence"))


def generate() -> str:
    import json
    expected = json.loads(EXPECTATIONS.read_text())
    rows = []
    for row in origins():
        entry = expected[identity(row)]
        rows.append(f"| {row['file']}:{row['line']} | `{row['legacy']}` | `{row['cause']}` | "
                    f"{entry['telemetry']} | `{row['stage']}` | `{row['message']}` |")
    return (
        "# Decline and stop causes\n\n"
        "Generated by `python3 tests/check-decline-causes.py`. Legacy return/fallback is "
        "independent of the required diagnostic cause. The independent expectations in "
        "`tests/attribution-cause-expectations.json` assert every enumerated origin's cause "
        "and telemetry class in `test_attribution_fault_inventory.py`. New origins fail the "
        "inventory check until reviewed. This includes boolean validators beneath status "
        "adapters, rather than only lifting throw constructors.\n\n"
        "Runtime panels reach original validators using corrupted frontend signals, APs, "
        "completed Spot monitors, transition labels, reduction provenance, trusted games and "
        "proof artifacts. `native_contract_validators` tests all game-validator checks in all "
        "three seed solvers and every checker status/verdict and solver-failure-kind pair. "
        "`native_env_contract_validators` reaches the four U game/policy invariant checks. "
        "`attribution-contracts` checks serialized events and terminal results at every "
        "Acacia preparation/reduction/proof boundary, with diagnostics-off comparisons. "
        "`native_failure_census` tests every numbered R/U/seed/binding fault, including "
        "verified recovery. `spot-lazy-buchi-view` tests provider corruption/resource hooks "
        "and all closure-provider cause conversions on serialized lifecycle packets. "
        "The per-origin source assertions cover branches that are not individually forced "
        "at runtime; this census does not claim branch coverage.\n\n"
        "DECLINE means genuine applicability/unsupported input or a completed logical "
        "certificate rejection. STOPPED/error means integrity, invalid input, internal or "
        "operational failure. Limits, allocation/capacity, deadlines and cancellation stop "
        "with their own cause. CHECK_OK/INVALID and CHECK_OK/UNKNOWN are errors; only "
        "CHECK_OK/CERT_FAILED or REFUTED decline. Earlier non-applicability causes survive "
        "when all retries/search alternatives fail. A successful retry clears the cause.\n\n"
        "## Fault and corruption hook census\n\n"
        + (ROOT / "tests/attribution-hook-census.md").read_text() + "\n"
        "## Validator, contract and failure-origin census\n\n"
        f"{len(rows)} enumerated checks/origins across preparation, reduction, seeds, R, U, "
        "checking, native proof binding, OxiDD and Spot providers. A slash-separated class "
        "means the source retains a typed failure from its called operation; status matrices "
        "and runtime fault panels test the alternatives. Boolean helpers report through "
        "the named consumer; false does not itself emit telemetry.\n\n"
        "| Origin | Legacy result | Cause expression/helper | Expected telemetry | Stage | Check/message |\n"
        "|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    table = generate()
    if args.check:
        if OUTPUT.read_text() != table:
            raise SystemExit("decline cause census is stale; regenerate it")
    else:
        OUTPUT.write_text(table)


if __name__ == "__main__":
    main()
