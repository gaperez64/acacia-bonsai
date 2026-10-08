"""Checks for the independent circuit/DFA product used by Moore synthesis tests."""

import importlib.util
from pathlib import Path
import sys

import pytest

SPEC = importlib.util.spec_from_file_location(
    "moore_aiger_oracle", Path(__file__).resolve().parents[1] / "moore_aiger_oracle.py")
oracle = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = oracle
SPEC.loader.exec_module(oracle)


def circuit(tmp_path, text):
    path = tmp_path / "controller.aag"
    path.write_text(text)
    return oracle.Aiger.read(path)


def monitor(delay=1, preset=None):
    return oracle.delayed_table_monitor(["x"], "p", (False, True), delay, preset)


def test_detects_lost_initial_input(tmp_path):
    # The reviewed controller: ready'=1, memory'=ready & x, p=memory.
    aig = circuit(tmp_path, "aag 4 1 2 1 1\n2\n4 1\n6 8\n6\n8 2 4\ni0 x\no0 p\n")
    result = oracle.check(aig, *monitor())
    assert not result.valid
    assert result.counterexample == [{"x": True, "p": False}, {"x": False, "p": False}]
    assert not result.input_dependent


@pytest.mark.parametrize("reset", ["", " 0", " 1", " 4"])
def test_exact_correct_delay_and_latch_resets(tmp_path, reset):
    aig = circuit(tmp_path, f"aag 2 1 1 1 0\n2\n4 2{reset}\n4\ni0 x\no0 p\n")
    assert oracle.check(aig, *monitor()).valid
    if reset in (" 1", " 4"):
        assert not oracle.check(aig, *monitor(preset=False)).valid


def test_output_uses_old_latch_and_current_input(tmp_path):
    aig = circuit(tmp_path, "aag 3 1 1 1 1\n2\n4 2\n6\n6 4 3\ni0 x\no0 p\n")
    assert aig.step((True,), (False,)) == ((True,), (False,))
    assert aig.step((False,), (True,)) == ((False,), (True,))


def test_mealy_output_dependency_rejected_for_moore(tmp_path):
    aig = circuit(tmp_path, "aag 1 1 0 1 0\n2\n2\ni0 x\no0 p\n")
    result = oracle.check(aig, *monitor(delay=0))
    assert not result.valid and result.input_dependent
    assert oracle.check(aig, *monitor(delay=0), moore=False).valid


def test_semantically_constant_input_gate_is_moore(tmp_path):
    aig = circuit(tmp_path, "aag 2 1 0 1 1\n2\n4\n4 2 3\ni0 x\no0 p\n")
    assert oracle.check(aig, (), lambda state, letter: ()).valid


def test_state_cap_never_reports_success(tmp_path):
    aig = circuit(tmp_path, "aag 2 1 1 1 0\n2\n4 2\n4\ni0 x\no0 p\n")
    with pytest.raises(RuntimeError, match="state cap"):
        oracle.check(aig, *monitor(), max_states=1)


@pytest.mark.parametrize("row", ["4 2 2", "4 2 3", "4 2 5 6"])
def test_invalid_reset_rejected(tmp_path, row):
    with pytest.raises(ValueError):
        circuit(tmp_path, f"aag 2 1 1 1 0\n2\n{row}\n4\ni0 x\no0 p\n")


def test_input_shift_preserves_unconstrained_initial_round(tmp_path):
    # G(X p <-> X x) constrains p_t=x_t for t>=1; input_0 is unused.
    aig = circuit(tmp_path, "aag 3 1 1 1 1\n2\n4 1\n6\n6 2 4\ni0 x\no0 p\n")
    shifted = oracle.delayed_table_monitor(["x"], "p", (False, True), 0, start_step=1)
    assert oracle.check(aig, *shifted, moore=False).valid
    assert not oracle.check(aig, *monitor(delay=0), moore=False).valid
