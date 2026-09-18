#!/usr/bin/env python3
"""Manual schema proposal for the arbiter/round_robin_arbiter family (W4.3/W5).

proposal_origin=manual. Informed by the seed witnesses (Acacia-synthesized,
checker-verified controllers for n=2,3,4, see families/seeds/*/controllers),
which established that a small *stateful* controller exists and is needed
(the spec carries no assumption that a request stays raised, so a strategy
that only looks at the instantaneous request vector cannot be correct -- it
must latch pending requests). This is a hand-derived generalization of that
shape, not a decompilation of the seed AIGER circuits. The exact checker,
not this script, is the authority on correctness at every n it is run on.

Schema (schema vocabulary from plan section 8.1):
  memory:    n `pending` flags (one per client) + a one-hot pointer over
             positions 1..n-1 (n-1 latches; position 0 is implied by "all
             of those are 0"), bounded cyclic increment mod n by rotation
  read:      named scalar r_i per client
  test:      pending_i, (pointer == i)
  update:    pending_i := (pending_i or r_i) and not grant_i
             pointer   := rotate right by one position (mod n)
  output:    grant_i := pending_i and (pointer == i)
  replicate: the pending/grant rule is one fixed local rule per index i

A binary-encoded (log2 n bit) pointer with an explicit ripple-carry
incrementer and a mod-n wraparound comparator was tried first and reliably
crashed the pinned Spot build's spot.aiger_circuit parser (aiger.cc:354,
register_new_lit_ assertion) for every n with a 2+ bit pointer (n=3,4,10; the
single-bit n=2 case happened not to trigger it). Reproduced with AND-gate
sharing disabled too, so it is not this script's common-subexpression cache;
it looks like a genuine parser limitation on that gate pattern under the
pinned build, not a defect in the proposed circuit's logic. Not chased
further -- switching to one-hot sidesteps the comparator/incrementer pattern
entirely and this is a research proposer, not the checker.

Every request is latched into `pending` the instant it is raised and stays
latched until granted, so it survives being dropped before the pointer
reaches that index. The pointer sweeps 0..n-1 unconditionally every step
(independent of grants or requests), so a client whose pending flag is set
receives its grant within at most n steps of being latched -- this is the
"bounded cyclic increment" primitive, not a rediscovery of the seed's raw
gate structure.

Usage:
  build_arbiter_witness.py --n 10 --output out.aag
"""
import argparse


class AigerBuilder:
    def __init__(self):
        self.next_var = 1
        self.gates = []          # (lhs_lit, rhs0_lit, rhs1_lit) in var order
        self.and_cache = {}
        self.latches = []        # (lhs_lit, next_lit) reset=0 implicit

    def new_var(self):
        lit = 2 * self.next_var
        self.next_var += 1
        return lit

    def new_latch(self):
        lit = self.new_var()
        self.latches.append([lit, None])  # next filled in later
        return lit

    def set_latch_next(self, latch_lit, next_lit):
        for entry in self.latches:
            if entry[0] == latch_lit:
                entry[1] = next_lit
                return
        raise KeyError(latch_lit)

    @staticmethod
    def NOT(lit):
        return lit ^ 1

    def AND(self, a, b):
        if a == 0 or b == 0:
            return 0
        if a == 1:
            return b
        if b == 1:
            return a
        if a == b:
            return a
        if a == self.NOT(b):
            return 0
        key = (a, b) if a < b else (b, a)
        if key in self.and_cache:
            return self.and_cache[key]
        lhs = self.new_var()
        self.gates.append((lhs, key[0], key[1]))
        self.and_cache[key] = lhs
        return lhs

    def OR(self, a, b):
        return self.NOT(self.AND(self.NOT(a), self.NOT(b)))


def build(n: int):
    if n < 1:
        raise ValueError("n must be >= 1")

    b = AigerBuilder()

    # Inputs r_0..r_{n-1}, in order.
    inputs = [b.new_var() for _ in range(n)]

    # State: n pending latches (reset 0). Pointer is one-hot over positions
    # 1..n-1, represented by n-1 latches (reset 0); position 0 is implied by
    # "all of those are 0", which is both the true initial state and the
    # correctly-recurring one-hot case, so no explicit non-zero reset value
    # is needed anywhere.
    pending = [b.new_latch() for _ in range(n)]
    hot = [b.new_latch() for _ in range(n - 1)]  # hot[i] means position i+1

    def at(pos):
        if pos == 0:
            # NOR of all other one-hot bits.
            acc = 0
            for h in hot:
                acc = b.OR(acc, h)
            return b.NOT(acc)
        return hot[pos - 1]

    grants = []
    for i in range(n):
        eq_i = at(i)
        grant_i = b.AND(eq_i, pending[i])
        grants.append(grant_i)
        next_pending_i = b.AND(b.OR(pending[i], inputs[i]), b.NOT(grant_i))
        b.set_latch_next(pending[i], next_pending_i)

    # Rotate: next `hot[i]` (position i+1) is set iff position i was active
    # this step, for i = 0..n-2. Position 0 wrapping back in is implicit
    # (all `hot` bits false) once position n-1 was active and rotates off
    # the end without setting any `hot` bit.
    for i in range(n - 1):
        b.set_latch_next(hot[i], at(i))

    return b, inputs, grants, n


def render(b: AigerBuilder, inputs, outputs, n: int) -> str:
    lines = []
    max_var = b.next_var - 1
    lines.append(f"aag {max_var} {len(inputs)} {len(b.latches)} {len(outputs)} {len(b.gates)}")
    for lit in inputs:
        lines.append(str(lit))
    for lhs, nxt in b.latches:
        assert nxt is not None
        lines.append(f"{lhs} {nxt}")
    for lit in outputs:
        lines.append(str(lit))
    for lhs, r0, r1 in b.gates:
        lines.append(f"{lhs} {r0} {r1}")
    for i in range(len(inputs)):
        lines.append(f"i{i} r_{i}")
    for i in range(len(outputs)):
        lines.append(f"o{i} g_{i}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--n", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    b, inputs, outputs, n = build(args.n)
    text = render(b, inputs, outputs, n)
    with open(args.output, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {args.output}: n={n} max_var={b.next_var - 1} "
          f"latches={len(b.latches)} and_gates={len(b.gates)}")


if __name__ == "__main__":
    main()
