"""Exact, per-predicate BDD projection and target instantiation."""
from __future__ import annotations

import itertools
import json
import pathlib
import re
import time
from dataclasses import dataclass
from math import comb

from acacia_lift.artifact import Aag, AagBuilder
from acacia_lift.bdd_kernel import VarInfo
from acacia_lift.buddy_veccompose import BuddyVariableAdapter
from acacia_lift.direct import Decline
from acacia_lift.tools import ToolConfiguration, load_buddy_bindings
from .provenance import (SeedWindow, axis_members, monitor_indices, monitor_key,
                         role_signatures)
from .settings import (MAX_PREDICATE_ARITY, MAX_SUBSETS_PER_PREDICATE,
                       MOVE_SCHEMA_SECONDS)
from .source import InstanceFiles

_STATE_RE = re.compile(r"monitor_(\d+)_state_(\d+)\Z")


@dataclass(frozen=True)
class Goal:
    number: int
    key: tuple
    owner: int | None


@dataclass
class GameInstance:
    files: InstanceFiles
    members: tuple[int, ...]
    game: Aag
    certificate: Aag | None
    metadata: dict | None
    variables: list[VarInfo]
    goals: list[Goal]
    roles: dict[int, tuple]

    @classmethod
    def load(cls, files: InstanceFiles, members: tuple[int, ...],
             certificate: pathlib.Path | None = None) -> "GameInstance":
        game = Aag.read(files.game)
        cert = Aag.read(certificate) if certificate is not None else None
        meta = json.loads(pathlib.Path(str(certificate) + ".json").read_text(
            encoding="utf-8")) if certificate is not None else None
        data = files.data
        monitors = {record["monitor"]: record for record in data["monitors"]}
        if (len(monitors) != len(data["monitors"]) or
                any(record["latch_literals"] and
                    len(record["latch_literals"]) != record["state_count"]
                    for record in data["monitors"])):
            raise Decline("schema_abi", "monitor_inventory")
        roles = role_signatures(files, members)
        variables = []
        for position, name in enumerate(game.latch_names):
            if files.data["semantics"] == "strict" and name == "assumption_safety_violated":
                variables.append(VarInfo(position, ("state", "strict_release"),
                                         frozenset()))
                continue
            match = _STATE_RE.fullmatch(name)
            if match is None or int(match[1]) not in monitors:
                raise Decline("schema_abi", "unsupported_latch")
            record = monitors[int(match[1])]
            structural_key = monitor_key(record, data)
            indices = (() if structural_key[5] == "symmetric" else
                       monitor_indices(record, data))
            owners = frozenset(index for index in indices if index in roles)
            key = ("state", structural_key, indices, int(match[2]))
            variables.append(VarInfo(position, key, owners))
        signals = {row["game_symbol"]: row for row in
                   [*data["inputs"], *data["outputs"]]}
        if len(signals) != len(game.inputs):
            raise Decline("schema_abi", "signal_inventory")
        offset = len(variables)
        for position, name in enumerate(game.input_names):
            row = signals.get(name)
            if row is None:
                raise Decline("schema_abi", "game_signal_unmatched")
            indices = tuple(row["index_tuple"])
            owners = frozenset(index for index in indices
                               if row["index_role"] == "element" and index in roles)
            key = ("letter", row["direction"], row["declaration_id"], indices)
            variables.append(VarInfo(offset + position, key, owners))
        if cert is not None:
            if (len(cert.inputs) != len(variables) or
                    meta.get("counts", {}).get("sampling_latches") != 0 or
                    meta.get("counts", {}).get("original_game_latches") != len(game.latches) or
                    cert.input_names != [*game.latch_names, *game.input_names]):
                raise Decline("schema_abi", "certificate_input_mismatch")
        justice = [record for record in data["monitors"]
                   if record["role"] == "justice"]
        if (files.data["semantics"] == "strict" and not justice and
                len(game.justice) == 1):
            goals = [Goal(0, ("implicit_true_justice",), None)]
            return cls(files, members, game, cert, meta, variables, goals, roles)
        if len(justice) != len(game.justice):
            raise Decline("schema_abi", "justice_inventory")
        goals = []
        for index, record in enumerate(justice):
            indices = tuple(record["source_origin"]["index_tuple"])
            owner = indices[0] if len(indices) == 1 and indices[0] in roles else None
            # Multi-index and bus-wide goal alignment needs a separate semantic
            # alignment proof; it cannot be guessed from justice order.
            if len(indices) > 1:
                raise Decline("schema_abi", "multi_index_goal")
            key = (monitor_key(record, data), roles.get(owner))
            goals.append(Goal(index, key, owner))
        return cls(files, members, game, cert, meta, variables, goals, roles)

    @property
    def fairness(self) -> int:
        return len(self.game.fairness)

    def levels(self, goal: Goal) -> int:
        if self.metadata is None:
            raise Decline("schema_abi", "missing_seed_metadata")
        return int(self.metadata["counts"]["levels_per_goal"][goal.number])


def _normalized(key: tuple, slots: dict[int, int]) -> tuple:
    if key[0] == "state":
        return (key[0], key[1], tuple(slots.get(index, ("fixed", index))
                                        for index in key[2]), key[3])
    if key[0] == "letter":
        return (key[0], key[1], key[2],
                tuple(slots.get(index, ("fixed", index)) for index in key[3]))
    raise ValueError(key)


def _ordered(instance: GameInstance, subset: tuple[int, ...],
             goal: Goal | None) -> tuple[int, ...]:
    if goal is not None and goal.owner in subset:
        return (goal.owner, *sorted((i for i in subset if i != goal.owner),
                                    key=lambda i: (repr(instance.roles[i]), i)))
    return tuple(sorted(subset, key=lambda i: (repr(instance.roles[i]), i)))


class Bdds:
    """One checked BuDDy manager for seed reconstruction and target export."""

    def __init__(self, config: ToolConfiguration, public_width: int):
        buddy, _extension, _binding, extension_path = load_buddy_bindings(
            config.bindings_site)
        self.buddy = buddy
        self.variables = BuddyVariableAdapter(buddy, extension_path)
        if not buddy.bdd_isrunning():
            buddy.bdd_init(4_000_000, 400_000)
            buddy.bdd_setmaxincrease(1_000_000)
        self.width = self.variables.variable_count()
        self._grow(public_width)
        self.normal: dict[tuple, int] = {}

    def _grow(self, width: int) -> None:
        if width > self.width:
            self.variables.set_variable_count(width)
            if self.variables.variable_count() < width:
                raise Decline("schema_capacity", "bdd_variable_limit")
            self.width = width

    def normal_var(self, key: tuple) -> int:
        if key not in self.normal:
            index = self.width
            self._grow(index + 1)
            self.normal[key] = index
        return self.normal[key]

    def support(self, function) -> set[int]:
        node = self.buddy.bdd_support(function)
        result = set()
        while node != self.buddy.bddtrue and node != self.buddy.bddfalse:
            result.add(self.buddy.bdd_var(node))
            node = self.buddy.bdd_high(node)
        return result

    def cube(self, variables) -> object:
        result = self.buddy.bddtrue
        for variable in sorted(set(variables)):
            result &= self.buddy.bdd_ithvar(variable)
        return result

    def relabel(self, function, mapping: dict[int, int]):
        missing = self.support(function) - mapping.keys()
        if missing:
            raise Decline("schema_abi", "unmapped_bdd_support")
        memo = {}
        def visit(node):
            if node == self.buddy.bddtrue or node == self.buddy.bddfalse:
                return node
            key = node.id()
            if key not in memo:
                high = visit(self.buddy.bdd_high(node))
                low = visit(self.buddy.bdd_low(node))
                memo[key] = self.buddy.bdd_ite(
                    self.buddy.bdd_ithvar(mapping[self.buddy.bdd_var(node)]),
                    high, low)
            return memo[key]
        return visit(function)

    def from_aag(self, aag: Aag, literal: int):
        inputs = {value // 2: index for index, value in enumerate(aag.inputs)}
        gates = {lhs // 2: (left, right) for lhs, left, right in aag.gates}
        memo = {}
        def visit(value: int):
            if value == 0:
                return self.buddy.bddfalse
            if value == 1:
                return self.buddy.bddtrue
            if value not in memo:
                if value & 1:
                    result = self.buddy.bdd_not(visit(value ^ 1))
                elif value // 2 in inputs:
                    result = self.buddy.bdd_ithvar(inputs[value // 2])
                else:
                    left, right = gates[value // 2]
                    result = visit(left) & visit(right)
                memo[value] = result
            return memo[value]
        return visit(literal)

    def game_literal(self, game: Aag, literal: int):
        nstate = len(game.latches)
        inputs = {value // 2: nstate + index
                  for index, value in enumerate(game.inputs)}
        latches = {row[0] // 2: index for index, row in enumerate(game.latches)}
        gates = {lhs // 2: (left, right) for lhs, left, right in game.gates}
        memo = {}
        def visit(value: int):
            if value == 0:
                return self.buddy.bddfalse
            if value == 1:
                return self.buddy.bddtrue
            if value not in memo:
                if value & 1:
                    result = self.buddy.bdd_not(visit(value ^ 1))
                elif value // 2 in inputs:
                    result = self.buddy.bdd_ithvar(inputs[value // 2])
                elif value // 2 in latches:
                    result = self.buddy.bdd_ithvar(latches[value // 2])
                else:
                    left, right = gates[value // 2]
                    result = visit(left) & visit(right)
                memo[value] = result
            return memo[value]
        return visit(literal)

    def to_aag_literals(self, builder: AagBuilder, function,
                        literal_map: dict[int, int], memo: dict[int, int]) -> int:
        def visit(node):
            if node == self.buddy.bddfalse:
                return 0
            if node == self.buddy.bddtrue:
                return 1
            key = node.id()
            if key not in memo:
                variable = self.buddy.bdd_var(node)
                if variable not in literal_map:
                    raise Decline("candidate", "unmapped_bdd_variable")
                high = visit(self.buddy.bdd_high(node))
                low = visit(self.buddy.bdd_low(node))
                value = literal_map[variable]
                memo[key] = builder.lor(builder.land(value, high),
                                        builder.land(value ^ 1, low))
            return memo[key]
        return visit(function)


def _template_group(instance: GameInstance, subset: tuple[int, ...],
                    goal: Goal | None) -> tuple:
    roles = tuple(instance.roles[i] for i in subset)
    relation = tuple("goal" if goal is not None and goal.owner == i else "other"
                     for i in subset)
    return roles, relation


def projection(bdds: Bdds, instance: GameInstance, function,
               arity: int, goal: Goal | None, deadline: float) -> dict[tuple, object]:
    if comb(len(instance.members), arity) > MAX_SUBSETS_PER_PREDICATE:
        raise Decline("schema_capacity", "subset_count_limit")
    result = {}
    rebuilt = bdds.buddy.bddtrue
    support = bdds.support(function)
    for selected in itertools.combinations(instance.members, arity):
        if time.monotonic() >= deadline:
            raise Decline("schema", "discovery_budget_exhausted")
        subset = _ordered(instance, selected, goal)
        slots = {index: position for position, index in enumerate(subset)}
        keep = [row for row in instance.variables if row.owners <= set(subset)]
        keep_ids = {row.index for row in keep}
        drop = support - keep_ids
        projected = (bdds.buddy.bdd_exist(function, bdds.cube(drop))
                     if drop else function)
        normal_map = {row.index: bdds.normal_var(_normalized(row.key, slots))
                      for row in keep}
        normalized = bdds.relabel(projected, normal_map)
        group = _template_group(instance, subset, goal)
        if group in result and result[group] != normalized:
            raise Decline("schema", "within_role_projection_disagreement")
        result[group] = normalized
        reverse = {value: key for key, value in normal_map.items()}
        rebuilt &= bdds.relabel(normalized, reverse)
    if rebuilt != function:
        raise Decline("schema", "predicate_not_exactly_reconstructed")
    return result


def learn_predicate(bdds: Bdds, seeds: list[GameInstance],
                    names: list[str], goals: list[Goal | None],
                    deadline: float) -> tuple[dict, int]:
    if len(seeds) != len(names) or len(seeds) != len(goals):
        raise ValueError("seed predicate vectors differ")
    for arity in range(min(MAX_PREDICATE_ARITY, min(len(seed.members) for seed in seeds)) + 1):
        if time.monotonic() >= deadline:
            raise Decline("schema", "discovery_budget_exhausted")
        if not any(len(seed.members) > arity for seed in seeds):
            continue
        observations = []
        try:
            for seed, name, goal in zip(seeds, names, goals, strict=True):
                if seed.certificate is None:
                    raise Decline("schema", "missing_seed_certificate")
                function = bdds.from_aag(seed.certificate,
                                         seed.certificate.output(name))
                observations.append(projection(bdds, seed, function, arity, goal,
                                               deadline))
        except Decline:
            continue
        first = observations[0]
        if all(set(value) == set(first) and
               all(value[group] == first[group] for group in first)
               for value in observations[1:]):
            return first, arity
    raise Decline("schema", "no_bounded_exact_template")


def instantiate(bdds: Bdds, target: GameInstance, templates: dict,
                arity: int, goal: Goal | None, deadline: float):
    if comb(len(target.members), arity) > MAX_SUBSETS_PER_PREDICATE:
        raise Decline("schema_capacity", "target_subset_count_limit")
    result = bdds.buddy.bddtrue
    inverse = {value: key for key, value in bdds.normal.items()}
    for selected in itertools.combinations(target.members, arity):
        if time.monotonic() >= deadline:
            raise Decline("schema", "discovery_budget_exhausted")
        subset = _ordered(target, selected, goal)
        group = _template_group(target, subset, goal)
        if group not in templates:
            raise Decline("instantiate", "missing_role_template")
        slots = {index: position for position, index in enumerate(subset)}
        concrete = {_normalized(row.key, slots): row.index
                    for row in target.variables if row.owners <= set(subset)}
        function = templates[group]
        mapping = {}
        for variable in bdds.support(function):
            key = inverse[variable]
            if key not in concrete:
                raise Decline("instantiate", "missing_target_variable")
            mapping[variable] = concrete[key]
        result &= bdds.relabel(function, mapping)
    return result


def prepare(window: SeedWindow, target_files: InstanceFiles,
            certificates: list[pathlib.Path]) -> tuple[list[GameInstance], GameInstance]:
    if len(certificates) != len(window.instances):
        raise ValueError("certificate count")
    seeds = []
    for files, cert in zip(window.instances, certificates, strict=True):
        _target_members, members = axis_members(target_files, files)
        seeds.append(GameInstance.load(files, members, cert))
    target = GameInstance.load(target_files, window.members)
    if any(len(seed.members) >= len(target.members) for seed in seeds):
        raise Decline("schema_abi", "seed_not_smaller")
    if any(seed.fairness != target.fairness for seed in seeds):
        raise Decline("schema_abi", "fairness_changed")
    if any(set(seed.roles.values()) != set(target.roles.values()) for seed in seeds):
        raise Decline("schema_abi", "role_classes_changed")
    return seeds, target


def learn_certificate(bdds: Bdds, seeds: list[GameInstance],
                      target: GameInstance, deadline: float) -> tuple[dict[str, object], list[int], dict[str, int | str]]:
    predicates = {}
    arities = {}
    templates, arity = learn_predicate(bdds, seeds, ["inv"] * len(seeds),
                                        [None] * len(seeds), deadline)
    predicates["inv"] = instantiate(bdds, target, templates, arity, None, deadline)
    arities["inv"] = arity
    depths = []
    for goal in target.goals:
        aligned = []
        for seed in seeds:
            matches = [item for item in seed.goals if item.key == goal.key]
            if not matches:
                raise Decline("schema_abi", "goal_class_absent")
            aligned.append(matches[0])
        levels = [seed.levels(item) for seed, item in zip(seeds, aligned, strict=True)]
        if len(set(levels)) != 1:
            raise Decline("schema", "rank_depth_changed")
        depth = levels[0]
        depths.append(depth)
        record = target.game.justice[goal.number]
        if len(record) != 1:
            raise Decline("schema_abi", "multi_member_justice")
        predicates[f"goal_{goal.number}"] = bdds.game_literal(target.game, record[0])
        for level in range(depth):
            union = bdds.buddy.bddfalse
            for fair in range(max(1, target.fairness)):
                names = [f"x_{item.number}_{level}_{fair}" for item in aligned]
                templates, arity = learn_predicate(bdds, seeds, names, aligned,
                                                   deadline)
                name = f"x_{goal.number}_{level}_{fair}"
                predicates[name] = instantiate(bdds, target, templates, arity,
                                               goal, deadline)
                arities[name] = arity
                union |= predicates[name]
            predicates[f"y_{goal.number}_{level}"] = union
        move_name = f"move_{goal.number}"
        try:
            move_templates, move_arity = learn_predicate(
                bdds, seeds, [f"move_{item.number}" for item in aligned],
                aligned, min(deadline, time.monotonic() + MOVE_SCHEMA_SECONDS))
            predicates[move_name] = instantiate(bdds, target, move_templates,
                                                move_arity, goal, deadline)
            arities[move_name] = move_arity
        except Decline:
            # The exact target transition relation gives a semantic move
            # construction when a bounded seed move has no stable template.
            # The target checker must still verify the whole certificate.
            arities[move_name] = "exact_target_transition"
    return predicates, depths, arities
