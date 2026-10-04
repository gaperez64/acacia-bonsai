"""ASCII AIGER objects and deterministic artifact construction."""
from __future__ import annotations

import dataclasses
import pathlib
import re
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .generalizer import Instance
from collections.abc import Iterable

@dataclasses.dataclass
class Aag:
    path: pathlib.Path
    max_var: int
    inputs: list[int]
    latches: list[tuple[int, int, int]]
    outputs: list[int]
    bad: list[int]
    constraints: list[int]
    justice: list[list[int]]
    fairness: list[int]
    gates: list[tuple[int, int, int]]
    input_names: list[str]
    latch_names: list[str]
    output_names: list[str]
    justice_names: list[str]
    fairness_names: list[str]

    @classmethod
    def read(cls, path: pathlib.Path) -> "Aag":
        lines = path.read_text(encoding="utf-8").splitlines()
        if not lines:
            raise ValueError(f"empty AAG: {path}")
        h = lines[0].split()
        if h[0] != "aag" or len(h) not in (6, 10):
            raise ValueError(f"unsupported AAG header in {path}: {lines[0]}")
        nums = list(map(int, h[1:])) + [0] * (9 - (len(h) - 1))
        m, ni, nl, no, na, nb, nc, nj, nf = nums[:9]
        p = 1

        def take(count: int) -> list[str]:
            nonlocal p
            result = lines[p:p + count]
            if len(result) != count:
                raise ValueError(f"truncated AAG: {path}")
            p += count
            return result

        inputs = [int(x) for x in take(ni)]
        latches = []
        for row in take(nl):
            fields = list(map(int, row.split()))
            latches.append((fields[0], fields[1], fields[2] if len(fields) > 2 else 0))
        outputs = [int(x) for x in take(no)]
        bad = [int(x) for x in take(nb)]
        constraints = [int(x) for x in take(nc)]
        justice_sizes = [int(x) for x in take(nj)]
        justice = [[int(x) for x in take(size)] for size in justice_sizes]
        fairness = [int(x) for x in take(nf)]
        gates = [tuple(map(int, x.split())) for x in take(na)]
        symbols = lines[p:]

        def names(prefix: str, count: int, fallback: str) -> list[str]:
            found: dict[int, str] = {}
            for row in symbols:
                match = re.fullmatch(rf"{prefix}(\d+) (.+)", row)
                if match:
                    found[int(match.group(1))] = match.group(2)
            return [found.get(i, f"{fallback}{i}") for i in range(count)]

        return cls(path, m, inputs, latches, outputs, bad, constraints, justice,
                   fairness, gates, names("i", ni, "i"), names("l", nl, "l"),
                   names("o", no, "o"), names("j", nj, "j"),
                   names("f", nf, "f"))

    def output(self, name: str) -> int:
        try:
            return self.outputs[self.output_names.index(name)]
        except ValueError as exc:
            raise KeyError(name) from exc


class AagBuilder:
    """Deterministic strashed combinational ASCII-AIGER builder."""

    def __init__(self, input_names: Iterable[str]):
        self.input_names = list(input_names)
        self.next_var = len(self.input_names)
        self.gates: list[tuple[int, int, int]] = []
        self.cache: dict[tuple[int, int], int] = {}

    def land(self, a: int, b: int) -> int:
        if a == 0 or b == 0:
            return 0
        if a == 1:
            return b
        if b == 1 or a == b:
            return a
        if a == (b ^ 1):
            return 0
        key = tuple(sorted((a, b)))
        if key in self.cache:
            return self.cache[key]
        self.next_var += 1
        lit = 2 * self.next_var
        self.gates.append((lit, key[0], key[1]))
        self.cache[key] = lit
        return lit

    def lor(self, a: int, b: int) -> int:
        return self.land(a ^ 1, b ^ 1) ^ 1

    def render(self, outputs: list[tuple[str, int]], comment: str) -> str:
        rows = [f"aag {self.next_var} {len(self.input_names)} 0 "
                f"{len(outputs)} {len(self.gates)}"]
        rows.extend(str(2 * (i + 1)) for i in range(len(self.input_names)))
        rows.extend(str(lit) for _name, lit in outputs)
        rows.extend(f"{a} {b} {c}" for a, b, c in self.gates)
        rows.extend(f"i{i} {name}" for i, name in enumerate(self.input_names))
        rows.extend(f"o{i} {name}" for i, (name, _lit) in enumerate(outputs))
        rows.extend(("c", comment))
        return "\n".join(rows) + "\n"

def _certificate_sidecar(target: Instance, path: pathlib.Path,
                         levels: list[int], output_count: int,
                         ands: int) -> dict:
    nstate = len(target.game.latches)
    nu = sum(not name.startswith("controllable_") for name in target.game.input_names)
    nc = len(target.game.input_names) - nu
    state = []
    for index, ((cur, nxt, reset), name) in enumerate(
            zip(target.game.latches, target.game.latch_names, strict=True)):
        state.append({"certificate_input": index, "name": name,
                      "game_latch": index, "game_literal": cur,
                      "next_game_literal": nxt, "reset": reset,
                      "solver_added": False})
    unc, con = [], []
    for p, (lit, name) in enumerate(zip(target.game.inputs,
                                        target.game.input_names, strict=True)):
        item = {"certificate_input": nstate + p, "game_input": p,
                "game_literal": lit, "name": name}
        (con if name.startswith("controllable_") else unc).append(item)
    outputs = [{"name": "inv", "kind": "winning_region"}]
    outputs += [{"name": f"goal_{j}", "kind": "goal", "goal": j}
                for j in range(len(target.goals))]
    outputs += [{"name": f"y_{j}_{k}", "kind": "mu_level", "goal": j,
                 "level": k} for j, count in enumerate(levels) for k in range(count)]
    nfair_disj = max(1, target.fairness)
    outputs += [{"name": f"x_{j}_{k}_{i}", "kind": "nu_level", "goal": j,
                 "level": k, "fairness": i} for j, count in enumerate(levels)
                for k in range(count) for i in range(nfair_disj)]
    outputs += [{"name": f"move_{j}", "kind": "move_relation", "goal": j}
                for j in range(len(target.goals))]
    return {
        "format": "tlsf-gr1-certificate-v1", "status": "realizable",
        "circuit": {"path": path.name, "kind": "ASCII AIGER combinational"},
        "fixpoint": "generalized fixed-arity monitor certificate",
        "counts": {"goals": len(target.goals),
                   "justice_records": len(target.game.justice),
                   "fairness_assumptions": target.fairness,
                   "state_variables": nstate,
                   "original_game_latches": nstate, "sampling_latches": 0,
                   "uncontrollable_inputs": nu, "controllable_inputs": nc,
                   "predicates": output_count,
                   "aig_inputs": nstate + len(target.game.inputs),
                   "aig_latches": 0, "aig_ands": ands,
                   "levels_per_goal": levels},
        "outputs": outputs,
        "goals": [{"goal": j, "justice_record": j, "record_member": 0}
                  for j in range(len(target.goals))],
        "variables": {"state": state, "uncontrollable": unc,
                      "controllable": con},
        "sampling_semantics": "No input-dependent acceptance sampling latches were required.",
        "goal_counter_latches": [
            {"strategy_latch": nstate + j,
             "name": f"__tlsf_gr1_goal_counter_{j}", "goal": j,
             "reset": 0, "effective_initial": j == 0,
             "advance_to_goal": (j + 1) % len(target.goals)}
            for j in range(len(target.goals))],
        "goal_counter_semantics": "All-zero denotes goal 0; goals advance cyclically.",
        "rank_semantics": "y_j_k is definitionally the union of x_j_k_i.",
        "move_semantics": "Generalized pre-Skolem most-permissive move relation.",
    }


def _policy_sidecar(target: Instance, path: pathlib.Path,
                    ands: int) -> dict:
    nstate = len(target.game.latches)
    goals = len(target.goals)
    uncontrollable = [(p, name) for p, name in enumerate(target.game.input_names)
                      if not name.startswith("controllable_")]
    controllable = [(p, name) for p, name in enumerate(target.game.input_names)
                    if name.startswith("controllable_")]
    return {
        "format": "tlsf-gr1-policy-v1",
        "circuit": {"path": path.name, "kind": "ASCII AIGER combinational"},
        "counts": {"game_state_variables": nstate,
                   "original_game_latches": nstate, "sampling_latches": 0,
                   "goals": goals, "uncontrollable_inputs": len(uncontrollable),
                   "controllable_outputs": len(controllable),
                   "aig_inputs": nstate + goals + len(uncontrollable),
                   "aig_outputs": len(controllable) + goals, "aig_ands": ands},
        "inputs": {
            "state": [{"policy_input": j, "game_latch": j, "name": name}
                      for j, name in enumerate(target.game.latch_names)],
            "counter": [{"policy_input": nstate + j, "goal": j,
                         "name": f"curr_{j}", "reset": 0,
                         "effective_initial": j == 0} for j in range(goals)],
            "uncontrollable": [
                {"policy_input": nstate + goals + j, "game_input": p,
                 "name": name} for j, (p, name) in enumerate(uncontrollable)]},
        "outputs": {
            "controllable": [{"policy_output": j, "game_input": p,
                              "name": name}
                             for j, (p, name) in enumerate(controllable)],
            "counter_next": [{"policy_output": len(controllable) + j,
                              "goal": j, "name": f"curr_next_{j}"}
                             for j in range(goals)]},
        "counter_semantics": "All-zero denotes curr_0; advance on the current goal.",
        "skolem_rule": "controllable inputs in game order, true/lowest index first",
    }


def _aag_vector_evaluate(game: Aag, latch_values: list[bool],
                         input_vectors: list[int], mask: int) -> tuple[list[int], list[int]]:
    """Evaluate an AAG for several input valuations packed into Python bits."""
    values = [0] * (game.max_var + 1)
    for (literal, _next, _reset), value in zip(
            game.latches, latch_values, strict=True):
        values[literal // 2] = mask if value else 0
    for literal, value in zip(game.inputs, input_vectors, strict=True):
        values[literal // 2] = value

    def value(literal: int) -> int:
        result = values[literal // 2] if literal >= 2 else (mask if literal else 0)
        return result ^ mask if literal & 1 else result

    for lhs, left, right in game.gates:
        values[lhs // 2] = value(left) & value(right)
    return ([value(row[1]) for row in game.latches],
            [value(record[0]) for record in game.justice])

