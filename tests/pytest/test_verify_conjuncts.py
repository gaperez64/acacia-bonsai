"""Unit tests for the exact conjunct-decomposition rewrite (plan section 7.2).

Needs Spot's Python bindings, which are not importable from this repo's
default `.venv` in this environment (a pre-existing Python-version mismatch:
`.venv` is 3.14, the installed spot module is compiled for 3.13 -- unrelated
to this sprint). Skipped rather than xfail'd when unavailable, since it is an
environment gap, not a code defect; run directly with a matching interpreter
(e.g. `python3.13`, with `PYTHONPATH`/`LD_LIBRARY_PATH` pointed at spot's
site-packages and libspot) to actually exercise these.
"""

import importlib.util
import sys
from pathlib import Path

import pytest

spot = pytest.importorskip("spot")

SCRIPT = (Path(__file__).resolve().parents[2] / "benchmarking" / "witness-lifting-20260918" /
          "families" / "proposals" / "verify_conjuncts.py")
SPEC = importlib.util.spec_from_file_location("verify_conjuncts", SCRIPT)
vc = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = vc
SPEC.loader.exec_module(vc)


def f(text):
    return spot.formula(text)


def equivalent(f1, f2):
    """Two formulas denote the same language iff their XOR is unsatisfiable."""
    return spot.translate(spot.formula.Xor(f1, f2)).is_empty()


def conjunction_matches_original(original, pieces):
    """The pieces, ANDed back together, must be exactly the original formula
    -- not just the same truth value on a few sample words."""
    rebuilt = pieces[0] if len(pieces) == 1 else spot.formula.And(pieces)
    return equivalent(original, rebuilt)


# ---------- pure top-level And ----------

def test_flat_and_returns_each_conjunct():
    g = f("Ga & Gb & Gc")
    pieces = vc.top_level_conjuncts(g)
    assert {str(p) for p in pieces} == {"Ga", "Gb", "Gc"}


def test_nested_and_is_flattened():
    # Spot's own parser may already flatten n-ary And, so build a genuinely
    # nested tree by hand to test the recursion, not just Spot's own parsing.
    inner = spot.formula.And([f("Gb"), f("Gc")])
    nested = spot.formula.And([f("Ga"), inner])
    pieces = vc.top_level_conjuncts(nested)
    assert {str(p) for p in pieces} == {"Ga", "Gb", "Gc"}


def test_non_and_atom_is_returned_whole():
    g = f("G(a -> Fb)")
    assert vc.top_level_conjuncts(g) == [g]


# ---------- implication consequent splitting ----------

def test_implication_with_and_consequent_splits_retaining_full_antecedent():
    g = f("Ga -> (Gb & Gc)")
    pieces = vc.top_level_conjuncts(g)
    assert {str(p) for p in pieces} == {"Ga -> Gb", "Ga -> Gc"}


def test_implication_with_non_and_consequent_is_not_split():
    g = f("Ga -> Gb")
    assert vc.top_level_conjuncts(g) == [g]


def test_implication_consequent_split_is_recursive():
    g = f("Ga -> (Gb & (Gc & Gd))")
    pieces = vc.top_level_conjuncts(g)
    assert {str(p) for p in pieces} == {"Ga -> Gb", "Ga -> Gc", "Ga -> Gd"}


def test_and_of_an_implication_and_a_plain_conjunct():
    g = f("(Ga -> (Gb & Gc)) & Gd")
    pieces = vc.top_level_conjuncts(g)
    assert {str(p) for p in pieces} == {"Ga -> Gb", "Ga -> Gc", "Gd"}


# ---------- what must NOT be split (soundness guards) ----------

def test_antecedent_is_never_descended_into():
    # An And *inside the antecedent* must stay intact; only the consequent's
    # top-level And is split.
    g = f("(Ga & Gb) -> Gc")
    assert vc.top_level_conjuncts(g) == [g]


def test_biconditional_is_not_treated_as_an_implication():
    g = f("Ga <-> (Gb & Gc)")
    assert vc.top_level_conjuncts(g) == [g]


def test_implication_whose_consequent_is_itself_an_implication_is_not_split():
    # A -> (B -> C) has an Implies consequent, not an And -- must pass
    # through as one piece, not be misread as ordinary A -> (G1 and G2).
    g = f("Ga -> (Gb -> Gc)")
    assert vc.top_level_conjuncts(g) == [g]


# ---------- exactness: rebuilding must be equivalent to the original ----------

@pytest.mark.parametrize("text", [
    "Ga & Gb & Gc",
    "G(a -> Fb) -> (G(c -> Fd) & G(e -> Ff))",
    "(Ga -> (Gb & Gc)) & (Gd -> (Ge & Gf))",
    "!ga W ra & G((ga & X(!ra & !ga)) -> X(!ga W ra)) & G((ga & G!ra) -> F!ga) & G(ra -> Fga)",
])
def test_decomposition_is_exact(text):
    g = f(text)
    pieces = vc.top_level_conjuncts(g)
    assert conjunction_matches_original(g, pieces)


def test_arbiter_shaped_formula_matches_the_manual_schema_docstring_example():
    # Mirrors the actual round_robin_arbiter n=3 shape this sprint hit
    # (Assume -> And(mutex, per-client fairness)), reduced to 2 clients.
    text = ("G((r0 && !g0 -> Xr0) & (!r0 && g0 -> X!r0) & F!(r0 && g0) & "
            "(r1 && !g1 -> Xr1) & (!r1 && g1 -> X!r1) & F!(r1 && g1)) -> "
            "G(!g0 & !g1) & G(r0 -> Fg0) & G(r1 -> Fg1)")
    g = f(text)
    pieces = vc.top_level_conjuncts(g)
    assert len(pieces) == 3  # 1 mutex-shaped term + one per client
    assert conjunction_matches_original(g, pieces)
    # Every piece must retain the identical antecedent (the full Assume).
    antecedents = {str(p[0]) for p in pieces if p.kind() == spot.op_Implies}
    assert len(antecedents) == 1
