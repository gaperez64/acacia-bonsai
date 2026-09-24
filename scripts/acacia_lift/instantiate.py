"""Pure helpers for mapping schema templates to target clients."""
from __future__ import annotations

from typing import TYPE_CHECKING

from .artifact import AagBuilder

if TYPE_CHECKING:
    from .generalizer import Bdds, GoalInfo, Instance

def _normal_key(key: tuple, slots: dict[int, int]) -> tuple:
    if key[:2] == ("state", "monitor"):
        _state, _monitor, template, indices, bit = key
        return ("state", "monitor", template,
                tuple(slots[index] for index in indices), bit)
    if key[0] == "letter":
        role, base, indices = key[1:]
        return ("letter", role, base, tuple(slots[index] for index in indices))
    return key


def _ordered_subset(instance: Instance, subset: tuple[int, ...],
                    goal: GoalInfo | None) -> tuple[int, ...]:
    if goal and goal.owner in subset:
        others = sorted((x for x in subset if x != goal.owner),
                        key=lambda x: ((x - goal.owner) % instance.n,
                                       instance.role_by_client[x]))
        return (goal.owner, *others)
    return tuple(sorted(subset, key=lambda x: (instance.role_by_client[x], x)))
def _structured_move_literals(bdds: Bdds, target: Instance,
                              builder: AagBuilder,
                              predicates: dict[str, object],
                              levels: list[int],
                              current_memo: dict[int, int],
                              current_literals: dict[int, int] | None = None
                              ) -> dict[str, int]:
    """Emit exact move relations by circuit-level next-state composition."""
    nstate = len(target.game.latches)
    public_width = nstate + len(target.game.inputs)
    if current_literals is None:
        current_literals = {i: 2 * (i + 1) for i in range(public_width)}
    if set(current_literals) != set(range(public_width)):
        raise ValueError("structured move map does not cover the game ABI")

    # Import the target transition circuit into the certificate builder.  Its
    # current latches and letters are certificate inputs in the public ABI.
    gate = {lhs // 2: (left, right)
            for lhs, left, right in target.game.gates}
    base = {row[0] // 2: current_literals[i]
            for i, row in enumerate(target.game.latches)}
    base.update({literal // 2: current_literals[nstate + p]
                 for p, literal in enumerate(target.game.inputs)})
    imported: dict[int, int] = {}

    def game_literal(literal: int) -> int:
        if literal < 2:
            return literal
        if literal & 1:
            return game_literal(literal ^ 1) ^ 1
        variable = literal // 2
        if variable in base:
            return base[variable]
        if variable in imported:
            return imported[variable]
        left, right = gate[variable]
        result = builder.land(game_literal(left), game_literal(right))
        imported[variable] = result
        return result

    next_literals = [game_literal(row[1]) for row in target.game.latches]
    bad_literal = game_literal(target.game.bad[0]) if target.game.bad else 0
    next_map = {i: literal for i, literal in enumerate(next_literals)}
    next_map.update({nstate + p: current_literals[nstate + p]
                     for p in range(len(target.game.inputs))})
    next_memo: dict[int, int] = {}

    def current(function) -> int:
        return bdds.to_aag_literals(
            builder, function, current_literals, current_memo)

    def following(function) -> int:
        return bdds.to_aag_literals(builder, function, next_map, next_memo)

    _next_state, _bad, _goals, fairness = bdds.game_functions(target.game)
    result = {}
    for j, depth in enumerate(levels):
        at_goal = predicates["inv"] & predicates[f"goal_{j}"]
        move = builder.land(
            current(at_goal), builder.land(bad_literal ^ 1,
                                           following(predicates["inv"])))
        covered = at_goal
        for level in range(depth):
            strict = (at_goal if level == 0 else
                      at_goal | predicates[f"y_{j}_{level - 1}"])
            for fair in range(max(1, target.fairness)):
                x = predicates[f"x_{j}_{level}_{fair}"]
                fair_pred = (fairness[fair] if fairness
                             else bdds.buddy.bddtrue)
                target_pred = strict | (bdds.buddy.bdd_not(fair_pred) & x)
                layer = x & bdds.buddy.bdd_not(covered)
                case = builder.land(
                    current(layer),
                    builder.land(bad_literal ^ 1, following(target_pred)))
                move = builder.lor(move, case)
                covered |= x
        result[f"move_{j}"] = move
    return result


def _structured_arbiter_policy_literals(
        bdds: Bdds, target: Instance, builder: AagBuilder,
        predicates: dict[str, object], levels: list[int]) -> list[int]:
    """Exact lowest-index Skolemization over AtMostOne grant actions."""
    nstate = len(target.game.latches)
    ngoals = len(target.goals)
    uncontrollable = [(p, name) for p, name in enumerate(target.game.input_names)
                      if not name.startswith("controllable_")]
    controllable = [(p, name) for p, name in enumerate(target.game.input_names)
                    if name.startswith("controllable_")]

    # Policy AIG inputs are state, goal counter, then environment letters.
    state_literals = {i: 2 * (i + 1) for i in range(nstate)}
    environment_literals = {
        nstate + p: 2 * (nstate + ngoals + j + 1)
        for j, (p, _name) in enumerate(uncontrollable)}
    curr = [2 * (nstate + j + 1) for j in range(ngoals)]
    any_curr = 0
    for literal in curr:
        any_curr = builder.lor(any_curr, literal)
    effective = [builder.lor(curr[0], any_curr ^ 1), *curr[1:]]

    available = []
    # Canonical action order: grant client 0, 1, ..., n-1, then none.
    for selected in [*range(len(controllable)), None]:
        literal_map = dict(state_literals)
        literal_map.update(environment_literals)
        for index, (p, _name) in enumerate(controllable):
            literal_map[nstate + p] = int(selected == index)
        moves = _structured_move_literals(
            bdds, target, builder, predicates, levels, {}, literal_map)
        relation = 0
        for j in range(ngoals):
            relation = builder.lor(
                relation, builder.land(effective[j], moves[f"move_{j}"]))
        available.append(relation)

    remaining = 1
    grants = []
    for relation in available[:-1]:
        chosen = builder.land(remaining, relation)
        grants.append(chosen)
        remaining = builder.land(remaining, relation ^ 1)

    # The checker fixes the counter update independently of the controller.
    state_map = dict(state_literals)
    goal_memo: dict[int, int] = {}
    goal_literals = [bdds.to_aag_literals(
        builder, predicates[f"goal_{j}"], state_map, goal_memo)
                     for j in range(ngoals)]
    next_curr = []
    for j in range(ngoals):
        advance = builder.land(effective[j], goal_literals[j])
        prev = (j + ngoals - 1) % ngoals
        prev_advance = builder.land(effective[prev], goal_literals[prev])
        next_curr.append(builder.lor(
            builder.land(effective[j], advance ^ 1), prev_advance))
    return [*grants, *next_curr]


def _structured_arbiter_certificate_moves(
        bdds: Bdds, target: Instance, builder: AagBuilder,
        predicates: dict[str, object], levels: list[int]) -> dict[str, int]:
    """Emit the exact relation as a union of all AtMostOne grant actions."""
    nstate = len(target.game.latches)
    uncontrollable = [(p, name) for p, name in enumerate(target.game.input_names)
                      if not name.startswith("controllable_")]
    controllable = [(p, name) for p, name in enumerate(target.game.input_names)
                    if name.startswith("controllable_")]
    base = {i: 2 * (i + 1) for i in range(nstate)}
    base.update({nstate + p: 2 * (nstate + p + 1)
                 for p, _name in uncontrollable})
    control_literals = [2 * (nstate + p + 1)
                        for p, _name in controllable]

    availability = []
    action_cubes = []
    for selected in [*range(len(controllable)), None]:
        literal_map = dict(base)
        cube = 1
        for index, (p, _name) in enumerate(controllable):
            value = selected == index
            literal_map[nstate + p] = int(value)
            literal = control_literals[index]
            cube = builder.land(cube, literal if value else literal ^ 1)
        availability.append(_structured_move_literals(
            bdds, target, builder, predicates, levels, {}, literal_map))
        action_cubes.append(cube)

    result = {}
    for j in range(len(target.goals)):
        relation = 0
        for action, moves in enumerate(availability):
            possible = moves[f"move_{j}"]
            relation = builder.lor(
                relation, builder.land(possible, action_cubes[action]))
        result[f"move_{j}"] = relation
    return result

