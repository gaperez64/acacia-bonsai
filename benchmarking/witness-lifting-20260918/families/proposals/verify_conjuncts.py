#!/usr/bin/env python3
"""Exact conjunct-decomposed AIGER/LTL checker (plan section 7.2).

For a fixed controller, C |= (psi_1 and ... and psi_k) iff every C |= psi_j.
Splitting the target formula into its top-level conjuncts and checking each
against the SAME controller and the SAME cached automaton-from-circuit lets
Spot translate k small automata instead of one automaton for a large
k-way-conjoined formula, without weakening what is checked: every conjunct
must still verify, and no conjunct may omit any part of an implication's
assumption context (the split happens only at the top-level AND, never
inside an A -> (G1 and G2) implication's antecedent).

This is an extension of tlsf-tools/scripts/verify_aiger_ltl.py's approach
(same AIGER-circuit loading, same product-emptiness check per obligation),
not a replacement -- it exists because a monolithic n=10 arbiter conjunction
timed out under eager translation (see families/target-checks.tsv) while the
per-conjunct decomposition is the plan's own prescribed next step before
reaching for a candidate-restricted product (W6).

Usage:
  verify_conjuncts.py --aiger C.aag --tlsf T.tlsf --tlsf2ltl PATH
"""
import argparse
import subprocess
import sys
import time


def read_formula_text(tlsf, tlsf2ltl):
    proc = subprocess.run(
        [tlsf2ltl, "--format", "ltl", tlsf],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise SystemExit(proc.returncode)
    return proc.stdout.strip()


def top_level_conjuncts(formula):
    """Flatten top-level `And`s, and `A -> (G1 and G2 and ...)` implications,
    into a flat list of obligations that are jointly equivalent to the whole
    formula (plan section 7.2).

    Two rewrites only, both exact:
      - And(x1, x2, ...)      -> concat(split(x1), split(x2), ...)
      - Implies(A, And(G1,G2)) -> concat(split(A -> G1), split(A -> G2))
        (the full antecedent A is retained in every piece -- this is what
        makes the rewrite exact, not an approximation).
    Anything else (an implication whose consequent is not an And, a bare
    atom, an already-irreducible obligation) is returned as one piece.
    Never descends into an implication's antecedent, and never rewrites any
    other operator (in particular not <->, W, U, or a consequent that is
    itself an implication) -- those pass through unsplit rather than risk
    misreading TLSF's non-standard-implication constructs as ordinary ones.
    """
    import spot
    if formula.kind() == spot.op_And:
        out = []
        for i in range(formula.size()):
            out.extend(top_level_conjuncts(formula[i]))
        return out
    if formula.kind() == spot.op_Implies:
        antecedent, consequent = formula[0], formula[1]
        if consequent.kind() == spot.op_And:
            out = []
            for i in range(consequent.size()):
                out.extend(top_level_conjuncts(spot.formula.Implies(antecedent, consequent[i])))
            return out
    return [formula]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--aiger", required=True)
    parser.add_argument("--tlsf", required=True)
    parser.add_argument("--tlsf2ltl", default="tlsf2ltl")
    parser.add_argument("--per-conjunct-timeout", type=float, default=None,
                        help="not a hard timeout (single process); informational budget check only")
    args = parser.parse_args(argv)

    import spot

    formula_text = read_formula_text(args.tlsf, args.tlsf2ltl)
    formula = spot.formula(formula_text)
    aiger_text = open(args.aiger, encoding="utf-8").read()
    circuit = spot.aiger_circuit(aiger_text, bdd_dict=spot._bdd_dict)

    circuit_aps = set(circuit.input_names()) | set(circuit.output_names())
    formula_aps = {str(ap) for ap in spot.atomic_prop_collect(formula)}
    missing = sorted(formula_aps - circuit_aps)
    if missing:
        sys.stderr.write("verify-conjuncts: formula APs missing from AIGER symbols: " + ",".join(missing) + "\n")
        return 2

    system = circuit.as_automaton()
    conjuncts = top_level_conjuncts(formula)
    print(f"top-level conjuncts: {len(conjuncts)}")

    all_verified = True
    for idx, psi in enumerate(conjuncts):
        t0 = time.monotonic()
        bad = spot.translate(spot.formula.Not(psi))
        product = spot.product(system, bad)
        empty = product.is_empty()
        dt = time.monotonic() - t0
        status = "verified" if empty else "REFUTED"
        print(f"  [{idx}] {status} ({dt:.3f}s)  {str(psi)[:100]}")
        if not empty:
            all_verified = False
            word = product.accepting_word()
            print(f"      counterexample: {word}")

    if all_verified:
        print("verified (all conjuncts)")
        return 0
    print("REFUTED (at least one conjunct failed)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
