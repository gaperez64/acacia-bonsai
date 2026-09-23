#!/usr/bin/env python3
"""Index-aware, bounded GR(1) certificate generalizer.

This is a research driver, not part of tlsf-tools.  It deliberately treats the
monitor provenance as the cross-instance ABI: AIG variable numbers are never
used to align two sizes.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import itertools
import json
import os
import pathlib
import re
import subprocess
import sys
import time
from collections import Counter, defaultdict
from collections.abc import Iterable


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TT = ROOT / "subprojects" / "tlsf-tools"
MONITOR = TT / "scripts" / "gr1_monitor_game.py"
SOLVER = TT / "build-oxidd" / "tlsfsolve"
CHECKER = TT / "build-oxidd" / "tlsfcertcheck"
BINDINGS_PYTHON = (pathlib.Path("/usr/bin/python3.13")
                   if pathlib.Path("/usr/bin/python3.13").exists()
                   else pathlib.Path(sys.executable))
ALIGNMENT = HERE / "m4-alignment.tsv"
SEPARABILITY = HERE / "m4-invariant-separability.tsv"
MOVE_SEPARABILITY = HERE / "m4-move-separability.tsv"
RESULTS = pathlib.Path(os.environ.get(
    "GENERALIZE_GR1_RESULTS", HERE / "m4-results.tsv"))
MAX_CANDIDATES_PER_TARGET = 32
MAX_CEGIS_ROUNDS = 3
VERDICTS = {0: "VERIFIED", 1: "REFUTED", 2: "UNKNOWN", 3: "UNKNOWN",
            4: "INVALID", 5: "INVALID", 6: "UNKNOWN"}


def _trace(message: str) -> None:
    if os.environ.get("GENERALIZE_GR1_TRACE"):
        print(f"generalize_gr1: {message}", file=sys.stderr, flush=True)


@dataclasses.dataclass(frozen=True)
class ProposerLimits:
    max_candidates: int = MAX_CANDIDATES_PER_TARGET
    checker_timeout_s: float = 120.0
    max_cegis_rounds: int = MAX_CEGIS_ROUNDS

    def __post_init__(self) -> None:
        if not 1 <= self.max_candidates <= MAX_CANDIDATES_PER_TARGET:
            raise ValueError(
                f"max_candidates must be in [1, {MAX_CANDIDATES_PER_TARGET}]")
        if self.checker_timeout_s <= 0:
            raise ValueError("checker_timeout_s must be positive")
        if not 0 <= self.max_cegis_rounds <= MAX_CEGIS_ROUNDS:
            raise ValueError(f"max_cegis_rounds must be in [0, {MAX_CEGIS_ROUNDS}]")


@dataclasses.dataclass(frozen=True)
class CandidateSchema:
    family: str
    arity: int
    seeds: tuple[int, ...]
    role_classes: tuple[str, ...]
    bus_schemas: tuple[str, ...]
    template_counts: tuple[tuple[str, int], ...]


@dataclasses.dataclass(frozen=True)
class FamilySpec:
    source: str
    arity: int
    default_seeds: tuple[int, ...]


FAMILIES = {
    "arbiter": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter.tlsf",
        2, (3, 4)),
    "prioritized_arbiter": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/prioritized_arbiter/parametric/prioritized_arbiter.tlsf",
        1, (3, 4)),
    "load_balancer": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/load_balancer/parametric/load_balancer.tlsf",
        2, (2, 3, 4)),
    "arbiter_with_cancel": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter_with_cancel.tlsf",
        2, (2, 3, 4)),
    "collector_v1": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/collector/parametric/collector_v1.tlsf",
        1, (3,)),
    # Second round of arity measurement (m4-invariant-separability.tsv, merged
    # from the round-2 file).  Same footing as the five above: each arity below
    # is measured and constant over every n that solves, not assumed.
    "arbiter_with_buffer": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter_with_buffer.tlsf",
        1, (2, 3, 4)),
    "simple_arbiter_with_hints": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/ltl_with_hints/parametric/simple_arbiter_with_hints.tlsf",
        1, (2, 4, 6)),
    "amba_decomposed_lock": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/amba/amba_decomposed/parametric/amba_decomposed_lock.tlsf",
        1, (2, 3, 4)),
    "abcg_arbiter": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/abcg_arbiter.tlsf",
        2, (2, 3)),
    "arbiter_on_inpchange": FamilySpec(
        "tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/arbiter_on_inpchange.tlsf",
        2, (2, 3, 4)),
}
OUT_OF_SCOPE = frozenset(
    ("round_robin_arbiter", "lift", "amba_decomposed_arbiter"))


class Decline(RuntimeError):
    """A named, evidence-bearing UNKNOWN result."""

    def __init__(self, stage: str, predicate: str, n: int | None, reason: str):
        self.stage = stage
        self.predicate = predicate
        self.n = n
        self.reason = reason
        where = "" if n is None else f" at n={n}"
        super().__init__(f"{stage}: predicate {predicate!r}{where}: {reason}")


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


def _read_tsv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def stable_from(family: str) -> int:
    for row in _read_tsv(ALIGNMENT):
        if row["family"] == family and row["stable_from"]:
            return int(row["stable_from"])
    # The explicit regression is not in the fixed-arity scope, but seed
    # validation is intentionally available before the scope decline.
    if family == "round_robin_arbiter_unreal2":
        return 3
    raise Decline("seed", "stable_from", None,
                  f"family {family!r} has no measured stable regime")


def measured_arity(family: str) -> int | None:
    invariant_values = {int(row["min_k"]) for row in _read_tsv(SEPARABILITY)
                        if row["family"] == family and row["min_k"]}
    if len(invariant_values) != 1:
        return None
    invariant = next(iter(invariant_values))
    move_values = {int(row["min_k"]) for row in _read_tsv(MOVE_SEPARABILITY)
                   if row["family"] == family and row["min_k"]}
    # collector_v1 has only its n=3 invariant measurement; its move relation
    # is reconstructed semantically below.  Wherever a move measurement is
    # present it must agree with the invariant measurement.
    return invariant if not move_values or move_values == {invariant} else None


def measured_role_class_count(family: str) -> int | None:
    for row in _read_tsv(ALIGNMENT):
        if row["family"] != family:
            continue
        values = {int(value) for value in row["role_classes"].split(",")
                  if value}
        return next(iter(values)) if len(values) == 1 else None
    return None


def _provenance_role_class_count(instance: "Instance") -> int:
    inventories: dict[tuple[int, ...], Counter] = defaultdict(Counter)
    for monitor in instance.prov["monitors"]:
        if monitor["arity_kind"] == "local":
            inventories[tuple(monitor["index_tuple"])][monitor["template"]] += 1
    return len({tuple(sorted(inventory.items()))
                for inventory in inventories.values()})


def _run(command: list[str], timeout: float, cwd: pathlib.Path = ROOT) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, cwd=cwd, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              check=False, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        return subprocess.CompletedProcess(command, 124, exc.stdout or "", exc.stderr or "timeout")


def build_game(family: str, n: int, directory: pathlib.Path,
               timeout: float) -> tuple[pathlib.Path, pathlib.Path]:
    spec = FAMILIES[family]
    game = directory / f"{family}_{n}.game.aag"
    prov = directory / f"{family}_{n}.prov.json"
    command = [str(BINDINGS_PYTHON), str(MONITOR), str(ROOT / spec.source),
               "--param", f"n={n}", "--semantics", "exact", "--output",
               str(game), "--provenance-out", str(prov)]
    env = dict(os.environ)
    site = "/usr/local/lib64/python3.13/site-packages"
    env["PYTHONPATH"] = site + (":" + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    try:
        proc = subprocess.run(command, cwd=ROOT, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              check=False, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise Decline("seed", "monitor_game", n, f"timed out after {timeout:g}s")
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout).strip()[-500:]
        raise Decline("seed", "monitor_game", n,
                      f"construction failed with exit {proc.returncode}: {detail}")
    return game, prov


def solve_seed(family: str, n: int, directory: pathlib.Path,
               timeout: float) -> "Instance":
    game, prov = build_game(family, n, directory, timeout)
    cert = directory / f"{family}_{n}.cert.aag"
    policy = directory / f"{family}_{n}.policy.aag"
    command = [str(SOLVER), "--certificate", str(cert),
               "--certificate-json", str(cert) + ".json", "--policy", str(policy),
               "--policy-json", str(policy) + ".json", str(game)]
    proc = _run(command, timeout)
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout).strip()[-500:]
        raise Decline("seed", "tlsfsolve", n,
                      f"seed solve failed with exit {proc.returncode}: {detail}")
    return Instance.load(family, n, game, prov, cert)


def _monitor_state(name: str) -> tuple[int, int] | None:
    match = re.fullmatch(r"monitor_(\d+)_state_(\d+)", name)
    return (int(match.group(1)), int(match.group(2))) if match else None


def _collapse_indices(text: str) -> str:
    return re.sub(r"_i\d+", "_I", text)


def bus_schema(record: dict, n: int) -> str:
    """Semantically name the intentionally small bus schema library."""
    buses = record["support"]["buses"]
    signature = record.get("symmetric_signature")
    if record.get("symmetric") and isinstance(signature, dict):
        parts = []
        for bus, allowed in sorted(signature.items()):
            if allowed == [0]:
                kind = "NoneOf"
            elif allowed == [0, 1]:
                kind = "AtMostOne"
            elif allowed == list(range(1, n + 1)):
                kind = "AtLeastOne"
            else:
                kind = "Counts{" + ",".join(map(str, allowed)) + "}"
            parts.append(f"{kind}({bus})")
        return " & ".join(parts)
    text = _collapse_indices(record["template"])
    scalars = tuple(record["support"]["scalars"])
    bus_names = tuple(sorted(buses))
    if " U " in text and "g_m" in scalars:
        return f"U(NoneOf({','.join(bus_names)}),g_m)"
    if " W " in text:
        return f"W({','.join(bus_names)};{','.join(scalars)})"
    if "XG(" in text and bus_names:
        return f"X(AtMostOne({','.join(bus_names)}))"
    if (text.startswith("G(!idle | X(") and "idle" in scalars and
            bus_names):
        return f"X(AtLeastOne({','.join(bus_names)}))"
    if text.startswith("G(idle | X(") and bus_names:
        return f"X(NoneOf({','.join(bus_names)}))"
    if "G(G!" in text and "FallFinished" in text:
        return f"W(AllSeen({','.join(bus_names)}),allFinished)"
    raise Decline("bus-wide schemas", record["template"], n,
                  "unmatched bus-wide conjunct")


@dataclasses.dataclass
class VarInfo:
    index: int
    key: tuple
    owners: frozenset[int]


@dataclasses.dataclass
class GoalInfo:
    goal: int
    monitor: dict
    owner: int | None
    key: tuple


@dataclasses.dataclass
class Instance:
    family: str
    n: int
    game_path: pathlib.Path
    prov_path: pathlib.Path
    cert_path: pathlib.Path | None
    game: Aag
    prov: dict
    cert: Aag | None
    meta: dict | None
    variables: list[VarInfo]
    goals: list[GoalInfo]
    role_by_client: dict[int, str]
    bus_schemas: tuple[str, ...]

    @classmethod
    def load(cls, family: str, n: int, game_path: pathlib.Path,
             prov_path: pathlib.Path, cert_path: pathlib.Path | None = None) -> "Instance":
        game = Aag.read(game_path)
        prov = json.loads(prov_path.read_text(encoding="utf-8"))
        cert = Aag.read(cert_path) if cert_path else None
        meta = json.loads(pathlib.Path(str(cert_path) + ".json").read_text(
            encoding="utf-8")) if cert_path else None
        monitors = {record["monitor"]: record for record in prov["monitors"]}
        schemas = {mid: bus_schema(record, n) for mid, record in monitors.items()
                   if record["arity_kind"] == "bus_wide"}

        # A role is the index-relative monitor-template inventory touching a
        # client.  This detects load_balancer's special client zero without
        # consulting AIG structure.
        raw_roles: dict[int, list[tuple]] = {i: [] for i in range(n)}
        for record in monitors.values():
            indices = tuple(record["index_tuple"])
            if record["arity_kind"] != "local" or not indices:
                continue
            for value in set(indices):
                if value < n:
                    pattern = tuple("self" if x == value else "other" for x in indices)
                    raw_roles[value].append((record["template"], pattern))
        signatures = {i: tuple(sorted(items)) for i, items in raw_roles.items()}
        unique = {signature: f"role_{pos}" for pos, signature in enumerate(
            sorted(set(signatures.values()), key=repr))}
        role_by_client = {i: unique[signature] for i, signature in signatures.items()}

        variables: list[VarInfo] = []
        if meta:
            records = sorted(meta["variables"]["state"],
                             key=lambda x: x["certificate_input"])
            for item in records:
                parsed = _monitor_state(item["name"])
                if parsed is None:
                    key = ("state", "solver", item["name"])
                    owners = frozenset()
                else:
                    mid, bit = parsed
                    record = monitors[mid]
                    indices = tuple(record["index_tuple"])
                    if record["arity_kind"] == "bus_wide":
                        key = ("state", "bus", schemas[mid], bit,
                               record["state_count"])
                        owners = frozenset()
                    else:
                        key = ("state", "monitor", record["template"], indices, bit)
                        owners = frozenset(indices)
                variables.append(VarInfo(item["certificate_input"], key, owners))
            signal_records = {
                ("controllable_" if output else "") + item["name"]:
                ("output" if output else "input", item["base_name"],
                 tuple(item["index_tuple"]))
                for output, records2 in ((False, prov["inputs"]), (True, prov["outputs"]))
                for item in records2
            }
            for kind in ("uncontrollable", "controllable"):
                for item in meta["variables"][kind]:
                    signal = signal_records.get(item["name"])
                    if signal is None:
                        raise Decline("canonicalize", item["name"], n,
                                      "certificate input is absent from provenance")
                    role, base, indices = signal
                    variables.append(VarInfo(item["certificate_input"],
                                             ("letter", role, base, indices),
                                             frozenset(indices)))
            if sorted(v.index for v in variables) != list(range(len(cert.inputs))):
                raise Decline("canonicalize", "certificate_inputs", n,
                              "sidecar does not cover each circuit input exactly once")
        else:
            # Target certificate inputs follow the public ABI: state then all
            # game inputs.  No in-scope exact monitor has sampled acceptance.
            for index, name in enumerate(game.latch_names):
                mid, bit = _monitor_state(name) or (-1, index)
                record = monitors.get(mid)
                if record is None:
                    key, owners = ("state", "solver", name), frozenset()
                elif record["arity_kind"] == "bus_wide":
                    key = ("state", "bus", schemas[mid], bit, record["state_count"])
                    owners = frozenset()
                else:
                    indices = tuple(record["index_tuple"])
                    key = ("state", "monitor", record["template"], indices, bit)
                    owners = frozenset(indices)
                variables.append(VarInfo(index, key, owners))
            p = len(game.latches)
            prov_signals = {item["name"]: ("input", item["base_name"],
                                            tuple(item["index_tuple"]))
                            for item in prov["inputs"]}
            prov_signals.update({"controllable_" + item["name"]:
                                 ("output", item["base_name"],
                                  tuple(item["index_tuple"]))
                                 for item in prov["outputs"]})
            for name in game.input_names:
                role, base, indices = prov_signals[name]
                variables.append(VarInfo(p, ("letter", role, base, indices),
                                         frozenset(indices)))
                p += 1

        guarantees = [record for record in prov["monitors"]
                      if record["role"] == "justice"]
        goals = []
        for goal, record in enumerate(guarantees):
            indices = tuple(record["index_tuple"])
            owner = indices[0] if record["arity_kind"] == "local" and indices else None
            key = (("bus", schemas[record["monitor"]])
                   if record["arity_kind"] == "bus_wide"
                   else ("local", record["template"],
                         tuple(0 if x == owner else 1 for x in indices)))
            goals.append(GoalInfo(goal, record, owner, key))
        if len(goals) != len(game.justice):
            raise Decline("canonicalize", "goals", n,
                          "provenance justice monitors do not match game justice records")
        return cls(family, n, game_path, prov_path, cert_path, game, prov, cert,
                   meta, variables, goals, role_by_client,
                   tuple(sorted(set(schemas.values()))))

    def levels(self, goal: int) -> int:
        assert self.meta is not None
        return int(self.meta["counts"]["levels_per_goal"][goal])

    @property
    def fairness(self) -> int:
        if self.meta:
            return int(self.meta["counts"]["fairness_assumptions"])
        return len(self.game.fairness)


class Bdds:
    def __init__(self, var_count: int = 8192):
        site = "/usr/local/lib64/python3.13/site-packages"
        if site not in sys.path:
            sys.path.insert(0, site)
        import buddy  # pylint: disable=import-outside-toplevel
        self.buddy = buddy
        if not buddy.bdd_isrunning():
            buddy.bdd_init(8_000_000, 800_000)
            # BuDDy cannot lower this later; set it once for every seed and
            # target.  Do not bdd_done() between instances: Python proxy
            # destructors may still hold references and crash after teardown.
            buddy.bdd_setvarnum(var_count)
            buddy.bdd_setmaxincrease(2_000_000)
        self.var_count = var_count
        self.next_base = var_count // 4
        self.normal_base = var_count // 2
        self.normal: dict[tuple, int] = {}

    def close(self) -> None:
        # The manager is intentionally process-wide; see __init__.
        pass

    def cube(self, variables: Iterable[int]):
        result = self.buddy.bddtrue
        for variable in sorted(set(variables)):
            result &= self.buddy.bdd_ithvar(variable)
        return result

    def from_aag(self, aag: Aag, literal: int):
        gate = {lhs // 2: (left, right) for lhs, left, right in aag.gates}
        input_var = {lit // 2: i for i, lit in enumerate(aag.inputs)}
        memo = {}

        def visit(lit: int):
            if lit == 0:
                return self.buddy.bddfalse
            if lit == 1:
                return self.buddy.bddtrue
            if lit in memo:
                return memo[lit]
            if lit & 1:
                result = self.buddy.bdd_not(visit(lit ^ 1))
            elif lit // 2 in input_var:
                result = self.buddy.bdd_ithvar(input_var[lit // 2])
            else:
                left, right = gate[lit // 2]
                result = visit(left) & visit(right)
            memo[lit] = result
            return result

        return visit(literal)

    def game_functions(self, game: Aag):
        """Compile game literals over public certificate variable indices."""
        nstate = len(game.latches)
        gate = {lhs // 2: (left, right) for lhs, left, right in game.gates}
        input_var = {lit // 2: nstate + i for i, lit in enumerate(game.inputs)}
        latch_var = {row[0] // 2: i for i, row in enumerate(game.latches)}
        memo = {}

        def visit(lit: int):
            if lit == 0:
                return self.buddy.bddfalse
            if lit == 1:
                return self.buddy.bddtrue
            if lit in memo:
                return memo[lit]
            if lit & 1:
                result = self.buddy.bdd_not(visit(lit ^ 1))
            elif lit // 2 in input_var:
                result = self.buddy.bdd_ithvar(input_var[lit // 2])
            elif lit // 2 in latch_var:
                result = self.buddy.bdd_ithvar(latch_var[lit // 2])
            else:
                left, right = gate[lit // 2]
                result = visit(left) & visit(right)
            memo[lit] = result
            return result

        next_state = [visit(row[1]) for row in game.latches]
        bad = visit(game.bad[0]) if game.bad else self.buddy.bddfalse
        goals = [visit(record[0]) for record in game.justice]
        fairness = [visit(lit) for lit in game.fairness]
        return next_state, bad, goals, fairness

    def substitute_state(self, function, next_state: list[object]):
        """Simultaneously substitute the game's next-state functions.

        The installed Python BuDDy wrapper accidentally exposes ``bdd_pair``
        as ``std::pair<bdd,bdd>`` rather than ``bddPair``, so veccompose cannot
        be used.  Sequentially composing state variables directly is wrong:
        a later composition would rewrite variables occurring inside an
        earlier replacement.  Route through a disjoint auxiliary block to
        retain simultaneous-substitution semantics while keeping both passes
        in BuDDy's native compose operation.
        """
        if self.next_base + len(next_state) >= self.normal_base:
            raise OverflowError("next-state auxiliary BDD budget exhausted")
        variables = list(range(len(next_state)))
        temporaries = [self.next_base + variable for variable in variables]
        return self.substitute_variables(
            function, variables, next_state, temporaries)

    def substitute_variables(self, function, variables: list[int],
                             replacements: list[object],
                             temporaries: list[int]):
        if not (len(variables) == len(replacements) == len(temporaries)):
            raise ValueError("substitution vectors have different lengths")
        result = function
        for variable, temporary in zip(variables, temporaries, strict=True):
            result = self.buddy.bdd_compose(
                result, self.buddy.bdd_ithvar(temporary), variable)
        for temporary, replacement in zip(
                temporaries, replacements, strict=True):
            result = self.buddy.bdd_compose(result, replacement, temporary)
        return result

    def cpre(self, target, next_state: list[object], bad,
             controls: list[int], uncontrollable: list[int]):
        step = self.buddy.bdd_not(bad) & self.substitute_state(target, next_state)
        if controls:
            step = self.buddy.bdd_exist(step, self.cube(controls))
        if uncontrollable:
            step = self.buddy.bdd_forall(step, self.cube(uncontrollable))
        return step

    def relabel(self, function, mapping: dict[int, int]):
        memo = {}

        def visit(node):
            if node == self.buddy.bddtrue or node == self.buddy.bddfalse:
                return node
            key = node.id()
            if key in memo:
                return memo[key]
            old = self.buddy.bdd_var(node)
            if old not in mapping:
                raise KeyError(f"BDD variable {old} has no relabelling")
            high = visit(self.buddy.bdd_high(node))
            low = visit(self.buddy.bdd_low(node))
            result = self.buddy.bdd_ite(
                self.buddy.bdd_ithvar(mapping[old]), high, low)
            memo[key] = result
            return result

        return visit(function)

    def normal_var(self, key: tuple) -> int:
        if key not in self.normal:
            index = self.normal_base + len(self.normal)
            if index >= self.var_count:
                raise OverflowError("canonical BDD variable budget exhausted")
            self.normal[key] = index
        return self.normal[key]

    def to_aag(self, builder: AagBuilder, function, variable_map: dict[int, int],
               memo: dict[int, int] | None = None) -> int:
        # One memo may be shared by outputs using the same variable_map.  This
        # is important for rank families: their BDD DAGs have large common
        # tails which should remain one AIG DAG rather than be emitted once
        # per predicate.
        literal_map = {variable: 2 * (aig_input + 1)
                       for variable, aig_input in variable_map.items()}
        return self.to_aag_literals(builder, function, literal_map, memo)

    def to_aag_literals(self, builder: AagBuilder, function,
                        literal_map: dict[int, int],
                        memo: dict[int, int] | None = None) -> int:
        """Emit a BDD with variables mapped to arbitrary AIG literals."""
        if memo is None:
            memo = {}

        def visit(node):
            if node == self.buddy.bddfalse:
                return 0
            if node == self.buddy.bddtrue:
                return 1
            key = node.id()
            if key in memo:
                return memo[key]
            var = self.buddy.bdd_var(node)
            if var not in literal_map:
                raise KeyError(f"cannot emit unmapped BDD variable {var}")
            vlit = literal_map[var]
            high = visit(self.buddy.bdd_high(node))
            low = visit(self.buddy.bdd_low(node))
            # ite(v,h,l) = (v&h) | (!v&l)
            result = builder.lor(builder.land(vlit, high),
                                 builder.land(vlit ^ 1, low))
            memo[key] = result
            return result

        return visit(function)


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


def projection_templates(bdds: Bdds, instance: Instance, name: str, arity: int,
                         goal: GoalInfo | None = None) -> dict[tuple, object]:
    assert instance.cert is not None
    function = bdds.from_aag(instance.cert, instance.cert.output(name))
    all_vars = {item.index for item in instance.variables}
    templates: dict[tuple, object] = {}
    rebuilt = bdds.buddy.bddtrue
    for subset0 in itertools.combinations(range(instance.n), arity):
        subset = _ordered_subset(instance, subset0, goal)
        selected = frozenset(subset)
        keep = {item.index for item in instance.variables
                if not item.owners or item.owners <= selected}
        drop = all_vars - keep
        projected = (bdds.buddy.bdd_exist(function, bdds.cube(drop))
                     if drop else function)
        slots = {client: pos for pos, client in enumerate(subset)}
        mapping = {item.index: bdds.normal_var(_normal_key(item.key, slots))
                   for item in instance.variables if item.index in keep}
        normalized = bdds.relabel(projected, mapping)
        roles = tuple(instance.role_by_client[index] for index in subset)
        relation = tuple("goal" if goal and index == goal.owner else "other"
                         for index in subset)
        # The GR(1) fixed point enumerates justice records in source order.
        # Its exact rank predicates (and, on invalid multi-hot monitor states,
        # even W*) may retain the stable lowest-index anchor although clients
        # share one specification role.  Record that index-relative anchor
        # explicitly; it is not inferred from AIG topology.
        anchor = tuple(index == 0 for index in subset)
        group = (roles, relation, anchor)
        previous = templates.get(group)
        if previous is not None and previous != normalized:
            raise Decline("anti-unify", name, instance.n,
                          f"projections disagree within role class {group}")
        templates[group] = normalized

        # Rebuild in the seed's concrete variable space for the exact
        # separability check.  Each projection, not one representative per
        # role, is a conjunct.
        reverse = {bdds.normal_var(_normal_key(item.key, slots)): item.index
                   for item in instance.variables if item.index in keep}
        rebuilt &= bdds.relabel(normalized, reverse)
    if rebuilt != function:
        raise Decline("anti-unify", name, instance.n,
                      f"declared arity k={arity} does not reconstruct predicate")
    return templates


def _one_hot_validity(bdds: Bdds, instance: Instance):
    groups: dict[tuple, list[int]] = defaultdict(list)
    for item in instance.variables:
        if item.key[:2] == ("state", "monitor"):
            groups[item.key[:-1]].append(item.index)
        elif item.key[:2] == ("state", "bus"):
            groups[item.key[:-2] + (item.key[-1],)].append(item.index)
    result = bdds.buddy.bddtrue
    for variables in groups.values():
        atleast = bdds.buddy.bddfalse
        for variable in variables:
            atleast |= bdds.buddy.bdd_ithvar(variable)
        atmost = bdds.buddy.bddtrue
        for left, right in itertools.combinations(variables, 2):
            atmost &= (bdds.buddy.bdd_nithvar(left) |
                       bdds.buddy.bdd_nithvar(right))
        result &= atleast & atmost
    return result


def instantiate_templates(bdds: Bdds, target: Instance,
                          templates: dict[tuple, object], arity: int,
                          goal: GoalInfo | None = None):
    result = bdds.buddy.bddtrue
    for subset0 in itertools.combinations(range(target.n), arity):
        subset = _ordered_subset(target, subset0, goal)
        roles = tuple(target.role_by_client[index] for index in subset)
        relation = tuple("goal" if goal and index == goal.owner else "other"
                         for index in subset)
        anchor = tuple(index == 0 for index in subset)
        group = (roles, relation, anchor)
        if group not in templates:
            raise Decline("instantiate", goal.key if goal else "inv", target.n,
                          f"no template for role class {group}")
        slots = {client: pos for pos, client in enumerate(subset)}
        concrete = {_normal_key(item.key, slots): item.index
                    for item in target.variables
                    if not item.owners or item.owners <= frozenset(subset)}
        support = _support(bdds, templates[group])
        mapping = {}
        inverse_normal = {value: key for key, value in bdds.normal.items()}
        for variable in support:
            key = inverse_normal[variable]
            if key not in concrete:
                raise Decline("instantiate", goal.key if goal else "inv", target.n,
                              f"canonical variable {key!r} is absent at target")
            mapping[variable] = concrete[key]
        result &= bdds.relabel(templates[group], mapping)
    return result


def _support(bdds: Bdds, function) -> set[int]:
    node = bdds.buddy.bdd_support(function)
    result = set()
    while node != bdds.buddy.bddtrue and node != bdds.buddy.bddfalse:
        result.add(bdds.buddy.bdd_var(node))
        node = bdds.buddy.bdd_high(node)
    return result


def _merge_seed_templates(stage: str, predicate: str,
                          by_seed: list[tuple[int, dict[tuple, object]]]) -> dict[tuple, object]:
    result: dict[tuple, object] = {}
    for n, templates in by_seed:
        for key, function in templates.items():
            # Existential projections at a larger seed can strengthen the
            # representative on unreachable encodings while their complete
            # conjunction remains exact.  Stable-regime seeds are processed
            # in increasing n, so retain the most constrained observation;
            # every retained seed is independently reconstructed above and
            # the instantiated candidate remains untrusted until checked.
            result[key] = function
    return result


def _goal_match(goals: list[GoalInfo], key: tuple) -> GoalInfo:
    matches = [goal for goal in goals if goal.key == key]
    if len(matches) != 1:
        raise KeyError(f"goal class {key!r} has {len(matches)} matches")
    return matches[0]


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


def emit_candidate(bdds: Bdds, target: Instance, out: pathlib.Path,
                   predicates: dict[str, object], levels: list[int]) -> tuple[pathlib.Path, pathlib.Path]:
    cert_path = out / f"{target.family}_{target.n}.certificate.aag"
    policy_path = out / f"{target.family}_{target.n}.policy.aag"
    cert_inputs = [*target.game.latch_names, *target.game.input_names]
    cert_builder = AagBuilder(cert_inputs)
    cert_outputs = []
    identity = {i: i for i in range(len(cert_inputs))}
    cert_memo: dict[int, int] = {}
    structured_moves = (_structured_arbiter_certificate_moves(
        bdds, target, cert_builder, predicates, levels)
                        if target.family == "arbiter" else {})
    for name, function in predicates.items():
        if name in structured_moves:
            cert_outputs.append((name, structured_moves[name]))
        else:
            cert_outputs.append((name, bdds.to_aag(
                cert_builder, function, identity, cert_memo)))
    cert_path.write_text(cert_builder.render(
        cert_outputs, "index-aware GR(1) generalized certificate"), encoding="utf-8")
    cert_meta = _certificate_sidecar(target, cert_path, levels,
                                     len(cert_outputs), len(cert_builder.gates))
    pathlib.Path(str(cert_path) + ".json").write_text(
        json.dumps(cert_meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    _trace("certificate emitted")

    if target.family == "arbiter":
        nstate = len(target.game.latches)
        ngoals = len(target.goals)
        uncontrollable = [name for name in target.game.input_names
                          if not name.startswith("controllable_")]
        controls = [name for name in target.game.input_names
                    if name.startswith("controllable_")]
        policy_inputs = [*target.game.latch_names,
                         *(f"curr_{j}" for j in range(ngoals)),
                         *uncontrollable]
        policy_builder = AagBuilder(policy_inputs)
        literals = _structured_arbiter_policy_literals(
            bdds, target, policy_builder, predicates, levels)
        names = [*controls, *(f"curr_next_{j}" for j in range(ngoals))]
        policy_path.write_text(policy_builder.render(
            list(zip(names, literals, strict=True)),
            "canonical lowest-index structured Skolemization"),
            encoding="utf-8")
        policy_meta = _policy_sidecar(
            target, policy_path, len(policy_builder.gates))
        pathlib.Path(str(policy_path) + ".json").write_text(
            json.dumps(policy_meta, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        _trace("structured arbiter policy emitted")
        return cert_path, policy_path

    # Build the relation selected by the effective goal counter.
    nstate = len(target.game.latches)
    ngoals = len(target.goals)
    # Use a policy-only variable order with the counter before the game.  In
    # the certificate ABI state necessarily comes first, but using that order
    # for the goal mux expands every move relation before inspecting curr.
    curr_base = 512
    policy_game_base = 1024
    public_game_vars = nstate + len(target.game.inputs)
    if policy_game_base + public_game_vars >= bdds.next_base:
        raise OverflowError("policy BDD variable block exhausted")
    policy_game_map = {i: policy_game_base + i
                       for i in range(public_game_vars)}
    policy_moves = []
    policy_goals = []
    for j in range(ngoals):
        policy_moves.append(bdds.relabel(
            predicates[f"move_{j}"],
            {v: policy_game_map[v]
             for v in _support(bdds, predicates[f"move_{j}"])}))
        policy_goals.append(bdds.relabel(
            predicates[f"goal_{j}"],
            {v: policy_game_map[v]
             for v in _support(bdds, predicates[f"goal_{j}"])}))
        _trace(f"policy relation relabelled goal {j + 1}/{ngoals}")
    any_curr = bdds.buddy.bddfalse
    curr = []
    for j in range(ngoals):
        bit = bdds.buddy.bdd_ithvar(curr_base + j)
        curr.append(bit)
        any_curr |= bit
    effective = [curr[0] | bdds.buddy.bdd_not(any_curr), *curr[1:]]
    relation = bdds.buddy.bddfalse
    for j in range(ngoals):
        relation |= effective[j] & policy_moves[j]
    _trace("policy goal relation muxed")

    controls = [policy_game_base + nstate + p
                for p, name in enumerate(target.game.input_names)
                if name.startswith("controllable_")]
    functions = []
    chosen = bdds.buddy.bddtrue
    control_cube = bdds.cube(controls)
    for pos, control in enumerate(controls):
        positive = relation & chosen & bdds.buddy.bdd_ithvar(control)
        function = bdds.buddy.bdd_exist(positive, control_cube)
        functions.append(function)
        bit = bdds.buddy.bdd_ithvar(control)
        chosen &= ((bit & function) |
                   (bdds.buddy.bdd_not(bit) & bdds.buddy.bdd_not(function)))
        _trace(f"policy control {pos + 1}/{len(controls)} Skolemized")

    # The counter protocol is independent of the Skolem choices.
    next_curr = []
    for j in range(ngoals):
        advance = effective[j] & policy_goals[j]
        prev = (j + ngoals - 1) % ngoals
        prev_advance = effective[prev] & policy_goals[prev]
        next_curr.append((effective[j] & bdds.buddy.bdd_not(advance)) |
                         prev_advance)

    uncontrollable = [(p, name) for p, name in enumerate(target.game.input_names)
                      if not name.startswith("controllable_")]
    policy_inputs = [*target.game.latch_names,
                     *(f"curr_{j}" for j in range(ngoals)),
                     *(name for _p, name in uncontrollable)]
    remap = {policy_game_base + i: i for i in range(nstate)}
    remap.update({curr_base + j: nstate + j for j in range(ngoals)})
    remap.update({policy_game_base + nstate + p: nstate + ngoals + j
                  for j, (p, _name) in enumerate(uncontrollable)})
    policy_builder = AagBuilder(policy_inputs)
    policy_outputs = []
    control_names = [name for name in target.game.input_names
                     if name.startswith("controllable_")]
    for name, function in zip(control_names, functions, strict=True):
        policy_outputs.append((name, bdds.to_aag(
            policy_builder, function, remap)))
    for j, function in enumerate(next_curr):
        policy_outputs.append((f"curr_next_{j}", bdds.to_aag(
            policy_builder, function, remap)))
    policy_path.write_text(policy_builder.render(
        policy_outputs, "canonical lowest-index Skolemization"), encoding="utf-8")
    _trace("policy emitted")
    policy_meta = _policy_sidecar(target, policy_path, len(policy_builder.gates))
    pathlib.Path(str(policy_path) + ".json").write_text(
        json.dumps(policy_meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return cert_path, policy_path


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


def _exactly_one(bdds: Bdds, variables: list[int]):
    """Linear-size exact-one BDD, avoiding a quadratic pairwise formula."""
    none = bdds.buddy.bddtrue
    one = bdds.buddy.bddfalse | bdds.buddy.bddfalse
    for variable in variables:
        positive = bdds.buddy.bdd_ithvar(variable)
        negative = bdds.buddy.bdd_not(positive)
        one = (one & negative) | (none & positive)
        none &= negative
    return one


def _collector_candidate(
        bdds: Bdds, seeds: list[Instance], target: Instance,
        out: pathlib.Path, stage: dict[str, float]
) -> tuple[CandidateSchema, Instance, pathlib.Path, pathlib.Path, dict]:
    """Instantiate the collector's bus-wide W schema on semantic states.

    The generated monitor uses one-hot state encodings whose width grows with
    n.  Its stable object is therefore the W automaton semantics, not a bit
    ordinal.  The canonical policy pulses ``allFinished`` exactly when every
    local W monitor is in its reset/accepting state.  Universal predecessor
    layers over the finite semantic product provide an independently checkable
    rank certificate for that policy.
    """
    predicate = "W(AllSeen(finished),allFinished)"
    started = time.monotonic()
    if predicate not in target.bus_schemas:
        raise Decline("bus-wide schemas", predicate, target.n,
                      f"target schemas are {target.bus_schemas!r}")
    if any(predicate not in seed.bus_schemas for seed in seeds):
        failed = next(seed.n for seed in seeds if predicate not in seed.bus_schemas)
        raise Decline("bus-wide schemas", predicate, failed,
                      "semantic schema differs across seeds")
    stage["bus_schemas"] = time.monotonic() - started

    started = time.monotonic()
    seed_depths = [tuple(seed.levels(goal.goal) for goal in seed.goals)
                   for seed in seeds]
    if len(set(seed_depths)) != 1:
        failed = next(seed.n for seed, depths in zip(seeds, seed_depths, strict=True)
                      if depths != seed_depths[0])
        raise Decline("ranks", predicate, failed,
                      f"rank depth differs across seeds: {seed_depths}")
    stage["anti_unify"] = time.monotonic() - started

    started = time.monotonic()
    monitors = {record["monitor"]: record for record in target.prov["monitors"]}
    bus_records = [record for record in monitors.values()
                   if record["arity_kind"] == "bus_wide"]
    local_records = sorted(
        (record for record in monitors.values()
         if record["arity_kind"] == "local"),
        key=lambda record: tuple(record["index_tuple"]))
    if len(bus_records) != 1 or len(local_records) != target.n:
        raise Decline("canonicalize", predicate, target.n,
                      f"expected one bus and {target.n} local monitors")
    bus = bus_records[0]
    if any(record["state_count"] != 3 for record in local_records):
        raise Decline("canonicalize", "local W monitor", target.n,
                      "expected the measured three-state local ABI")

    def latch_index(record: dict, bit: int) -> int:
        name = f"monitor_{record['monitor']}_state_{bit}"
        try:
            return target.game.latch_names.index(name)
        except ValueError as exc:
            raise Decline("canonicalize", name, target.n,
                          "monitor state is absent from game ABI") from exc

    bus_latches = [latch_index(bus, bit) for bit in range(bus["state_count"])]
    local_latches = [[latch_index(record, bit) for bit in range(3)]
                     for record in local_records]
    input_positions = {name: pos for pos, name in enumerate(target.game.input_names)}
    try:
        finished_positions = [input_positions[f"finished_{i}"]
                              for i in range(target.n)]
        control_position = input_positions["controllable_allFinished"]
    except KeyError as exc:
        raise Decline("canonicalize", str(exc), target.n,
                      "collector letter is absent from game ABI") from exc
    stage["canonicalize"] = time.monotonic() - started

    started = time.monotonic()
    valuation_count = 1 << target.n
    valuation_mask = (1 << valuation_count) - 1
    env_vectors = [sum(1 << valuation for valuation in range(valuation_count)
                       if valuation & (1 << client))
                   for client in range(target.n)]
    semantic_count = bus["state_count"] * (1 << target.n)
    successors: list[frozenset[int]] = []
    goal_states = [set() for _goal in target.goals]
    latch_values = [False] * len(target.game.latches)
    for bus_state in range(bus["state_count"]):
        for local_mask in range(1 << target.n):
            state_id = bus_state * (1 << target.n) + local_mask
            for index in bus_latches:
                latch_values[index] = False
            latch_values[bus_latches[bus_state]] = True
            for client, group in enumerate(local_latches):
                for index in group:
                    latch_values[index] = False
                local_state = 1 if local_mask & (1 << client) else 0
                latch_values[group[local_state]] = True
            inputs = [0] * len(target.game.inputs)
            for client, position in enumerate(finished_positions):
                inputs[position] = env_vectors[client]
            inputs[control_position] = valuation_mask if local_mask == 0 else 0
            next_vectors, justice_vectors = _aag_vector_evaluate(
                target.game, latch_values, inputs, valuation_mask)
            for goal, vector in enumerate(justice_vectors):
                if vector not in (0, valuation_mask):
                    raise Decline("ranks", f"goal_{goal}", target.n,
                                  "game justice unexpectedly depends on letters")
                if vector:
                    goal_states[goal].add(state_id)
            next_ids = set()
            for valuation in range(valuation_count):
                selected_bus = [bit for bit, index in enumerate(bus_latches)
                                if next_vectors[index] & (1 << valuation)]
                if len(selected_bus) != 1:
                    raise Decline("ranks", predicate, target.n,
                                  "bus successor is not one-hot")
                next_local = 0
                for client, group in enumerate(local_latches):
                    selected = [bit for bit, index in enumerate(group)
                                if next_vectors[index] & (1 << valuation)]
                    if len(selected) != 1 or selected[0] == 2:
                        raise Decline("ranks", "local W invariant", target.n,
                                      "canonical policy leaves the safe local states")
                    if selected[0] == 1:
                        next_local |= 1 << client
                next_ids.add(selected_bus[0] * (1 << target.n) + next_local)
            successors.append(frozenset(next_ids))
    if len(successors) != semantic_count:
        raise AssertionError("collector semantic-state enumeration is incomplete")

    # Greatest generalized-Buechi region under the fixed policy.  Environment
    # choices are the outgoing edges, hence every predecessor test is
    # universal.  This also removes the rejecting one-hot states of the bus
    # monitor without depending on their generated ordinal numbers.
    winning = set(range(semantic_count))
    while True:
        previous_winning = set(winning)
        for accepting0 in goal_states:
            accepting = accepting0 & winning
            reached = set(accepting)
            while True:
                expanded = reached | {
                    state for state in winning
                    if successors[state] <= reached
                }
                if expanded == reached:
                    break
                reached = expanded
            winning &= reached
        if winning == previous_winning:
            break
    reset_bus = next((bit for bit, index in enumerate(bus_latches)
                      if target.game.latches[index][2] != 0), None)
    if reset_bus is None:
        # ASCII AIGER's omitted/zero reset means state bit zero; the monitor
        # encoder has exactly one explicit true reset bit.
        reset_bus = 0
    reset_state = reset_bus * (1 << target.n)
    if reset_state not in winning:
        raise Decline("ranks", predicate, target.n,
                      "canonical policy's generalized-Buechi region excludes reset")
    reachable = {reset_state}
    while True:
        expanded = reachable | set().union(
            *(successors[state] for state in reachable))
        if expanded == reachable:
            break
        reachable = expanded
    if not reachable <= winning:
        raise Decline("ranks", predicate, target.n,
                      "canonical policy reaches a state outside its winning region")
    # A certificate needs an inductive region containing reset, not the
    # maximal winning region.  The reachable closure is both stronger and far
    # cheaper for the independent checker to compose with 2^n bus latches.
    winning = reachable

    rank_sets: list[list[set[int]]] = []
    for goal, accepting0 in enumerate(goal_states):
        accepting = accepting0 & winning
        covered: set[int] = set()
        levels = []
        while covered != winning:
            allowed = accepting | covered
            current = set(accepting)
            current.update(state for state in winning
                           if successors[state] <= allowed)
            if current == covered:
                raise Decline("ranks", f"goal_{goal}", target.n,
                              f"canonical policy rank stalls after {len(levels)} levels")
            levels.append(current)
            covered = current
            if len(levels) > semantic_count:
                raise AssertionError("collector rank iteration did not converge")
        rank_sets.append(levels)
    stage["ranks"] = time.monotonic() - started

    started = time.monotonic()
    def state_set(members: set[int]):
        # Positive one-hot literals are sufficient because every obligation
        # intersects the invariant; keeping the formula positive also yields
        # compact, monotone BDDs for the 2^n-state bus monitor.
        result = bdds.buddy.bddfalse | bdds.buddy.bddfalse
        local_width = 1 << target.n
        by_bus: dict[int, set[int]] = defaultdict(set)
        for state_id in members:
            bus_state, local_mask = divmod(state_id, local_width)
            by_bus[bus_state].add(local_mask)
        all_local_masks = set(range(local_width))
        for bus_state, local_masks in sorted(by_bus.items()):
            local_predicate = bdds.buddy.bddfalse | bdds.buddy.bddfalse
            if local_masks == all_local_masks:
                local_predicate = bdds.buddy.bddtrue
            else:
                for local_mask in sorted(local_masks):
                    cube = bdds.buddy.bddtrue
                    for client, group in enumerate(local_latches):
                        cube &= bdds.buddy.bdd_ithvar(
                            group[1 if local_mask & (1 << client) else 0])
                    local_predicate |= cube
            result |= (bdds.buddy.bdd_ithvar(bus_latches[bus_state]) &
                       local_predicate)
        return result

    winning_bus_states = sorted({state // (1 << target.n) for state in winning})
    inv = _exactly_one(
        bdds, [bus_latches[state] for state in winning_bus_states])
    for state, variable in enumerate(bus_latches):
        if state not in winning_bus_states:
            inv &= bdds.buddy.bdd_nithvar(variable)
    for group in local_latches:
        inv &= _exactly_one(bdds, group)
        inv &= bdds.buddy.bdd_nithvar(group[2])
    # If reachability couples a bus state to only some local masks, retain the
    # coupling.  (The measured collector instances have the simpler complete
    # product, but this assertion keeps the emitted invariant honest.)
    inv &= state_set(winning)
    policy = bdds.buddy.bddtrue
    for group in local_latches:
        policy &= bdds.buddy.bdd_ithvar(group[0])

    _next, _bad, exact_goals, _fairness = bdds.game_functions(target.game)
    predicates: dict[str, object] = {"inv": inv}
    levels = [len(items) for items in rank_sets]
    for j, goal in enumerate(exact_goals):
        predicates[f"goal_{j}"] = goal
    for j, rows in enumerate(rank_sets):
        previous_rank = bdds.buddy.bddfalse | bdds.buddy.bddfalse
        for k, members in enumerate(rows):
            raw_rank = state_set(members)
            # Values outside inv are don't-cares for every proof obligation.
            # BuDDy's restrict-aware simplifier finds a compact representative
            # over the one-hot semantic region; unioning the previous level
            # preserves the checker's global monotonicity requirement.
            rank = previous_rank | bdds.buddy.bdd_simplify(raw_rank, inv)
            predicates[f"y_{j}_{k}"] = rank
            predicates[f"x_{j}_{k}_0"] = rank
            previous_rank = rank
    control = bdds.buddy.bdd_ithvar(
        len(target.game.latches) + control_position)
    move = inv & ((control & policy) |
                  (bdds.buddy.bdd_not(control) & bdds.buddy.bdd_not(policy)))
    for j in range(len(target.goals)):
        predicates[f"move_{j}"] = move
    if os.environ.get("GENERALIZE_GR1_TRACE"):
        _trace("collector predicate nodes " + ", ".join(
            f"{name}={bdds.buddy.bdd_nodecount(function)}"
            for name, function in predicates.items()))

    ordered = {"inv": predicates["inv"]}
    for j in range(len(target.goals)):
        ordered[f"goal_{j}"] = predicates[f"goal_{j}"]
    for j, depth in enumerate(levels):
        for k in range(depth):
            ordered[f"y_{j}_{k}"] = predicates[f"y_{j}_{k}"]
    for j, depth in enumerate(levels):
        for k in range(depth):
            ordered[f"x_{j}_{k}_0"] = predicates[f"x_{j}_{k}_0"]
    for j in range(len(target.goals)):
        ordered[f"move_{j}"] = predicates[f"move_{j}"]
    cert, policy_path = emit_candidate(bdds, target, out, ordered, levels)
    stage["instantiate"] = time.monotonic() - started
    candidate = CandidateSchema(
        target.family, FAMILIES[target.family].arity,
        tuple(seed.n for seed in seeds),
        tuple(f"role_{index}" for index in range(
            measured_role_class_count(target.family) or 0)),
        target.bus_schemas,
        (("bus_semantic_states", bus["state_count"]),
         ("move_reconstructed", len(target.goals)),
         ("rank", sum(levels)),
         ("winning_bus_states", len(winning_bus_states)),
         ("winning_semantic_states", len(winning))))
    evidence = {
        "format": "acacia-param-lift-gr1-evidence-v1",
        "family": target.family, "target": target.n,
        "seeds": [seed.n for seed in seeds], "arity": candidate.arity,
        "role_classes": list(candidate.role_classes),
        "bus_schemas": list(candidate.bus_schemas),
        "template_counts": dict(candidate.template_counts),
        "rank_depths": levels,
        "stages": ["seed", "canonicalize", "anti-unify",
                   "bus-wide schemas", "ranks", "instantiate", "CEGIS"],
        "stage_evidence": {
            "seed": {"sizes": [seed.n for seed in seeds],
                     "exports": ["policy", "certificate", "provenance"]},
            "canonicalize": {"variable_abi":
                             "(template,index_tuple,state_bit)"},
            "anti-unify": {"arity": candidate.arity,
                           "role_classes": list(candidate.role_classes)},
            "bus-wide schemas": {"matched": list(candidate.bus_schemas)},
            "ranks": {"depths": levels,
                      "construction": "universal semantic attractor"},
            "instantiate": {"certificate": cert.name,
                            "policy": policy_path.name},
            "CEGIS": {"round_cap": MAX_CEGIS_ROUNDS},
        },
        "canonical_variable": "(template,index_tuple,state_bit)",
        "bus_instantiation": (
            "semantic one-hot W automaton under the canonical allFinished rule"),
        "y_reconstruction": "union_i x_j_k_i",
        "skolem_rule": "allFinished iff every local W monitor is in state 0",
    }
    (out / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return candidate, target, cert, policy_path, {"times": stage,
                                                   "evidence": evidence}


def generalize_once(family: str, target_n: int, seed_ns: tuple[int, ...],
                    out: pathlib.Path, limits: ProposerLimits) -> tuple[CandidateSchema, Instance,
                                                                       pathlib.Path, pathlib.Path,
                                                                       dict]:
    out.mkdir(parents=True, exist_ok=True)
    stage = {}
    started = time.monotonic()
    seeds = [solve_seed(family, n, out, limits.checker_timeout_s) for n in seed_ns]
    stage["seed"] = time.monotonic() - started
    target_game, target_prov = build_game(family, target_n, out,
                                          limits.checker_timeout_s)
    target = Instance.load(family, target_n, target_game, target_prov)
    role_counts = [_provenance_role_class_count(seed) for seed in seeds]
    role_counts.append(_provenance_role_class_count(target))
    measured_roles = measured_role_class_count(family)
    if len(set(role_counts)) != 1 or measured_roles is None or (
            role_counts[0] != measured_roles):
        raise Decline("anti-unify", "role classes", target_n,
                      "role-class count is not constant across seeds/target "
                      f"and equal to the measurement: observed={role_counts}, "
                      f"measured={measured_roles}")

    if family == "collector_v1":
        bdds = Bdds()
        try:
            return _collector_candidate(bdds, seeds, target, out, stage)
        finally:
            bdds.close()

    started = time.monotonic()
    bdds = Bdds()
    try:
        arity = FAMILIES[family].arity
        stage["canonicalize"] = time.monotonic() - started

        started = time.monotonic()
        # Stable bus schemas must have the same state ABI.  Variable-count
        # changes are semantic, not a license to align by ordinal.
        seed_bus_shapes = [{item.key for item in seed.variables
                            if item.key[:2] == ("state", "bus")}
                           for seed in seeds]
        target_bus_shapes = {item.key for item in target.variables
                             if item.key[:2] == ("state", "bus")}
        if any(shape != seed_bus_shapes[0] for shape in seed_bus_shapes[1:]) or (
                seed_bus_shapes and target_bus_shapes != seed_bus_shapes[0]):
            raise Decline("bus-wide schemas", "state ABI", target_n,
                          "matched bus schema has a size-dependent monitor-state encoding")
        stage["bus_schemas"] = time.monotonic() - started

        started = time.monotonic()
        inv_templates = _merge_seed_templates(
            "anti-unify", "inv",
            [(seed.n, projection_templates(bdds, seed, "inv", arity))
             for seed in seeds])
        inv = instantiate_templates(bdds, target, inv_templates, arity)
        stage["anti_unify"] = time.monotonic() - started

        # Each target goal is matched by its monitor template.  Depth and all
        # X/move templates must agree across seeds of the same goal class.
        started = time.monotonic()
        predicates: dict[str, object] = {"inv": inv}
        levels: list[int] = []
        template_tally = Counter({"inv": len(inv_templates)})
        next_state, game_bad, exact_goals, exact_fairness = bdds.game_functions(
            target.game)
        nstate = len(target.game.latches)
        structured_moves = family == "arbiter"
        if not structured_moves:
            # Move construction needs T[s:=next].  Use an interleaved internal
            # order (s0,s0',s1,s1',...,letters) so substitution does not
            # create a far-away-auxiliary intermediate.
            internal_map = {i: 2 * i for i in range(nstate)}
            internal_map.update({nstate + p: 2 * nstate + p
                                 for p in range(len(target.game.inputs))})
            public_map = {value: key for key, value in internal_map.items()}

            def internal(function):
                return bdds.relabel(
                    function, {v: internal_map[v]
                               for v in _support(bdds, function)})

            internal_next = [internal(function) for function in next_state]
            internal_inv = internal(inv)
            internal_not_bad = bdds.buddy.bdd_not(internal(game_bad))
            internal_state = [2 * i for i in range(nstate)]
            internal_temp = [2 * i + 1 for i in range(nstate)]
            internal_w_safe = internal_not_bad & bdds.substitute_variables(
                internal_inv, internal_state, internal_next, internal_temp)
            _trace("target invariant transition composed")
        for target_goal in target.goals:
            seed_goals = []
            for seed in seeds:
                matches = [goal for goal in seed.goals if goal.key == target_goal.key]
                if target_goal.owner is not None:
                    # Use the corresponding role, not the same concrete index.
                    desired_role = target.role_by_client[target_goal.owner]
                    matches = [goal for goal in matches if goal.owner is not None and
                               seed.role_by_client[goal.owner] == desired_role]
                    anchored = target_goal.owner == 0
                    same_anchor = [goal for goal in matches
                                   if (goal.owner == 0) == anchored]
                    if same_anchor:
                        matches = same_anchor
                if not matches:
                    raise Decline("ranks", repr(target_goal.key), seed.n,
                                  "goal role class absent from seed")
                seed_goals.append(matches[0])
            depths = [seed.levels(goal.goal) for seed, goal in zip(seeds, seed_goals)]
            if len(set(depths)) != 1:
                raise Decline("ranks", repr(target_goal.key),
                              seeds[depths.index(max(depths))].n,
                              f"rank depth differs across seeds: {depths}")
            # Goals are specification predicates, not learned solution
            # predicates.  Copy the target justice literal exactly.
            j = target_goal.goal
            goal_pred = exact_goals[j]
            predicates[f"goal_{j}"] = goal_pred

            # Anti-unify every retained ν-level.  The corresponding μ-level
            # is definitionally the union across fairness assumptions and is
            # deliberately reconstructed rather than learned independently.
            depth = depths[0]
            levels.append(depth)
            for level in range(depth):
                row = []
                for fair in range(max(1, target.fairness)):
                    by_seed = []
                    for seed, seed_goal in zip(seeds, seed_goals, strict=True):
                        name = f"x_{seed_goal.goal}_{level}_{fair}"
                        by_seed.append((seed.n, projection_templates(
                            bdds, seed, name, arity, seed_goal)))
                    templates = _merge_seed_templates(
                        "ranks", f"x_{j}_{level}_{fair}", by_seed)
                    x = instantiate_templates(
                        bdds, target, templates, arity, target_goal)
                    predicates[f"x_{j}_{level}_{fair}"] = x
                    row.append(x)
                    template_tally["rank"] += len(templates)
                union = bdds.buddy.bddfalse
                for x in row:
                    union |= x
                predicates[f"y_{j}_{level}"] = union

            # Reconstruct the target move relation from the generalized
            # invariant/ranks.  This is the same pre-Skolem relation exported
            # by M2 and avoids transferring any seed tie-breaking.
            if structured_moves:
                # The exact relation is composed as an AIG in emit_candidate;
                # retaining a placeholder here preserves output ordering.
                predicates[f"move_{j}"] = bdds.buddy.bddfalse
            else:
                at_goal = internal_inv & internal(goal_pred)
                move = at_goal & internal_w_safe
                covered = at_goal
                for level in range(depth):
                    strict = (at_goal if level == 0 else at_goal |
                              internal(predicates[f"y_{j}_{level - 1}"]))
                    for fair in range(max(1, target.fairness)):
                        x = internal(predicates[f"x_{j}_{level}_{fair}"])
                        layer = x & bdds.buddy.bdd_not(covered)
                        fair_pred = (exact_fairness[fair] if exact_fairness
                                     else bdds.buddy.bddtrue)
                        rank_target = strict | (
                            bdds.buddy.bdd_not(internal(fair_pred)) & x)
                        step = internal_not_bad & bdds.substitute_variables(
                            rank_target, internal_state, internal_next,
                            internal_temp)
                        move |= layer & step
                        covered |= x
                predicates[f"move_{j}"] = bdds.relabel(
                    move, {v: public_map[v] for v in _support(bdds, move)})
            template_tally["move_reconstructed"] += 1
            _trace(f"target goal {j + 1}/{len(target.goals)} ranks and move instantiated")
        stage["ranks"] = time.monotonic() - started

        # Checker requires a stable output order.
        ordered = {"inv": predicates["inv"]}
        for j in range(len(target.goals)):
            ordered[f"goal_{j}"] = predicates[f"goal_{j}"]
        for j, depth in enumerate(levels):
            for k in range(depth):
                ordered[f"y_{j}_{k}"] = predicates[f"y_{j}_{k}"]
        for j, depth in enumerate(levels):
            for k in range(depth):
                for fair in range(max(1, target.fairness)):
                    ordered[f"x_{j}_{k}_{fair}"] = predicates[f"x_{j}_{k}_{fair}"]
        for j in range(len(target.goals)):
            ordered[f"move_{j}"] = predicates[f"move_{j}"]
        started = time.monotonic()
        cert, policy = emit_candidate(bdds, target, out, ordered, levels)
        stage["instantiate"] = time.monotonic() - started
        candidate = CandidateSchema(
            family, arity, seed_ns,
            tuple(f"role_{index}" for index in range(measured_roles)),
            target.bus_schemas, tuple(sorted(template_tally.items())))
        evidence = {"format": "acacia-param-lift-gr1-evidence-v1",
                    "family": family, "target": target_n,
                    "seeds": list(seed_ns), "arity": arity,
                    "role_classes": list(candidate.role_classes),
                    "bus_schemas": list(candidate.bus_schemas),
                    "template_counts": dict(candidate.template_counts),
                    "rank_depths": levels,
                    "stages": ["seed", "canonicalize", "anti-unify",
                               "bus-wide schemas", "ranks", "instantiate",
                               "CEGIS"],
                    "stage_evidence": {
                        "seed": {"sizes": list(seed_ns),
                                 "exports": ["policy", "certificate",
                                             "provenance"]},
                        "canonicalize": {"variable_abi":
                                         "(template,index_tuple,state_bit)"},
                        "anti-unify": {"arity": arity,
                                       "template_counts":
                                           dict(candidate.template_counts),
                                       "role_classes":
                                           list(candidate.role_classes)},
                        "bus-wide schemas": {
                            "matched": list(candidate.bus_schemas)},
                        "ranks": {"depths": levels,
                                  "y": "union_i x_j_k_i"},
                        "instantiate": {"certificate": cert.name,
                                        "policy": policy.name},
                        "CEGIS": {"round_cap": MAX_CEGIS_ROUNDS},
                    },
                    "canonical_variable": "(template,index_tuple,state_bit)",
                    "y_reconstruction": "union_i x_j_k_i",
                    "skolem_rule": "game controllable order; true/lowest index first"}
        (out / "evidence.json").write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return candidate, target, cert, policy, {"times": stage, "evidence": evidence}
    finally:
        bdds.close()


def check_candidate(target: Instance, cert: pathlib.Path, policy: pathlib.Path,
                    method: str, timeout: float, label: str) -> dict:
    json_out = cert.parent / f"check-{label}.json"
    command = [str(CHECKER), "--method", method, "--timeout", str(timeout),
               "--json-out", str(json_out), "--certificate", str(cert),
               "--certificate-json", str(cert) + ".json", str(target.game_path),
               str(policy)]
    started = time.monotonic()
    proc = _run(command, timeout + 10)
    elapsed = time.monotonic() - started
    payload = None
    if json_out.exists():
        try:
            payload = json.loads(json_out.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            payload = None
    return {"returncode": proc.returncode,
            "verdict": VERDICTS.get(proc.returncode, "UNKNOWN"),
            "elapsed_s": elapsed, "stdout": proc.stdout, "stderr": proc.stderr,
            "json": payload, "command": command}


def _next_small(target: int, seeds: tuple[int, ...], stable: int) -> int:
    first = max(stable, max(seeds, default=stable - 1) + 1)
    unused = [n for n in range(first, target) if n not in seeds]
    return unused[0] if unused else target


def _write_result(row: dict[str, object]) -> None:
    columns = ["family", "target", "seeds", "arity", "role_classes",
               "stages_passed", "cegis_rounds", "verdict",
               "seed_s", "canonicalize_s", "anti_unify_s", "bus_schemas_s",
               "ranks_s", "instantiate_s", "cegis_s", "reason"]
    rows = []
    if RESULTS.exists():
        rows = _read_tsv(RESULTS)
    key = (str(row["family"]), str(row["target"]))
    rows = [old for old in rows if (old["family"], old["target"]) != key]
    rows.append({column: row.get(column, "") for column in columns})
    rows.sort(key=lambda item: (item["family"], int(item["target"])))
    with RESULTS.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def run(family: str, target: int, seeds: tuple[int, ...], out: pathlib.Path,
        limits: ProposerLimits, check_method: str = "auto") -> dict:
    started_all = time.monotonic()
    stable = stable_from(family)
    bad = [n for n in seeds if n < stable]
    if bad:
        raise Decline("seed", "stable_from", bad[0],
                      f"seed is below measured stable_from={stable}")
    if family == "round_robin_arbiter_unreal2":
        raise Decline("scope", "arity measurement", None,
                      "family is not one of the five fixed-arity M4 families")
    if family in OUT_OF_SCOPE:
        raise Decline("scope", "arity measurement", None,
                      "measured min_k = n; invariant strengthening search is required")
    if family not in FAMILIES:
        raise Decline("scope", "arity measurement", None,
                      "family has no fixed-arity M4 measurement")
    measured = measured_arity(family)
    if measured != FAMILIES[family].arity:
        raise Decline("scope", "arity measurement", None,
                      f"expected k={FAMILIES[family].arity}, measured {measured}")
    if not seeds:
        raise Decline("seed", "seed set", None, "at least one stable seed is required")
    if target <= max(seeds):
        raise Decline("seed", "target", target,
                      "target must be strictly larger than every seed")

    current = tuple(sorted(set(seeds)))
    rounds = 0
    cegis_started = time.monotonic()
    last = None
    while True:
        candidate, target_instance, cert, policy, detail = generalize_once(
            family, target, current, out, limits)
        probe = _next_small(target, current, stable)
        checks = []
        # A target-size checker is meaningful only for the target artefacts;
        # the small probe is materialized as a full CEGIS seed if needed.
        if probe < target and probe not in current:
            try:
                probe_dir = out / f"probe-{probe}"
                _candidate2, probe_instance, pcert, ppolicy, _detail2 = generalize_once(
                    family, probe, tuple(n for n in current if n < probe) or (current[0],),
                    probe_dir, limits)
                check = check_candidate(probe_instance, pcert, ppolicy,
                                        check_method, limits.checker_timeout_s,
                                        f"next-{probe}")
                checks.append((probe, check))
            except Decline as exc:
                check = {"returncode": 6, "verdict": "UNKNOWN",
                         "elapsed_s": 0.0, "stdout": "", "stderr": str(exc),
                         "json": None}
                checks.append((probe, check))
        target_check = check_candidate(target_instance, cert, policy,
                                       check_method, limits.checker_timeout_s,
                                       f"target-{target}")
        checks.append((target, target_check))
        last = (candidate, target_instance, cert, policy, detail, checks)
        failing = next(((n, check) for n, check in checks
                        if check["returncode"] != 0), None)
        if failing is None:
            break
        n, check = failing
        if check["returncode"] != 6 or rounds >= limits.max_cegis_rounds:
            break
        # The structured counterexample is evidence for the failed proof.  A
        # failed n is the next seed; never infer a losing controller from it.
        if n == target or n in current:
            break
        current = tuple(sorted((*current, n)))
        rounds += 1
    assert last is not None
    candidate, target_instance, cert, policy, detail, checks = last
    final_check = checks[-1][1]
    verdict = "VERIFIED" if final_check["returncode"] == 0 else (
        "INVALID" if final_check["returncode"] in (4, 5) else "UNKNOWN")
    times = detail["times"]
    times["cegis"] = time.monotonic() - cegis_started
    evidence = detail["evidence"]
    evidence["stage_evidence"]["CEGIS"].update({
        "rounds": rounds,
        "checks": [
            {"n": n, "exit_code": check["returncode"],
             "verdict": check["verdict"],
             "counterexample": bool(
                 isinstance(check.get("json"), dict) and
                 check["json"].get("counterexample"))}
            for n, check in checks
        ],
        "final_verdict": verdict,
    })
    (out / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    result = {"family": family, "target": target, "seeds": current,
              "arity": candidate.arity, "role_classes": candidate.role_classes,
              "stages_passed": detail["evidence"]["stages"],
              "cegis_rounds": rounds, "verdict": verdict,
              "certificate": cert, "policy": policy, "checks": checks,
              "times": times, "wall_s": time.monotonic() - started_all,
              "reason": "" if verdict == "VERIFIED" else
              ((final_check["stderr"] or final_check["stdout"]).strip()[-500:])}
    _write_result({"family": family, "target": target,
                   "seeds": ",".join(map(str, current)), "arity": candidate.arity,
                   "role_classes": ",".join(candidate.role_classes),
                   "stages_passed": ",".join(result["stages_passed"]),
                   "cegis_rounds": rounds, "verdict": verdict,
                   "seed_s": f"{times.get('seed', 0):.6f}",
                   "canonicalize_s": f"{times.get('canonicalize', 0):.6f}",
                   "anti_unify_s": f"{times.get('anti_unify', 0):.6f}",
                   "bus_schemas_s": f"{times.get('bus_schemas', 0):.6f}",
                   "ranks_s": f"{times.get('ranks', 0):.6f}",
                   "instantiate_s": f"{times.get('instantiate', 0):.6f}",
                   "cegis_s": f"{times.get('cegis', 0):.6f}",
                   "reason": result["reason"]})
    return result


def _decline_result(family: str, target: int, seeds: tuple[int, ...],
                    decline: Decline) -> dict:
    result = {"family": family, "target": target, "seeds": seeds,
              "arity": FAMILIES[family].arity if family in FAMILIES else "",
              "role_classes": (), "stages_passed": (), "cegis_rounds": 0,
              "verdict": "UNKNOWN", "reason": str(decline)}
    _write_result({"family": family, "target": target,
                   "seeds": ",".join(map(str, seeds)),
                   "arity": result["arity"], "role_classes": "",
                   "stages_passed": "", "cegis_rounds": 0,
                   "verdict": "UNKNOWN", "reason": str(decline)})
    return result


def main(argv: list[str] | None = None) -> int:
    # Spot and BuDDy are native CPython 3.13 modules in the research image.
    # Keep the driver runnable through the system's moving ``python3`` alias.
    if (sys.version_info[:2] != (3, 13) and
            pathlib.Path(sys.executable).resolve() != BINDINGS_PYTHON.resolve()):
        os.execv(str(BINDINGS_PYTHON), [str(BINDINGS_PYTHON), __file__,
                                       *(argv if argv is not None else sys.argv[1:])])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", required=True)
    parser.add_argument("--target", required=True, type=int)
    parser.add_argument("--seeds", help="comma-separated stable seed sizes")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--out", type=pathlib.Path)
    parser.add_argument("--check-method", choices=("auto", "certificate", "both"),
                        default="auto", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    default = FAMILIES.get(args.family)
    seeds = (tuple(int(item) for item in args.seeds.split(",") if item)
             if args.seeds is not None else (default.default_seeds if default else ()))
    out = (args.out or ROOT / "build_scratch" / "param-lift-m4" /
           f"{args.family}-n{args.target}").resolve()
    limits = ProposerLimits(checker_timeout_s=args.timeout)
    try:
        result = run(args.family, args.target, seeds, out, limits,
                     args.check_method)
    except Decline as exc:
        result = _decline_result(args.family, args.target, seeds, exc)
        out.mkdir(parents=True, exist_ok=True)
        (out / "evidence.json").write_text(json.dumps({
            "format": "acacia-param-lift-gr1-evidence-v1",
            "family": args.family, "target": args.target,
            "seeds": list(seeds), "verdict": "UNKNOWN",
            "decline": {"stage": exc.stage, "predicate": exc.predicate,
                        "n": exc.n, "reason": exc.reason},
        }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"{result['verdict']} family={args.family} target={args.target} "
          f"seeds={','.join(map(str, result['seeds'])) or '-'}")
    if result.get("reason"):
        print(f"reason: {result['reason']}")
    if result.get("certificate"):
        print(f"certificate: {result['certificate']}")
        print(f"policy: {result['policy']}")
    # UNKNOWN is a scientific verdict rather than a driver crash, but it must
    # not share an exit code with VERIFIED: only VERIFIED is decisive, and a
    # caller that tests the exit status would otherwise read a decline as a
    # success.  Mirror tlsfcertcheck: 0 VERIFIED, 3 UNKNOWN, 4 anything else.
    return {"VERIFIED": 0, "UNKNOWN": 3}.get(result["verdict"], 4)


if __name__ == "__main__":
    raise SystemExit(main())
