"""Synthesis-to-shield plumbing for the grid-world example.

Everything here used to live in the cells of ``example_shield_rl.ipynb``.  It
moved out so the notebook can be read as an argument rather than as a program:
its cells now show the three steps of the talk --- state the properties,
complement them and hand them to Spot, solve the safety game and expose its
boundary --- and this module carries the parts that are the same every time.

``modem-talk/sim/gen_rollouts.py`` imports from here too, so the animations and
the notebook cannot drift apart.

Spot is optional.  It is needed only to draw the automaton; the game, the
shield and the learning loop do not touch it.
"""

from __future__ import annotations

import random
import re
import subprocess
from collections import defaultdict
from typing import Iterable, Sequence

import acacia_boomslang as ab

from finite_mdp import ACTIONS, INPUT_APS, OUTPUT_APS, GridWorld, ProductState


def negated(formula: str) -> str:
    """Complement an LTL formula.

    Acacia wants an automaton for the *bad* plays, so every specification is
    negated on the way in.  In LTL that is the whole operation.
    """

    return f"!({formula})"


def build_game(
    formula: str,
    k: int,
    *,
    inputs: Sequence[str] = INPUT_APS,
    outputs: Sequence[str] = OUTPUT_APS,
):
    """Translate, preprocess and solve the ``k``-co-Buchi safety game.

    Returns ``(game, result)``.  ``result.is_real()`` answers realizability and
    ``result.get_winning_region()`` is the boundary the shield is built from.
    """

    game = ab.create_twa(negated(formula), list(inputs), list(outputs))
    ab.preprocess_aut_standard(game, k_max=k)
    ab.set_bool_thresh_no_bool_states(game, k_max=k)
    result = ab.solve_acacia_safety_game(game, k_max=k, k_min=k, k_inc=1)
    return game, result


class WinningRegionShield:
    """Map movement proposals to encodings and retain winning successors."""

    def __init__(self, world: GridWorld, game, result):
        if not result.is_real():
            raise ValueError("a shield requires a realizable game")
        self.world = world
        self.game = game
        self.result = result  # Keep the native owner alive.
        self.region = result.get_winning_region()
        self.k = self.region.k()

    def initial(self):
        return ab.get_initial_state(self.game)

    def successor(self, state_vector, state: ProductState, action: str):
        valuation = self.world.valuation(state, action)
        true_aps = [name for name, value in valuation.items() if value]
        false_aps = [name for name, value in valuation.items() if not value]
        return ab.successor(
            self.game,
            state_vector,
            ab.StringVector(true_aps),
            ab.StringVector(false_aps),
            self.k,
        )

    def allowed(self, state_vector, state: ProductState,
                actions: Iterable[str] = ACTIONS) -> list[str]:
        return [
            action
            for action in actions
            if self.region.contains(self.successor(state_vector, state, action))
        ]

    def choose(self, state_vector, state: ProductState, proposal: str,
               mode: str = "permissive"):
        allowed = self.allowed(state_vector, state)
        if not allowed:
            raise RuntimeError("no action remains inside the winning region")
        if mode == "fixed":
            action = allowed[0]
        elif mode == "permissive":
            action = proposal if proposal in allowed else allowed[0]
        else:
            raise ValueError(mode)
        return action, self.successor(state_vector, state, action), action != proposal


# --------------------------------------------------------------------------
# Drawing.  Spot only.
# --------------------------------------------------------------------------

def ucb_dot(game, *, layout: str = ".v", labels: bool = True) -> str:
    """Spot's drawing of the preprocessed automaton, as Graphviz source.

    ``layout=".v"`` is Spot's vertical option, which lays this automaton out as
    a band about three times wider than tall; the default ``rankdir=LR`` gives a
    column nothing can display.

    ``labels=False`` strips the edge labels.  Each is a cube over all nine
    atomic propositions, and Graphviz widens the drawing to fit them: with
    labels the joint automaton lays out roughly eleven metres across.  The strip
    touches edge lines only --- removing the graph label and the state names as
    well leaves Graphviz nothing to draw and it exits non-zero.
    """

    try:
        import spot
    except ImportError as exc:  # pragma: no cover - environment-dependent
        raise RuntimeError(
            "drawing the automaton needs Spot's Python bindings; the image "
            "ghcr.io/gaperez64/acacia-boomslang has them, and a local "
            "virtualenv needs Spot's site-packages on PYTHONPATH"
        ) from exc

    drawing = spot.automaton(ab.get_aut_hoa(game)).to_str("dot", layout)
    if labels:
        return drawing
    return "\n".join(
        re.sub(r' \[label="[^"]*"\]', "", line) if "->" in line else line
        for line in drawing.splitlines()
    )


def dot_svg(source: str) -> str:
    """Render Graphviz source, for `IPython.display.SVG`."""

    return subprocess.run(
        ["dot", "-Tsvg"], input=source,
        capture_output=True, text=True, check=True,
    ).stdout


def ucb_svg(game, **kwargs) -> str:
    return dot_svg(ucb_dot(game, **kwargs))


def formula_svg(formula: str, *, layout: str = ".v") -> str:
    """Draw the Buchi automaton of an LTL formula, labels and all.

    For the small single-guarantee automata, where the labels are the point.
    """

    import spot

    aut = spot.translate(spot.formula(formula), "BA", "sbacc")
    return dot_svg(aut.to_str("dot", layout))


# --------------------------------------------------------------------------
# Using the shield.
# --------------------------------------------------------------------------

def consecutive_wait_envelope(world: GridWorld, shield: WinningRegionShield):
    """The allowed sets while the learner proposes nothing but ``wait``.

    Stops at the first refusal, so ``len(...) - 1`` is how many steps in a row
    the shield lets the learner sit still.
    """

    state = world.initial_state(blocked=False)
    vector = shield.initial()
    allowed_by_step = []
    while True:
        allowed = shield.allowed(vector, state)
        allowed_by_step.append(allowed)
        if "wait" not in allowed:
            return allowed_by_step
        vector = shield.successor(vector, state, "wait")


def compare_mode(world: GridWorld, shield: WinningRegionShield, mode: str, *,
                 seed: int = 11, steps: int = 240):
    """Random proposals through the shield; returns interventions/collisions/tours."""

    rng = random.Random(seed)
    state = world.initial_state(blocked=False)
    vector = shield.initial()
    interventions = collisions = tours = 0
    for _ in range(steps):
        proposal = rng.choice(ACTIONS)
        action, next_vector, changed = shield.choose(vector, state, proposal, mode=mode)
        transition = world.sample_successor(state, action, rng)
        interventions += int(changed)
        collisions += int(transition.collision)
        tours += int(transition.completed)
        state, vector = transition.state, next_vector
    return interventions, collisions, tours


def train(world: GridWorld, shield: WinningRegionShield | None, *,
          seed: int, episodes: int = 240, horizon: int = 64) -> dict:
    """Tabular Q-learning, optionally behind the shield.

    Returns per-episode series plus the final table.  Two details are quirks
    rather than choices, and both stay because they are what produced the
    numbers the slides quote: the update writes ``q[key][proposal_index]`` even
    when the shield overrode the proposal, and an overridden step falls back to
    the best *allowed* action rather than the first one.
    """

    policy_rng = random.Random(seed)
    q = defaultdict(lambda: [0.0] * len(ACTIONS))
    rewards, collisions, tours, interventions = [], [], [], []

    for episode in range(episodes):
        environment_rng = random.Random(seed * 100_000 + episode)
        state = world.initial_state(blocked=False)
        vector = shield.initial() if shield else None
        totals = {"reward": 0.0, "collisions": 0, "tours": 0, "interventions": 0}
        epsilon = max(0.04, 0.35 * (1.0 - episode / episodes))

        for _ in range(horizon):
            key = (state.cell, state.blocked, state.progress)
            if policy_rng.random() < epsilon:
                proposal_index = policy_rng.randrange(len(ACTIONS))
            else:
                proposal_index = max(range(len(ACTIONS)), key=q[key].__getitem__)
            proposal = ACTIONS[proposal_index]

            if shield:
                allowed = shield.allowed(vector, state)
                if proposal in allowed:
                    action = proposal
                else:
                    action = max(allowed, key=lambda item: q[key][ACTIONS.index(item)])
                next_vector = shield.successor(vector, state, action)
            else:
                action, next_vector = proposal, None

            transition = world.sample_successor(state, action, environment_rng)
            reward = transition.reward
            next_state = transition.state
            next_key = (next_state.cell, next_state.blocked, next_state.progress)
            old = q[key][proposal_index]
            target = reward + 0.94 * max(q[next_key])
            q[key][proposal_index] = old + 0.24 * (target - old)

            totals["reward"] += reward
            totals["collisions"] += int(transition.collision)
            totals["tours"] += int(transition.completed)
            totals["interventions"] += int(action != proposal)
            state, vector = next_state, next_vector

        rewards.append(totals["reward"])
        collisions.append(totals["collisions"])
        tours.append(totals["tours"])
        interventions.append(totals["interventions"])

    return {
        "reward": rewards,
        "collisions": collisions,
        "tours": tours,
        "interventions": interventions,
        "q": q,
    }
