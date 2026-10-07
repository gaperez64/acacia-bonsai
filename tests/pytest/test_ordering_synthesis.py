"""Exact synthesis checks, with inherited Moore defects tracked separately."""

import importlib.util
import os
from pathlib import Path

import acacia_boomslang
import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "variable_order_checks", ROOT / "tests/check-variable-order.py")
CHECKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKS)


@pytest.fixture(scope="module")
def tools():
    # CI's Python module and CLI share a build. An explicit path also permits
    # checking a different prepared executable without searching stale builds.
    binary = Path(os.environ.get(
        "ACACIA_ORDER_BINARY",
        str(Path(acacia_boomslang.__file__).resolve().parents[1] / "acacia-bonsai")))
    checker = binary.parent.parent / "tests/controller-check"
    help_result = CHECKS.run(binary, "--help")
    assert help_result.returncode == 0, help_result.stderr
    if b"--tlsf" not in help_result.stdout:
        pytest.skip("this build has no native TLSF frontend")
    assert checker.is_file(), "build the controller-check test helper"
    return binary, checker


@pytest.fixture
def controllers(request, tools, tmp_path):
    label = request.param
    if label == "minimal-delay":
        source = ('INFO { TITLE: "minimal delay" SEMANTICS: Moore TARGET: Moore } '
                  'MAIN { INPUTS { x; } OUTPUTS { p; } GUARANTEE { G (X p <-> x); } }')
        specification = ("Moore", "G(X p <-> x)", "x", "p")
    else:
        source = next(source for name, source, _ in CHECKS.cases() if name == label)
        specification = CHECKS.SYNTHESIS_CASES[label]
    path = tmp_path / "source.tlsf"
    path.write_text(source)
    binary, checker = tools
    results = []
    for order in CHECKS.ORDERS:
        controller = tmp_path / f"{order}.aag"
        result = CHECKS.run(binary, "--var-order", order, "-T", path, "-s", controller)
        assert result.returncode == 0, (order, result.stdout, result.stderr)
        results.append((result.returncode, controller))
    return label, specification, checker, results


@pytest.mark.parametrize("controllers", ["copy", "indexed", "delay", "minimal-delay"],
                         indirect=True)
def test_synthesis_order_independence(controllers):
    _, specification, checker, results = controllers
    assert all(result == results[0][0] for result, _ in results)
    if specification[0] == "Moore":
        assert all(path.read_bytes() == results[0][1].read_bytes() for _, path in results)
    else:
        for _, path in results:
            CHECKS.validate_controller(checker, path, *specification)


@pytest.mark.xfail(
    strict=True, raises=CHECKS.ControllerSemanticError,
    reason="Pre-existing Moore synthesis defect; separate investigation in cov/wt-moore-synth "
           "(P2a review REPORT.md, finding 1). Remove this xfail when that fix lands.")
@pytest.mark.parametrize("controllers", ["delay", "minimal-delay"], indirect=True)
@pytest.mark.parametrize("order_index", range(len(CHECKS.ORDERS)), ids=CHECKS.ORDERS)
def test_moore_synthesis_semantics(controllers, order_index):
    _, specification, checker, results = controllers
    CHECKS.validate_controller(checker, results[order_index][1], *specification)


@pytest.mark.parametrize("semantics,output,formula,expected", [
    ("Mealy", 2, "G(p <-> x)", 0),
    ("Mealy", 0, "G(p <-> x)", 1),
    ("Mealy", 0, "G F p", 1),
    ("Mealy", 0, "G F !p", 0),
    ("Moore", 2, "G(p <-> x)", 2),
])
def test_exact_checker_controls(tools, tmp_path, semantics, output,
                                                  formula, expected):
    _, checker = tools
    controller = tmp_path / "control.aag"
    controller.write_text(f"aag 1 1 0 1 0\n2\n{output}\ni0 x\no0 p\n")
    result = CHECKS.run(checker, controller, semantics, formula, "x", "p")
    assert result.returncode == expected, (result.stdout, result.stderr)


def test_exact_checker_accepts_stored_moore_input(tools, tmp_path):
    _, checker = tools
    controller = tmp_path / "stored-input.aag"
    controller.write_text("aag 2 1 1 1 0\n2\n4 2\n4\ni0 x\no0 p\n")
    CHECKS.validate_controller(checker, controller, "Moore", "G(X p <-> x)", "x", "p")
