"""Keep the runtime fault-classification panel exhaustive as hooks are added."""

from pathlib import Path
import re
import json
import runpy

import pytest

ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / "subprojects/tlsf-tools/src/lib"
PANEL = ROOT / "subprojects/tlsf-tools/test/unit/native_both_api.cpp"


@pytest.mark.parametrize(
    ("hook", "table"),
    (
        ("env_rank_fault", "env_fault_cases"),
        ("lift_test_fault", "lift_fault_cases"),
        ("both_test_seed_fault", "seed_fault_cases"),
        ("both_test_check_fault", "binding_fault_cases"),
    ),
)
def test_native_fault_classification_inventory(hook: str, table: str) -> None:
    code = "\n".join((LIB / name).read_text() for name in ("gr1_lift.cc", "gr1_env_lift.cc"))
    modes = {int(value) for value in re.findall(rf"\b{hook}\s*==\s*(\d+)", code)}
    panel = PANEL.read_text()
    body = re.search(rf"\b{table}\[\]\s*=\s*\{{(.*?)\n?\}};", panel, re.S)[1]
    tested = (
        {int(value) for value in re.findall(r"\{\s*(\d+)\s*,", body)}
        if table != "binding_fault_cases"
        else {int(value) for value in re.findall(r"\d+", body)}
    )
    assert tested == modes, f"{hook}: missing={modes - tested}, obsolete={tested - modes}"


def test_decline_cause_census_matches_code() -> None:
    generator = runpy.run_path(str(ROOT / "tests/check-decline-causes.py"))
    assert generator["OUTPUT"].read_text() == generator["generate"]()


GENERATOR = runpy.run_path(str(ROOT / "tests/check-decline-causes.py"))
ORIGINS = GENERATOR["origins"]()
EXPECTED_CAUSES = json.loads(GENERATOR["EXPECTATIONS"].read_text())


def telemetry_class(row: dict) -> str:
    """Map the reviewed source cause to the class exercised by runtime panels."""
    c = row["cause"]
    legacy = row["legacy"]
    call = row["call"]
    if call == "return":
        if legacy in ("CHECK_CERT_FAILED", "CHECK_REFUTED"):
            return "DECLINE/applicability"
        if legacy in ("CHECK_INVALID",):
            return "STOPPED/error"
        if legacy == "CHECK_UNKNOWN":
            return "STOPPED/error/resource/deadline/cancelled"
        f = c
        if f in ("next_power_of_two", "gr1_check_node_capacity"):
            return "STOPPED/error"
        if f in (
            "write_result_json",
            "write_region_result_json",
            "emit_controller",
            "compile_aig_roots_seeded",
            "compile_certificate_outputs",
            "prepare_policy_independent",
            "compile_policy_roots",
            "ensure_full_policy",
            "setup_bdds",
            "build_closed_loop",
        ):
            return "STOPPED/error/resource/deadline/cancelled"
        if f in (
            "checked_bdd_eq",
            "checked_satisfiable",
            "reset_in_predicate",
            "certificate_output_is_selected",
        ):
            return "DECLINE/applicability or STOPPED/resource/deadline/cancelled"
        return "STOPPED/error"
    if c in ("worker_decline", "native_failure_category::decline") or c.endswith(
        ("::applicability", "::unsupported_operator", "_UNSUPPORTED", "_DECLINED")
    ):
        return "DECLINE/applicability"
    if c in (
        "worker_stopped",
        "category",
        "cause",
        "kind",
        "reason",
        "*failure",
        "caught ()",
        "result.unknown, result.error",
    ) or c.startswith(("native_failure (", "check_failure_cause(")):
        if c.startswith("check_failure_cause("):
            return "DECLINE/applicability or STOPPED/error/resource/deadline/cancelled"
        if c.startswith("native_failure ("):
            return "DECLINE/applicability or STOPPED/error/resource/deadline/cancelled"
        if c == "category":
            return "DECLINE/applicability or STOPPED/error/resource/deadline/cancelled"
        return "STOPPED/error/resource/deadline/cancelled"
    if c.startswith("solver_failure_cause("):
        return "STOPPED/error/resource/deadline/cancelled"
    if c in ("*search_cause", "e.cause"):
        return "STOPPED/error/resource/deadline/cancelled"
    if c.startswith("!is_applicability("):
        return "DECLINE/applicability or STOPPED/error/resource/deadline/cancelled"
    if c.startswith(("perr.status", "real ?", "unreal ?")):
        return "DECLINE/applicability or STOPPED/error"
    if c.startswith("view.outer"):
        return "STOPPED/error/resource"
    if c.startswith("failure_cause("):
        return "STOPPED/resource/deadline"
    if "failure.kind == OXIDD_FAILURE_CANCELLED" in c:
        return "STOPPED/cancelled/deadline"
    if c in ("resource", "std::bad_alloc", "std::length_error") or c.endswith(
        ("::resource", "_LIMIT", "_BDD", "_HOST", "_ARTIFACT_LIMIT")
    ):
        return "STOPPED/resource"
    if c.endswith(("::deadline", "_DEADLINE")):
        return "STOPPED/deadline"
    if c.endswith(("::cancelled", "::aborted", "_CANCELLED")):
        return "STOPPED/cancelled"
    if c.startswith(("row.status", "status == spot_rows")):
        return "STOPPED/error/resource"
    if c == "Unknown::resource_limit":
        return "STOPPED/resource"
    if c.startswith("FailureKind::") and c.endswith("_limit"):
        return "STOPPED/resource"
    if c in ("FailureKind::injected", "FailureKind::invalid_state", "FailureKind::unexpected"):
        return "STOPPED/error"
    if (
        c == "invalid_query"
        or c.startswith(("std::", "{FailureKind::"))
        or c in ("error", "Unknown::invalid_query")
    ):
        return "STOPPED/error"
    if c.endswith(
        (
            "::error",
            "::integrity",
            "::invalid",
            "_ERROR",
            "_INVALID",
            "_BAD_ARGUMENT",
            "_CONFIGURATION",
            "_CONVERSION",
        )
    ):
        return "STOPPED/error"
    raise ValueError(c)


@pytest.mark.parametrize("origin", ORIGINS, ids=lambda row: f"{row['file']}:{row['line']}")
def test_every_failure_origin_has_reviewed_telemetry_class(origin: dict) -> None:
    entry = EXPECTED_CAUSES[GENERATOR["identity"](origin)]
    assert origin["cause"] == entry["cause"], origin
    assert telemetry_class(origin) == entry["telemetry"], (origin, entry)


def test_cause_expectations_have_no_unenumerated_or_obsolete_checks() -> None:
    assert set(EXPECTED_CAUSES) == {GENERATOR["identity"](row) for row in ORIGINS}


def test_all_acacia_proof_corruptions_have_runtime_telemetry_assertions() -> None:
    panel = runpy.run_path(str(ROOT / "tests/check-attribution-contracts.py"))
    source = (ROOT / "src/native_test_hooks.cc").read_text()
    faults = set(re.findall(r'std::strcmp\s*\(fault, "([^"]+)"\)', source))
    assert faults == set(panel["PROOF_CASES"]) | {"r-decline", "region-method"}


def test_all_reduction_contract_corruptions_have_runtime_telemetry_assertions() -> None:
    panel = runpy.run_path(str(ROOT / "tests/check-attribution-contracts.py"))
    source = (ROOT / "subprojects/tlsf-tools/test/unit/reduction_contract_hooks.cc").read_text()
    modes = set(re.findall(r'\bmode\("([^"]+)"\)', source))
    assert modes == {
        row.fault for row in (*panel["REDUCTION_CASES"], *panel["PREPARATION_CASES"])
    } | {"seed-null", "provenance-recover"}
