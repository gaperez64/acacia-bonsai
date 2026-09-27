#!/usr/bin/env python3
"""Sound, deliberately incomplete all-n proof driver for M7.

The tier-1 generalizer learns predicates as concrete BuDDy BDDs and emits a
concrete AIG after instantiating them at a target ``n``.  Those objects are
excellent inputs to ``tlsfcertcheck``, but they are not a parametric term that
can be placed in an SMT query.  This driver therefore separates two facts:

* bus summaries are proved from their cardinality semantics for symbolic n;
* checker obligations are counted as proved only when a bounded parametric
  term for the certificate, game transition, and policy is available.

At present the second representation is absent.  The supported families thus
return UNKNOWN after discharging their bus summaries.  In particular, no
collection of successful concrete target checks is promoted to PROVED.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import os
import pathlib
import subprocess
import sys
import tempfile
import time
from collections.abc import Sequence
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "benchmarking/gr1-par2-20260923/oracle"))
from legacy_lift.capabilities import CAPABILITIES  # noqa: E402

Z3_PYTHON = ROOT / "build_scratch" / "smt" / "bin" / "python"
GENERALIZER = HERE / "generalize_gr1.py"
RESULTS = HERE / "m7-all-n.tsv"

TSV_FIELDS = (
    "family",
    "arity",
    "obligations_attempted",
    "obligations_proved",
    "summaries_discharged",
    "verdict",
    "seconds",
)

CHECKER_OBLIGATIONS = (
    "predicates-state-only",
    "y-is-union-of-x",
    "rank-levels-monotone",
    "certificate-goal-equals-game-justice",
    "reset-in-invariant",
    "every-invariant-state-has-least-rank",
    "policy-preserves-safety",
    "policy-preserves-invariant",
    "policy-counter-advances-exactly",
    "policy-rank-progress",
)


@dataclasses.dataclass(frozen=True)
class BusSchema:
    kind: str
    bus: str
    origin: str


@dataclasses.dataclass(frozen=True)
class FamilyPlan:
    source: str
    arity: int
    minimum_n: int
    schemas: tuple[BusSchema, ...]
    source_markers: tuple[str, ...]


FAMILIES = {
    "prioritized_arbiter": FamilyPlan(
        source=("tests/syntcomp-benchmarks/tlsf/prioritized_arbiter/"
                "parametric/prioritized_arbiter.tlsf"),
        arity=1,
        minimum_n=1,
        schemas=(
            BusSchema("AtMostOne", "g", "mutual_exclusion(g)"),
            BusSchema("NoneOf", "g", "inside X(NoneOf(g) U g_m)"),
        ),
        source_markers=("mutual_exclusion(g);", "!g[i] U g_m"),
    ),
    "arbiter_with_buffer": FamilyPlan(
        source=("tests/syntcomp-benchmarks/tlsf/arbiters_zoo/parametric/"
                "arbiter_with_buffer.tlsf"),
        arity=1,
        minimum_n=1,
        schemas=(BusSchema("AtMostOne", "g", "G mutual_exclusion(g)"),),
        source_markers=("G mutual_exclusion(g);", "bufferX(try, ack, i) U g[i]"),
    ),
    "amba_decomposed_lock": FamilyPlan(
        source=("tests/syntcomp-benchmarks/tlsf/amba/amba_decomposed/"
                "parametric/amba_decomposed_lock.tlsf"),
        arity=1,
        minimum_n=2,
        schemas=(
            BusSchema("AtMostOne", "HGRANT", "G mutual_exclusion(HGRANT)"),
            BusSchema("AtLeastOne", "HGRANT", "G (|| HGRANT[i])"),
        ),
        source_markers=("G mutual_exclusion(HGRANT);",
                        "G (||[0 <= i < n] HGRANT[i]);"),
    ),
}


@dataclasses.dataclass
class QueryResult:
    name: str
    result: str
    seconds: float
    model: str = ""


@dataclasses.dataclass
class ProofResult:
    family: str
    arity: int
    attempted: tuple[str, ...]
    proved: tuple[str, ...]
    summaries: tuple[QueryResult, ...]
    verdict: str
    reasons: tuple[str, ...]
    seconds: float
    cross_checks: tuple[str, ...] = ()


def _load_z3(argv: Sequence[str]) -> Any:
    try:
        import z3  # pylint: disable=import-outside-toplevel
        return z3
    except ModuleNotFoundError:
        if (Z3_PYTHON.is_file() and
                pathlib.Path(sys.prefix).resolve() !=
                Z3_PYTHON.parent.parent.resolve()):
            os.execv(str(Z3_PYTHON), [str(Z3_PYTHON), __file__, *argv])
        raise RuntimeError(
            f"z3 is unavailable; run with {Z3_PYTHON.relative_to(ROOT)}")


def _remaining_ms(deadline: float) -> int:
    return max(1, int(1000 * (deadline - time.monotonic())))


def _check_validity(z3: Any, name: str, assumptions: Sequence[Any],
                    proposition: Any, deadline: float) -> QueryResult:
    """Assert the negation of one lemma; only unsat discharges it."""
    if time.monotonic() >= deadline:
        return QueryResult(name, "unknown", 0.0, "driver timeout")
    solver = z3.SolverFor("QF_LIA")
    solver.set(timeout=_remaining_ms(deadline))
    solver.add(*assumptions)
    solver.add(z3.Not(proposition))
    started = time.monotonic()
    answer = solver.check()
    seconds = time.monotonic() - started
    if answer == z3.unsat:
        return QueryResult(name, "unsat", seconds)
    if answer == z3.sat:
        return QueryResult(name, "sat", seconds, str(solver.model()))
    return QueryResult(name, "unknown", seconds, solver.reason_unknown())


def _summary_queries(z3: Any, schema: BusSchema, phase: str, arity: int,
                     minimum_n: int, deadline: float) -> list[QueryResult]:
    """Prove an exact finite-partition abstraction of one bus schema.

    ``generic_other`` is one distinguished client outside the obligation's
    own indices.  ``anonymous_count`` is the exact number of asserted bits in
    the remaining clients.  Thus ``someone_else`` is derived, never assumed.
    The proof covers the boundary case where no generic other exists as well.
    """
    stem = f"{schema.kind}({schema.bus})@{phase}"
    n = z3.Int(f"{stem}.n")
    own = [z3.Bool(f"{stem}.own_{index}") for index in range(arity)]
    generic_other = z3.Bool(f"{stem}.generic_other")
    anonymous_count = z3.Int(f"{stem}.anonymous_count")
    has_generic = n > arity
    generic_count = z3.If(z3.And(has_generic, generic_other), 1, 0)
    other_count = generic_count + anonymous_count
    own_count = z3.Sum([z3.If(bit, 1, 0) for bit in own])
    total_count = own_count + other_count
    someone_else = z3.Or(z3.And(has_generic, generic_other),
                         anonymous_count > 0)

    # This is the semantic bridge, not an assumption about the schema: split
    # the exact bus cardinality into own, one distinguished other, and tail.
    bridge = (
        n >= minimum_n,
        n >= arity,
        anonymous_count >= 0,
        anonymous_count <= z3.If(has_generic, n - arity - 1, 0),
        z3.Implies(z3.Not(has_generic), z3.Not(generic_other)),
    )
    if schema.kind == "AtMostOne":
        semantic = total_count <= 1
        bounded = z3.And(own_count <= 1, other_count <= 1,
                         z3.Or(own_count == 0, other_count == 0))
        lemmas = (
            ("someone_else-exact", someone_else == (other_count > 0)),
            ("schema-exact", semantic == bounded),
            ("schema-implies-rest-at-most-one",
             z3.Implies(semantic, other_count <= 1)),
            ("schema-implies-own-excludes-rest",
             z3.Implies(semantic,
                        z3.Implies(own_count > 0, z3.Not(someone_else)))),
        )
    elif schema.kind == "AtLeastOne":
        semantic = total_count >= 1
        bounded = z3.Or(*own, someone_else)
        lemmas = (
            ("someone_else-exact", someone_else == (other_count > 0)),
            ("schema-exact", semantic == bounded),
            ("schema-implies-own-or-other-witness",
             z3.Implies(semantic, bounded)),
        )
    elif schema.kind == "NoneOf":
        semantic = total_count == 0
        bounded = z3.And(*(tuple(z3.Not(bit) for bit in own) +
                           (z3.Not(someone_else),)))
        lemmas = (
            ("someone_else-exact", someone_else == (other_count > 0)),
            ("schema-exact", semantic == bounded),
            ("schema-implies-no-other",
             z3.Implies(semantic, z3.Not(someone_else))),
        )
    else:
        raise ValueError(f"unsupported bus schema {schema.kind!r}")
    return [_check_validity(z3, f"{stem}:{suffix}", bridge, proposition,
                            deadline)
            for suffix, proposition in lemmas]


def _source_matches(plan: FamilyPlan) -> tuple[bool, str]:
    path = ROOT / plan.source
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return False, f"cannot read family source {path}: {exc}"
    missing = [marker for marker in plan.source_markers if marker not in text]
    if missing:
        return False, "family source no longer matches audited schemas: " + repr(missing)
    return True, ""


def _bounded_ir_obstacle(family: str) -> tuple[str, ...]:
    capability = CAPABILITIES[family]
    inv = capability.invariant_arities
    move = capability.move_arities
    measurements = (
        f"concrete projection measurements: invariant arities={inv or 'none'}, "
        f"move arities={move or 'none'}; measurements are evidence, not an all-n lemma"
    )
    return (
        measurements,
        "no parametric AST is retained for inv/x rank templates; the seed BDDs "
        "are instantiated over range(target.n) before they are emitted",
        "the game transition and justice/fairness predicates are compiled from "
        "the concrete target AAG",
        "the deterministic policy is Skolemized only after target-size move "
        "relations and the target-size goal counter have been constructed",
    )


def _cross_check(family: str, timeout: float) -> tuple[bool, tuple[str, ...]]:
    """Run the mandatory tier-1 checks for a prospective PROVED result."""
    reports = []
    deadline = time.monotonic() + timeout
    with tempfile.TemporaryDirectory(prefix=f"m7-{family}-") as raw:
        temporary = pathlib.Path(raw)
        environment = dict(os.environ)
        environment["GENERALIZE_GR1_RESULTS"] = str(temporary / "tier1.tsv")
        for n in (9, 13):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                reports.append(f"n={n}: UNKNOWN (cross-check timeout)")
                return False, tuple(reports)
            out = temporary / f"n{n}"
            command = [
                sys.executable, str(GENERALIZER), "--family", family,
                "--target", str(n), "--timeout", str(remaining),
                "--check-method", "certificate", "--out", str(out),
            ]
            try:
                proc = subprocess.run(
                    command, cwd=ROOT, env=environment, text=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    timeout=remaining + 5, check=False)
            except subprocess.TimeoutExpired:
                reports.append(f"n={n}: UNKNOWN (tier-1 process timeout)")
                return False, tuple(reports)
            verified = proc.returncode == 0 and "VERIFIED" in proc.stdout
            reports.append(f"n={n}: {'VERIFIED' if verified else 'DISAGREES'}")
            if not verified:
                detail = (proc.stderr or proc.stdout).strip().replace("\n", " ")[-300:]
                reports.append(f"n={n} detail: {detail}")
                return False, tuple(reports)
    return True, tuple(reports)


def prove_family(family: str, max_arity: int, timeout: float, z3: Any) -> ProofResult:
    started = time.monotonic()
    deadline = started + timeout
    plan = FAMILIES[family]
    reasons: list[str] = []
    summaries: list[QueryResult] = []
    proved: list[str] = []

    matches, source_reason = _source_matches(plan)
    if not matches:
        reasons.append(source_reason)
    elif plan.arity > max_arity:
        reasons.append(
            f"family needs arity {plan.arity}, above --max-arity {max_arity}")
    else:
        # Current and successor are separate copies: this also discharges the
        # abstraction when a schema occurs below X.
        for schema in plan.schemas:
            for phase in ("current", "successor"):
                summaries.extend(_summary_queries(
                    z3, schema, phase, plan.arity, plan.minimum_n, deadline))

    failed_summaries = [item for item in summaries if item.result != "unsat"]
    if failed_summaries:
        for item in failed_summaries:
            reasons.append(
                f"summary {item.name} was not discharged: {item.result}"
                + (f" ({item.model})" if item.model else ""))

    # The summary layer is sound, but it is not a substitute for the concrete
    # certificate/game/policy terms in tlsfcertcheck's implications.  Refuse
    # to create free Boolean placeholders: doing so could turn assumptions
    # about those terms into an apparent proof.
    if matches and plan.arity <= max_arity and not failed_summaries:
        reasons.extend(_bounded_ir_obstacle(family))

    verdict = "UNKNOWN"
    cross_checks: tuple[str, ...] = ()
    if len(proved) == len(CHECKER_OBLIGATIONS) and not failed_summaries:
        # This branch is intentionally unreachable until bounded semantic
        # obligation builders are added.  Keeping the gate here makes it
        # impossible to add PROVED later without also running the mandated
        # independent-size tier-1 checks.
        ok, cross_checks = _cross_check(
            family, max(0.001, deadline - time.monotonic()))
        verdict = "PROVED" if ok else "UNKNOWN"
        if not ok:
            reasons.append("tier-1 cross-check disagreed with the all-n proof")

    if max_arity >= 2 and verdict != "PROVED":
        reasons.append("arity 2 was not attempted because the arity-1 proof did not close")
    return ProofResult(
        family=family,
        arity=plan.arity,
        attempted=CHECKER_OBLIGATIONS,
        proved=tuple(proved),
        summaries=tuple(summaries),
        verdict=verdict,
        reasons=tuple(reasons),
        seconds=time.monotonic() - started,
        cross_checks=cross_checks,
    )


def _write_tsv(result: ProofResult) -> None:
    rows: dict[str, dict[str, str]] = {}
    if RESULTS.is_file():
        with RESULTS.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream, delimiter="\t")
            if reader.fieldnames == list(TSV_FIELDS):
                rows.update((row["family"], row) for row in reader)
    rows[result.family] = {
        "family": result.family,
        "arity": str(result.arity),
        "obligations_attempted": str(len(result.attempted)),
        "obligations_proved": str(len(result.proved)),
        "summaries_discharged": str(sum(
            item.result == "unsat" for item in result.summaries)),
        "verdict": result.verdict,
        "seconds": f"{result.seconds:.6f}",
    }
    order = {family: index for index, family in enumerate(FAMILIES)}
    temporary = RESULTS.with_name(f".{RESULTS.name}.{os.getpid()}.tmp")
    with temporary.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=TSV_FIELDS, delimiter="\t",
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(sorted(rows.values(),
                                key=lambda row: (order.get(row["family"], 999),
                                                 row["family"])))
    temporary.replace(RESULTS)


def _print_result(result: ProofResult) -> None:
    discharged = sum(item.result == "unsat" for item in result.summaries)
    print(f"{result.verdict} family={result.family} arity={result.arity}")
    print(f"checker obligations: attempted={len(result.attempted)} "
          f"proved={len(result.proved)}")
    for name in result.attempted:
        status = "UNSAT" if name in result.proved else "BLOCKED"
        print(f"  {status} {name}")
    print(f"summaries discharged: {discharged}/{len(result.summaries)}")
    for schema in FAMILIES[result.family].schemas:
        print(f"  source {schema.kind}({schema.bus}): {schema.origin}")
    for item in result.summaries:
        print(f"  {item.result.upper()} {item.name} ({item.seconds:.6f}s)")
        if item.model:
            print(f"    {item.model}")
    print("encoding: exact bus cardinality = own clients + one generic other "
          "+ anonymous-rest count; Boolean summaries (including someone_else) "
          "are derived from that partition")
    for reason in result.reasons:
        print(f"reason: {reason}")
    for report in result.cross_checks:
        print(f"cross-check: {report}")
    print(f"results: {RESULTS}")


def _positive_seconds(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", required=True, choices=tuple(FAMILIES))
    parser.add_argument("--max-arity", type=int, choices=(1, 2), default=1)
    parser.add_argument("--timeout", type=_positive_seconds, default=120.0,
                        metavar="S")
    args = parser.parse_args(arguments)
    try:
        z3 = _load_z3(arguments)
    except RuntimeError as exc:
        parser.error(str(exc))
    result = prove_family(args.family, args.max_arity, args.timeout, z3)
    _write_tsv(result)
    _print_result(result)
    return {"PROVED": 0, "REFUTED": 1, "UNKNOWN": 3}[result.verdict]


if __name__ == "__main__":
    raise SystemExit(main())
