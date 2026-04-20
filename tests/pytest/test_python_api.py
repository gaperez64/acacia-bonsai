"""Tests for the extended Python API added for issues #51 and #72.

These exercise the UCBBuilder-style helpers (successor, make_vector,
get_input_aps / get_output_aps, num_states, state_is_accepting,
initial_state_number) plus the winning-region simulation pieces used by
the example scripts.
"""
import runpy
import sys
from pathlib import Path

import pytest

import acacia_boomslang as ab


SPEC_REAL   = "!((G (F (req))) -> (G (F (grant))))"      # negated GR(1): real
SPEC_UNREAL = "!((G (F (req))) <-> (G(!grant)))"         # negated: unreal

INPUTS  = ["req"]
OUTPUTS = ["grant"]


# ---------- basic introspection ----------

def test_aps_are_preserved():
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    assert list(ab.get_input_aps(g))  == INPUTS
    assert list(ab.get_output_aps(g)) == OUTPUTS


def test_num_states_matches_hoa():
    import spot
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    aut = spot.automaton(ab.get_aut_hoa(g))
    assert ab.num_states(g) == aut.num_states()


def test_initial_state_vector_shape():
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    v = ab.get_initial_state(g)
    n = ab.num_states(g)
    assert len(v) == n
    # Exactly one entry should be 0 (the initial state), the rest -1.
    values = [v[i] for i in range(n)]
    assert values.count(0) == 1
    assert values.count(-1) == n - 1
    assert values[ab.initial_state_number(g)] == 0


def test_make_vector_roundtrip():
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    n = ab.num_states(g)
    entries = [-1, 0, 1][:n] + [0] * max(0, n - 3)
    entries = entries[:n]
    v = ab.make_vector(g, ab.IntVector(entries))
    assert [v[i] for i in range(n)] == entries


def test_make_vector_rejects_wrong_size():
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    with pytest.raises(Exception):
        ab.make_vector(g, ab.IntVector([0]))


# ---------- successor semantics ----------

def test_successor_matches_hoa_forward_simulation():
    """The `successor` helper must compute the same forward update as a
    hand-rolled spot-based simulation."""
    import spot
    import buddy
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    n = ab.num_states(g)
    aut = spot.automaton(ab.get_aut_hoa(g))
    # Register APs in aut so BDDs resolve.
    ap_vars = {p: buddy.bdd_ithvar(aut.register_ap(p)) for p in INPUTS + OUTPUTS}

    def ref_successor(v, true_aps, false_aps, k_cap):
        cube = buddy.bddtrue
        for a in true_aps:
            cube &= ap_vars[a]
        for a in false_aps:
            cube &= buddy.bdd_not(ap_vars[a])
        out = [-1] * n
        for src in range(n):
            if v[src] == -1:
                continue
            for e in aut.out(src):
                if (e.cond & cube) == buddy.bddfalse:
                    continue
                acc = 1 if aut.state_is_accepting(e.dst) else 0
                cand = min(k_cap, v[src] + acc)
                if cand > out[e.dst]:
                    out[e.dst] = cand
        return out

    v0 = [0] * n     # all states simultaneously active
    for tap, fap in [
        (["req"],           ["grant"]),
        (["grant"],         ["req"]),
        (["req", "grant"],  []),
        ([],                ["req", "grant"]),
    ]:
        vw = ab.make_vector(g, ab.IntVector(v0))
        got = ab.successor(g, vw, ab.StringVector(tap), ab.StringVector(fap), 5)
        expected = ref_successor(v0, tap, fap, 5)
        assert [got[i] for i in range(n)] == expected, \
            f"mismatch for T={tap} F={fap}: got {[got[i] for i in range(n)]} vs {expected}"


def test_successor_caps_at_k():
    g = ab.create_twa(SPEC_REAL, INPUTS, OUTPUTS)
    n = ab.num_states(g)
    # Feed a vector that is already at the cap on an accepting state and make
    # sure we never see a value above k_cap coming back.
    v = ab.make_vector(g, ab.IntVector([3] * n))
    for k_cap in [0, 1, 3, 10]:
        s = ab.successor(g, v,
                         ab.StringVector(["req"]),
                         ab.StringVector(["grant"]),
                         k_cap)
        for x in s:
            assert x <= k_cap


# ---------- winning region + solver ----------

def _solve(spec, k):
    g = ab.create_twa(spec, INPUTS, OUTPUTS)
    ab.preprocess_aut_standard(g, k_max=k)
    ab.set_bool_thresh_no_bool_states(g, k_max=k)
    r = ab.solve_acacia_safety_game(g, k_max=k, k_min=min(2, k), k_inc=1)
    return g, r


def test_winreg_contains_initial_state_when_realizable():
    g, r = _solve(SPEC_REAL, k=2)
    assert r.is_real()
    w = r.get_winning_region()
    v = ab.get_initial_state(g)
    assert w.contains(v)


def test_winreg_does_not_contain_overflow_vector():
    g, r = _solve(SPEC_REAL, k=2)
    assert r.is_real()
    w = r.get_winning_region()
    # An "overflow" vector where every state is at the maximum counter
    # should not be in the winning region (it represents having already
    # seen too many accepting visits simultaneously everywhere).
    n = ab.num_states(g)
    bad = ab.make_vector(g, ab.IntVector([99] * n))
    assert not w.contains(bad)


def test_winreg_outlives_gameresult_scope():
    """Regression test for the SWIG lifetime fix: a WinningRegion borrowed
    from a GameResult must remain usable even if only the region is kept."""
    def build_region():
        _, r = _solve(SPEC_REAL, k=2)
        assert r.is_real()
        return r.get_winning_region()
    w = build_region()
    # If _owner wiring is broken this crashes or corrupts memory.
    _ = len(w)
    _ = w.k()


def test_winreg_k_returns_int():
    _, r = _solve(SPEC_REAL, k=2)
    w = r.get_winning_region()
    assert isinstance(w.k(), int)


# ---------- showcase scripts are importable & runnable ----------

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_example_ucb_builder_runs(capsys):
    """Part of the #72 showcase that this CI promises to exercise."""
    sys.path.insert(0, str(REPO_ROOT / "python_examples"))
    try:
        from example_ucb_builder import UCB
        ucb = UCB.build(SPEC_REAL, INPUTS, OUTPUTS, k=2)
        assert ucb is not None
        v0 = ucb.initial_state_vector()
        assert ucb.is_safe(v0)
        for t, f in ucb.iter_io_cubes():
            succ = ucb.get_transition_state(v0, t, f)
            assert isinstance(succ, list)
            assert len(succ) == ucb.num_states
    finally:
        sys.path.remove(str(REPO_ROOT / "python_examples"))


def test_example_simulate_auto_runs(monkeypatch):
    """Part of the #51 showcase: smoke-test the auto path of example_simulate."""
    monkeypatch.setattr(sys, "argv", ["example_simulate.py", "auto"])
    script = REPO_ROOT / "python_examples" / "example_simulate.py"
    with pytest.raises(SystemExit) as excinfo:
        runpy.run_path(str(script), run_name="__main__")
    assert excinfo.value.code == 0
