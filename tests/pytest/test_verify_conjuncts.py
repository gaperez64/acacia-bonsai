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


# ---------- main() end-to-end: real fixtures, no formula-level mocking ----------

ROOT = Path(__file__).resolve().parents[2]
TLSF2LTL = ROOT / "subprojects" / "tlsf-tools" / "build_nospot" / "tlsf2ltl"
ARBITER_N2_TLSF = (ROOT / "benchmarking" / "witness-lifting-20260918" / "families" / "seeds" /
                   "arbiter" / "arbiter_n2.tlsf")
ARBITER_N2_AAG = (ROOT / "benchmarking" / "witness-lifting-20260918" / "families" / "seeds" /
                  "arbiter" / "controllers" / "arbiter_n2.aag")

needs_tlsf2ltl = pytest.mark.skipif(
    not TLSF2LTL.exists(), reason="tlsf-tools' tlsf2ltl is a local build artifact, not tracked in git"
)


@needs_tlsf2ltl
def test_main_verifies_a_known_good_seed_end_to_end(tmp_path, capsys):
    rc = vc.main(["--aiger", str(ARBITER_N2_AAG), "--tlsf", str(ARBITER_N2_TLSF),
                 "--tlsf2ltl", str(TLSF2LTL)])
    out = capsys.readouterr().out
    assert rc == 0
    assert "verified (all conjuncts)" in out
    assert "top-level conjuncts:" in out


@needs_tlsf2ltl
def test_main_reports_refuted_for_an_always_false_circuit(tmp_path, capsys):
    # aag 2 2 0 2 0: 2 input variables (r_0, r_1), no gates, no latches; both
    # outputs are the constant literal 0 (false). Violates "every request is
    # eventually granted" for the very first request -- must be REFUTED,
    # never falsely "verified".
    bad_aiger = tmp_path / "always_false.aag"
    bad_aiger.write_text(
        "aag 2 2 0 2 0\n2\n4\n0\n0\ni0 r_0\ni1 r_1\no0 g_0\no1 g_1\n", encoding="utf-8"
    )
    rc = vc.main(["--aiger", str(bad_aiger), "--tlsf", str(ARBITER_N2_TLSF),
                 "--tlsf2ltl", str(TLSF2LTL)])
    out = capsys.readouterr().out
    assert rc == 1
    assert "REFUTED" in out
    assert "verified (all conjuncts)" not in out
    assert "counterexample:" in out


@needs_tlsf2ltl
def test_main_reports_missing_aps_and_does_not_attempt_any_check(tmp_path, capsys):
    # Same shape as arbiter_n2's real controller, but its symbol table
    # names a different AP (g_1 renamed to g_missing) than the formula
    # needs -- must be caught before any translate/product work, not
    # silently treated as an unconstrained free variable.
    aiger_text = ARBITER_N2_AAG.read_text().replace("o1 g_1", "o1 g_missing")
    renamed = tmp_path / "renamed.aag"
    renamed.write_text(aiger_text, encoding="utf-8")
    rc = vc.main(["--aiger", str(renamed), "--tlsf", str(ARBITER_N2_TLSF),
                 "--tlsf2ltl", str(TLSF2LTL)])
    out, err = capsys.readouterr()
    assert rc == 2
    assert "g_1" in err
    assert "top-level conjuncts:" not in out  # never got past the AP check


@needs_tlsf2ltl
def test_main_argv_list_matches_sys_argv_invocation(monkeypatch):
    # Regression test for a real gap this session found and fixed: main()
    # used to read sys.argv directly and could not be called with an
    # explicit argv list at all.
    argv = ["--aiger", str(ARBITER_N2_AAG), "--tlsf", str(ARBITER_N2_TLSF),
            "--tlsf2ltl", str(TLSF2LTL)]
    monkeypatch.setattr(sys, "argv", ["verify_conjuncts.py", *argv])
    assert vc.main(argv) == vc.main(None)


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
