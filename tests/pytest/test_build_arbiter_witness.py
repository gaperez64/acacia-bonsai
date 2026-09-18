"""Unit tests for the manual arbiter-family witness generator (pure Python,
no Spot dependency -- the AIGER text it emits is checked against Spot
separately, interactively, since this repo's spot-dependent pytest tests are
not currently runnable in this environment's default venv; see
test_verify_conjuncts.py for the formula-decomposition tests, which are
import-guarded for exactly that reason)."""

import importlib.util
import sys
from pathlib import Path

import pytest


SCRIPT = (Path(__file__).resolve().parents[2] / "benchmarking" / "witness-lifting-20260918" /
          "families" / "proposals" / "build_arbiter_witness.py")
SPEC = importlib.util.spec_from_file_location("build_arbiter_witness", SCRIPT)
witness = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = witness
SPEC.loader.exec_module(witness)


# ---------- AigerBuilder gate algebra ----------

def test_and_identities():
    b = witness.AigerBuilder()
    x = b.new_var()
    assert b.AND(0, x) == 0
    assert b.AND(x, 0) == 0
    assert b.AND(1, x) == x
    assert b.AND(x, 1) == x
    assert b.AND(x, x) == x
    assert b.AND(x, b.NOT(x)) == 0
    assert b.AND(b.NOT(x), x) == 0


def test_and_creates_and_caches_a_real_gate():
    b = witness.AigerBuilder()
    x, y = b.new_var(), b.new_var()
    g1 = b.AND(x, y)
    assert g1 not in (0, 1, x, y)
    assert len(b.gates) == 1
    g2 = b.AND(y, x)  # commuted operand order
    assert g2 == g1
    assert len(b.gates) == 1  # reused, not re-created


def test_or_de_morgan():
    b = witness.AigerBuilder()
    x = b.new_var()
    assert b.OR(0, 0) == 0
    assert b.OR(1, x) == 1  # OR(true, anything) is the true constant
    assert b.OR(x, b.NOT(x)) == 1  # excluded middle, however it is represented


def test_not_is_an_involution():
    b = witness.AigerBuilder()
    x = b.new_var()
    assert b.NOT(b.NOT(x)) == x


def test_latch_next_must_be_set_before_render():
    b, inputs, outputs, n = witness.build(2)
    for _, nxt in b.latches:
        assert nxt is not None


# ---------- build(n) structural invariants ----------

def test_build_rejects_nonpositive_n():
    with pytest.raises(ValueError):
        witness.build(0)
    with pytest.raises(ValueError):
        witness.build(-1)


@pytest.mark.parametrize("n", [1, 2, 3, 4, 5, 7, 10])
def test_build_latch_and_io_counts(n):
    b, inputs, outputs, returned_n = witness.build(n)
    assert returned_n == n
    assert len(inputs) == n
    assert len(outputs) == n
    # n `pending` latches + (n-1) one-hot `hot` latches.
    assert len(b.latches) == 2 * n - 1


def test_n_equals_1_pointer_is_always_at_position_0():
    # The n=1 edge case the plan's arithmetic/schema test category asks for:
    # with no `hot` latches at all, position 0 must be the NOR of an empty
    # set, i.e. constant true, not an error or an unconstrained bit.
    b, inputs, outputs, n = witness.build(1)
    assert len(b.latches) == 1  # just `pending[0]`; no `hot` latches
    grant_0 = outputs[0]
    pending_0_lit = b.latches[0][0]
    # grant_0 = AND(at(0), pending_0) = AND(1, pending_0) = pending_0,
    # simplified away by the AND(1, x) identity -- no gate needed for it.
    assert grant_0 == pending_0_lit


@pytest.mark.parametrize("n", [1, 2, 3, 4, 5, 7, 10])
def test_render_header_matches_structure(n):
    b, inputs, outputs, _ = witness.build(n)
    text = witness.render(b, inputs, outputs, n)
    header = text.splitlines()[0].split()
    assert header[0] == "aag"
    max_var, n_in, n_latch, n_out, n_and = (int(x) for x in header[1:])
    assert max_var == b.next_var - 1
    assert n_in == n
    assert n_latch == 2 * n - 1
    assert n_out == n
    assert n_and == len(b.gates)


@pytest.mark.parametrize("n", [1, 2, 3, 5])
def test_render_symbol_table_names_every_ap(n):
    b, inputs, outputs, _ = witness.build(n)
    text = witness.render(b, inputs, outputs, n)
    lines = text.splitlines()
    input_syms = {line.split()[1] for line in lines if line.startswith("i")}
    output_syms = {line.split()[1] for line in lines if line.startswith("o")}
    assert input_syms == {f"r_{i}" for i in range(n)}
    assert output_syms == {f"g_{i}" for i in range(n)}


def test_render_is_deterministic():
    b1, i1, o1, n1 = witness.build(6)
    b2, i2, o2, n2 = witness.build(6)
    assert witness.render(b1, i1, o1, n1) == witness.render(b2, i2, o2, n2)


def test_different_n_are_structurally_distinct():
    # A generator bug that silently reused a cached circuit across calls
    # would be invisible from output alone; hash the rendered text instead.
    seen = set()
    for n in (1, 2, 3, 4, 5):
        b, inputs, outputs, _ = witness.build(n)
        seen.add(witness.render(b, inputs, outputs, n))
    assert len(seen) == 5
