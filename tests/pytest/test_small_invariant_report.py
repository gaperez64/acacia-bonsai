"""Tests for benchmarking/small-invariant-report.py."""

import importlib.util
import pathlib

import pytest

SCRIPT = (
    pathlib.Path(__file__).resolve().parents[2]
    / "benchmarking/small-invariant-report.py"
)


def load_module():
    spec = importlib.util.spec_from_file_location("small_invariant_report", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize(
    "instance,expected",
    [
        ("amba_decomposed_lock16.ltl", "amba_decomposed_lock"),
        ("amba_decomposed_lock_pb_16_pe_.ltl", "amba_decomposed_lock"),
        ("workstation_resupply_pb_3_pe_.ltl", "workstation_resupply"),
        ("Morning_f2774e0b.ltl", "Morning"),
        ("prioritized_arbiter7.ltl", "prioritized_arbiter"),
        ("f-real-real.ltl", "f-real-real"),
        ("OneCounter.ltl", "OneCounter"),
    ],
)
def test_family_collapses_every_parameter_spelling(instance, expected):
    assert load_module().family(instance) == expected


@pytest.mark.parametrize(
    "mode,columns",
    [
        ("core", ["core_maxima", "core_contains_init", "verified"]),
        ("kernel", ["kernel_maxima", "verified", "budget", "search_nodes"]),
        ("width", ["width", "contains_init", "matches_full_width"]),
    ],
)
def test_probe_mode_is_detected_from_the_header(mode, columns):
    module = load_module()

    assert module.detect_mode(["suite", "instance", "worker", *columns]) == mode


def core_row(name, checkpoint, solver_final, contains_init="yes", core=1):
    return {
        "suite": "syntcomp24",
        "instance": name,
        "worker": "real",
        "solver_final_maxima": solver_final,
        "checkpoint_maxima": str(checkpoint),
        "core_maxima": str(core),
        "core_contains_init": contains_init,
        "verified": "yes",
    }


def test_early_success_must_be_strictly_before_solver_convergence():
    module = load_module()
    rows = [
        core_row("early.ltl", 63, "64"),
        core_row("at-final.ltl", 64, "64"),
    ]

    assert [row["instance"] for row in module.early_core_rows(rows)] == ["early.ltl"]


def test_unconverged_solver_rows_are_excluded_from_early_success():
    module = load_module()
    rows = [
        core_row("converged.ltl", 63, "64"),
        core_row("timed-out.ltl", 1, "none"),
    ]

    assert [row["instance"] for row in module.early_core_rows(rows)] == [
        "converged.ltl"
    ]
