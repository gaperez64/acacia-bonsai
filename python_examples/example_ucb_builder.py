"""
Showcase for issue #72: reimplement the `UCBBuilder.py` helper from
synth-learn (https://github.com/mrudu/synth-learn) entirely in Python, using
the acacia_boomslang bindings — no subprocess call to the acacia binary, no
parsing of --K output.

Reference implementation:
    https://github.com/mrudu/synth-learn/blob/master/LTLsynthesis/UCBBuilder.py

Mapping from the reference to this implementation
-------------------------------------------------
Reference                              | Our port
---------------------------------------|------------------------------------
subprocess call + ANTICHAIN parsing    | ab.create_twa +
                                       |   solve_acacia_safety_game
spot.automata(AUTOMATA section)        | ab.get_aut_hoa +
                                       |   spot.automaton
self.num_states                        | ab.num_states(game)
self.antichain_heads (list of lists)   | WinningRegion (kept as-is) +
                                       |   WinningRegion.contains
get_bdd_propositions(aps)              | unchanged — uses spot.automaton +
                                       |   buddy.bdd_ithvar exactly like
                                       |   the reference
get_transition_state(v, edge_label)    | ab.successor +
                                       |   classify_by_cube, no BDD needed
is_safe(v)                             | WinningRegion.contains

The only places where BDDs are still used are when the caller actually
wants to operate on BDDs (build cubes, enumerate minterms). Our
`successor` takes true/false AP lists instead of a BDD, so the whole
transition-state computation runs without touching buddy.
"""
from __future__ import annotations

import itertools
from dataclasses import dataclass
from typing import Iterable, List, Optional

import acacia_boomslang as ab

# The reference builds BDDs via the Spot/Buddy Python bindings. We keep
# this optional — callers that only need `get_transition_state` and
# `is_safe` don't need to import buddy at all.
try:
    import buddy as _buddy
    import spot as _spot
except ImportError:                     # pragma: no cover
    _buddy = None
    _spot = None


@dataclass
class UCB:
    """Python-only reimplementation of synth-learn's UCB wrapper.

    Attributes
    ----------
    k           : the safety bound the game was solved at
    psi         : the (negated) LTL formula the UCB was built for
    inputs      : list of input AP names, in registration order
    outputs     : list of output AP names, in registration order
    game        : the underlying ab.Game (keeps TWA + BDD dict
                  alive for the lifetime of this UCB)
    winreg      : ab.WinningRegion — plays the role of the
                  antichain_heads field of the reference implementation
    num_states  : convenience copy of ab.num_states(game)
    """

    k: int
    psi: str
    inputs: List[str]
    outputs: List[str]
    game: "ab.Game"
    winreg: "ab.WinningRegion"  # keeps the parent GameResult alive via ._owner
    num_states: int

    # -------- construction --------

    @classmethod
    def build(cls, psi: str, inputs: List[str], outputs: List[str],
              k: int = 2, limit: int = 10) -> Optional["UCB"]:
        """Mimic the reference's build_UCB: try increasing k until the
        spec is realisable at that bound (or we run out of budget)."""
        while k <= limit:
            game = ab.create_twa(psi, inputs, outputs)
            ab.preprocess_aut_standard(game, k_max=k)
            ab.set_bool_thresh_no_bool_states(game, k_max=k)
            result = ab.solve_acacia_safety_game(
                game, k_max=k, k_min=min(2, k), k_inc=1)
            if result.is_real():
                return cls(
                    k=k, psi=psi, inputs=inputs, outputs=outputs,
                    game=game, winreg=result.get_winning_region(),
                    num_states=ab.num_states(game))
            k += 1
        return None

    # -------- API compatible with synth-learn's UCBBuilder.UCB --------

    def get_bdd_propositions(self, aps: Iterable[str]):
        """Enumerate the 2**n full assignments of the given APs as BDDs.

        Only needed if the caller wants to hand BDDs to spot; see the
        cube-based variant below which is enough for `get_transition_state`.
        """
        if _buddy is None or _spot is None:
            raise RuntimeError("spot/buddy Python bindings are required for "
                               "get_bdd_propositions")
        # Need a spot automaton whose BDD dict was used to register these
        # APs. We parse our own HOA output; the dicts are separate but the
        # AP names match, which is all BDDs need to resolve.
        aut = _spot.automaton(ab.get_aut_hoa(self.game))
        props = [_buddy.bdd_ithvar(aut.register_ap(p)) for p in aps]
        n = len(props)
        bdd_list = []
        for mask in range(1 << n):
            cube = _buddy.bddtrue
            for i in range(n):
                lit = props[i] if (mask >> (n - 1 - i)) & 1 \
                    else _buddy.bdd_not(props[i])
                cube &= lit
            bdd_list.append(cube)
        return bdd_list

    def get_transition_state(self, state_vector: List[int],
                             true_aps: Iterable[str],
                             false_aps: Iterable[str]) -> List[int]:
        """Forward-simulate the UCB by one step under an IO cube.

        Replaces the BDD-based signature of the reference's method:
        instead of taking an `edge_label` BDD we accept the two literal
        lists that describe the cube, which is strictly more Pythonic and
        drops the buddy dependency for the common case.
        """
        v = ab.make_vector(self.game, ab.IntVector(state_vector))
        s = ab.successor(self.game, v,
                         ab.StringVector(list(true_aps)),
                         ab.StringVector(list(false_aps)),
                         self.k)
        return [s[i] for i in range(len(s))]

    def is_safe(self, state_vector: List[int]) -> bool:
        """Whether `state_vector` is still inside the winning region,
        which is exactly what the reference's antichain-head containment
        check computes."""
        v = ab.make_vector(self.game, ab.IntVector(state_vector))
        return self.winreg.contains(v)

    # -------- helpers useful for callers --------

    def initial_state_vector(self) -> List[int]:
        v = ab.get_initial_state(self.game)
        return [v[i] for i in range(len(v))]

    def iter_io_cubes(self):
        """Yield every full IO assignment as (true_aps, false_aps). The
        caller can pass each pair to get_transition_state to compute all
        possible one-step successors."""
        all_aps = self.inputs + self.outputs
        for bits in itertools.product([False, True], repeat=len(all_aps)):
            t = [a for a, b in zip(all_aps, bits) if b]
            f = [a for a, b in zip(all_aps, bits) if not b]
            yield t, f


# -------------------------- demo --------------------------

def _format_cube(t, f):
    return " & ".join([*t, *(f"!{a}" for a in f)]) or "tt"


def demo():
    psi = "!((G (F (req))) -> (G (F (grant))))"
    ucb = UCB.build(psi, ["req"], ["grant"], k=2)
    assert ucb is not None, "demo spec should be realisable at k=2"
    print(f"UCB built at k={ucb.k}: {ucb.num_states} states, "
          f"{len(ucb.winreg)} antichain heads.")

    v0 = ucb.initial_state_vector()
    print(f"initial state vector: {v0}")
    print(f"is_safe(v0) = {ucb.is_safe(v0)}")

    # Enumerate all one-step successors (UCBBuilder's typical use case).
    print("\nOne-step successors for every IO:")
    for t, f in ucb.iter_io_cubes():
        succ = ucb.get_transition_state(v0, t, f)
        print(f"  IO {_format_cube(t, f):>15}  ->  {succ}   "
              f"safe={ucb.is_safe(succ)}")


if __name__ == "__main__":
    demo()
