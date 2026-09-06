# Frontier dossier: `arbiter_on_inpchange`

- **family key**: `param:tlsf/arbiters_zoo/parametric/arbiter_on_inpchange.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 7
- **frozen targets from this family**: `arbiter_on_inpchange_pb_5_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=1 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.04 |
| n=3 | REALIZABLE | REALIZABLE | REALIZABLE | 0.06 |
| n=4 | REALIZABLE | REALIZABLE | REALIZABLE | 1.29 |
| n=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.09 |
| n=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.11 |

## Boundary

- largest solved: `arbiter_on_inpchange_pb_4_pe_.ltl` (1.29 s)
- first unsolved: `arbiter_on_inpchange_pb_5_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `arbiter_on_inpchange_pb_5_pe_.ltl` | 53 | 16 | 736 | 18 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 0

## Structural conjecture

**The mechanism columns above describe the wrong workers, and the instance is not the
"small but hard" case it appears to be.**

`n=5` runs on the **equivariant solver**, not the classic backward path. A diagnostics trace
shows `equivariant=attempted` on 27 of 38 records, reaching `equivariant-after-closure` with
`eq_clients=5, eq_blocks=10, eq_orbits=6` — the solver is exploiting the symmetry between the
five arbiter clients, and that is where the 60 seconds go.

The classic workers all finish in **47 ms reporting `unknown`**, which is why the frozen-target
table records 53 automaton states, 16 rank coordinates and 736 actions for this instance. Those
numbers are real but they characterise workers that are not doing the work. The earlier claim
that this instance is "small by every measure and still unsolved, so whatever defeats it is not
size" does not survive: nothing was measured about the computation that actually runs.

This is the coverage gap the previous sprint recorded — the equivariant solver makes no
snapshot calls, so its fixed point is largely uninstrumented.

The forward arm does not settle it either. The forward preset disables the equivariant solver,
so `F` takes the classic path on this instance and times out there. **B and F have never been
compared like with like here.**

Conjecture, stated as a question rather than an answer: the growth is in the equivariant
closure at `n=5`, where 5 clients give 10 blocks and 6 orbits, and the orbit count crossing
some threshold makes the symmetric fixed point more expensive than the asymmetric one it
replaces. The `n=4` point solves in 1.29 s with `k_attempts=2` and `max_f_size=2905` on the
classic path.

## Next theorem

1. Instrument the equivariant fixed point — it currently emits no snapshots, so a family whose
   boundary lies inside it cannot be diagnosed at all. This blocks every other question here.
2. Re-run `n=4` and `n=5` with the equivariant solver **disabled** on both, to establish whether
   the classic path has the same cliff. If it does not, the cliff belongs to the equivariant
   path and this is not a bounded-game question.
3. Only then ask whether the forward solver helps, comparing the same backend on both sides.
