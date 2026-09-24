"""Emit untrusted target candidates and accept only independent checks."""
from __future__ import annotations

import json
import pathlib
import time
from dataclasses import dataclass

from acacia_lift.artifact import Aag, AagBuilder, _certificate_sidecar, _policy_sidecar
from acacia_lift.direct import Decline, run_command, sha256_file
from acacia_lift.tools import ToolConfiguration
from .schema import Bdds, GameInstance
from .settings import CHECKER_NODE_CAP, POLICY_PROOF_FRACTION


@dataclass(frozen=True)
class LiftedResult:
    game_sha256: str
    certificate: pathlib.Path
    certificate_sha256: str
    policy: pathlib.Path | None
    policy_sha256: str | None
    proof_method: str
    reduction_semantics: str
    checker: dict
    stages: dict


def _move_literals(bdds: Bdds, target: GameInstance, builder: AagBuilder,
                   predicates: dict, depths: list[int],
                   current: dict[int, int], memo: dict[int, int]) -> dict[str, int]:
    """Compose exact target transitions in the AIG; no seed move is trusted."""
    game = target.game
    nstate = len(game.latches)
    gates = {lhs // 2: (left, right) for lhs, left, right in game.gates}
    base = {row[0] // 2: current[i] for i, row in enumerate(game.latches)}
    base.update({literal // 2: current[nstate + i]
                 for i, literal in enumerate(game.inputs)})
    imported = {}
    def visit(literal: int) -> int:
        if literal < 2:
            return literal
        if literal & 1:
            return visit(literal ^ 1) ^ 1
        variable = literal // 2
        if variable in base:
            return base[variable]
        if variable not in imported:
            left, right = gates[variable]
            imported[variable] = builder.land(visit(left), visit(right))
        return imported[variable]
    following = {i: visit(row[1]) for i, row in enumerate(game.latches)}
    following.update({nstate + i: current[nstate + i]
                      for i in range(len(game.inputs))})
    bad = visit(game.bad[0]) if game.bad else 0
    next_memo = {}
    def now(function) -> int:
        return bdds.to_aag_literals(builder, function, current, memo)
    def later(function) -> int:
        return bdds.to_aag_literals(builder, function, following, next_memo)
    fairness = [bdds.game_literal(game, literal) for literal in game.fairness]
    result = {}
    for goal, depth in enumerate(depths):
        at_goal = predicates["inv"] & predicates[f"goal_{goal}"]
        move = builder.land(now(at_goal), builder.land(bad ^ 1,
                                                      later(predicates["inv"])))
        covered = at_goal
        for level in range(depth):
            strict = (at_goal if level == 0 else
                      at_goal | predicates[f"y_{goal}_{level - 1}"])
            for fair in range(max(1, target.fairness)):
                x = predicates[f"x_{goal}_{level}_{fair}"]
                fair_pred = fairness[fair] if fairness else bdds.buddy.bddtrue
                rank_target = strict | (bdds.buddy.bdd_not(fair_pred) & x)
                layer = x & bdds.buddy.bdd_not(covered)
                move = builder.lor(move, builder.land(
                    now(layer), builder.land(bad ^ 1, later(rank_target))))
                covered |= x
        result[f"move_{goal}"] = move
    return result


def emit_certificate(bdds: Bdds, target: GameInstance, predicates: dict,
                     depths: list[int], output: pathlib.Path) -> pathlib.Path:
    path = output / "lifted.certificate.aag"
    names = [*target.game.latch_names, *target.game.input_names]
    builder = AagBuilder(names)
    current = {index: 2 * (index + 1) for index in range(len(names))}
    memo = {}
    moves = _move_literals(bdds, target, builder, predicates, depths, current, memo)
    order = ["inv"]
    order.extend(f"goal_{j}" for j in range(len(target.goals)))
    order.extend(f"y_{j}_{k}" for j, depth in enumerate(depths)
                 for k in range(depth))
    order.extend(f"x_{j}_{k}_{i}" for j, depth in enumerate(depths)
                 for k in range(depth) for i in range(max(1, target.fairness)))
    order.extend(f"move_{j}" for j in range(len(target.goals)))
    outputs = [(name, bdds.to_aag_literals(builder, predicates[name], current, memo)
                if name in predicates else moves[name])
               for name in order]
    path.write_text(builder.render(outputs, "frontend-provenance lifted certificate"),
                    encoding="utf-8")
    metadata = _certificate_sidecar(target, path, depths, len(outputs), len(builder.gates))
    metadata["side"] = "system"
    metadata["reduction_semantics"] = target.files.data["semantics"]
    pathlib.Path(str(path) + ".json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def emit_policy(bdds: Bdds, target: GameInstance, certificate: pathlib.Path,
                output: pathlib.Path, deadline: float) -> pathlib.Path:
    """Global lowest-index Skolemization of the certified move relation."""
    if time.monotonic() >= deadline:
        raise Decline("policy", "budget_exhausted")
    game = target.game
    cert = Aag.read(certificate)
    nstate, ngoals = len(game.latches), len(target.goals)
    public_width = nstate + len(game.inputs)
    start = bdds.width
    bdds._grow(start + ngoals + public_width)
    public_to_policy = {index: start + ngoals + index for index in range(public_width)}
    moves = [bdds.relabel(bdds.from_aag(cert, cert.output(f"move_{j}")),
                          public_to_policy) for j in range(ngoals)]
    goals = [bdds.relabel(bdds.from_aag(cert, cert.output(f"goal_{j}")),
                          public_to_policy) for j in range(ngoals)]
    curr = [bdds.buddy.bdd_ithvar(start + j) for j in range(ngoals)]
    any_curr = bdds.buddy.bddfalse
    for bit in curr:
        any_curr |= bit
    effective = [curr[0] | bdds.buddy.bdd_not(any_curr), *curr[1:]]
    relation = bdds.buddy.bddfalse
    for j in range(ngoals):
        relation |= effective[j] & moves[j]
    controls = [public_to_policy[nstate + p]
                for p, name in enumerate(game.input_names)
                if name.startswith("controllable_")]
    functions = []
    chosen = bdds.buddy.bddtrue
    control_cube = bdds.cube(controls)
    for control in controls:
        if time.monotonic() >= deadline:
            raise Decline("policy", "budget_exhausted")
        function = bdds.buddy.bdd_exist(
            relation & chosen & bdds.buddy.bdd_ithvar(control), control_cube)
        functions.append(function)
        bit = bdds.buddy.bdd_ithvar(control)
        chosen &= (bit & function) | (bdds.buddy.bdd_not(bit) &
                                      bdds.buddy.bdd_not(function))
    next_curr = []
    for j in range(ngoals):
        advance = effective[j] & goals[j]
        previous = (j + ngoals - 1) % ngoals
        next_curr.append((effective[j] & bdds.buddy.bdd_not(advance)) |
                         (effective[previous] & goals[previous]))
    uncontrollable = [(p, name) for p, name in enumerate(game.input_names)
                      if not name.startswith("controllable_")]
    policy_inputs = [*game.latch_names, *(f"curr_{j}" for j in range(ngoals)),
                     *(name for _p, name in uncontrollable)]
    builder = AagBuilder(policy_inputs)
    mapping = {public_to_policy[i]: 2 * (i + 1) for i in range(nstate)}
    mapping.update({start + j: 2 * (nstate + j + 1) for j in range(ngoals)})
    mapping.update({public_to_policy[nstate + p]: 2 * (nstate + ngoals + j + 1)
                    for j, (p, _name) in enumerate(uncontrollable)})
    memo = {}
    names = [name for name in game.input_names if name.startswith("controllable_")]
    outputs = [(name, bdds.to_aag_literals(builder, function, mapping, memo))
               for name, function in zip(names, functions, strict=True)]
    outputs.extend((f"curr_next_{j}", bdds.to_aag_literals(builder, function,
                                                          mapping, memo))
                   for j, function in enumerate(next_curr))
    path = output / "lifted.policy.aag"
    path.write_text(builder.render(outputs, "global lowest-index Skolemization"),
                    encoding="utf-8")
    meta = _policy_sidecar(target, path, len(builder.gates))
    meta["side"] = "system"
    meta["reduction_semantics"] = target.files.data["semantics"]
    pathlib.Path(str(path) + ".json").write_text(
        json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def _check(config: ToolConfiguration, method: str, target: GameInstance,
           certificate: pathlib.Path, policy: pathlib.Path | None,
           output: pathlib.Path, deadline: float) -> tuple[bool, dict, int]:
    path = output / f"check-{method}.json"
    command = [str(config.checker), "--method", method, "--timeout",
               str(max(0.001, deadline - time.monotonic())),
               "--node-cap", str(CHECKER_NODE_CAP), "--json-out", str(path),
               "--certificate", str(certificate), "--certificate-json",
               str(certificate) + ".json", str(target.files.game)]
    if policy is not None:
        command.append(str(policy))
    try:
        result = run_command(command, deadline, f"check_{method}")
    except Decline as error:
        if error.reason == "budget_exhausted":
            return False, {"reason": "deadline"}, 124
        raise
    payload = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}
    if method == "certificate":
        verified = (result.returncode == 0 and
                    payload.get("verdict") == "VERIFIED" and
                    payload.get("methods", {}).get("certificate", {}).get("verdict") == "VERIFIED")
    else:
        verified = (result.returncode == 0 and
                    result.stdout.strip().splitlines()[-1:] == ["REGION_VERIFIED"] and
                    payload.get("format") == "tlsf-gr1-region-checkresult-v1" and
                    payload.get("method") == "gr1-region-v1" and
                    payload.get("verdict") == "REGION_VERIFIED" and
                    payload.get("exit_code") == 0)
    return verified, payload, result.returncode


def _capacity_or_deadline(code: int, payload: dict) -> bool:
    if code == 124:
        return True
    text = json.dumps(payload, sort_keys=True).lower()
    return any(word in text for word in ("capacity", "node cap", "node_cap",
                                         "out of memory", "timeout", "deadline"))


def prove(bdds: Bdds, target: GameInstance, predicates: dict,
          depths: list[int], output: pathlib.Path, config: ToolConfiguration,
          deadline: float) -> LiftedResult:
    stages = {}
    started = time.monotonic()
    game_hash = sha256_file(target.files.game)
    certificate = emit_certificate(bdds, target, predicates, depths, output)
    certificate_hash = sha256_file(certificate)
    stages["certificate_export"] = {"elapsed_s": time.monotonic() - started}
    policy = None
    policy_deadline = time.monotonic() + max(
        0.0, deadline - time.monotonic()) * POLICY_PROOF_FRACTION
    try:
        started = time.monotonic()
        policy = emit_policy(bdds, target, certificate, output, policy_deadline)
        stages["policy_export"] = {"elapsed_s": time.monotonic() - started}
    except (Decline, RuntimeError, MemoryError, OverflowError) as error:
        recoverable = (isinstance(error, (MemoryError, OverflowError)) or
                       isinstance(error, Decline) and error.reason == "budget_exhausted" or
                       any(word in str(error).lower() for word in
                           ("capacity", "memory", "allocation", "node cap")))
        if not recoverable:
            raise
        stages["policy_export"] = {"elapsed_s": time.monotonic() - started,
                                   "failure": type(error).__name__}
    if policy is not None:
        policy_hash = sha256_file(policy)
        started = time.monotonic()
        verified, payload, code = _check(config, "certificate", target,
                                         certificate, policy, output, policy_deadline)
        stages["policy_check"] = {"elapsed_s": time.monotonic() - started,
                                  "exit_code": code}
        if verified:
            if (sha256_file(target.files.game) != game_hash or
                    sha256_file(certificate) != certificate_hash or
                    sha256_file(policy) != policy_hash):
                raise Decline("target_check", "artifact_changed")
            return LiftedResult(game_hash, certificate, certificate_hash,
                                policy, policy_hash, "certificate",
                                target.files.data["semantics"], payload, stages)
        # A refuted candidate is not rescued by a weaker check. Region is a
        # capacity fallback for this same candidate only.
        if not _capacity_or_deadline(code, payload):
            raise Decline("target_check", "certificate_not_verified")
    if time.monotonic() >= deadline:
        raise Decline("target_check", "budget_exhausted")
    started = time.monotonic()
    verified, payload, code = _check(config, "region", target, certificate,
                                     None, output, deadline)
    stages["region_check"] = {"elapsed_s": time.monotonic() - started,
                              "exit_code": code}
    if (not verified or sha256_file(target.files.game) != game_hash or
            sha256_file(certificate) != certificate_hash):
        raise Decline("target_check", "region_not_verified")
    return LiftedResult(game_hash, certificate, certificate_hash,
                        None, None, "gr1-region-v1",
                        target.files.data["semantics"], payload, stages)
