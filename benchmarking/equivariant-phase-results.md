# Equivariant solver phase results (2026-07-12)

## Implemented

- Diagnostic decline reasons distinguish representative/orbit caps, record
  low-block payoff, and include verified star-generator progress.
- `vector_backed::from_antichain_unchecked()` provides a debug-checked bulk
  path for coordinate permutations; other downsets retain `apply()`.
- For groups with more than four clients, the solver uses configured
  Mona/actioner/input-picker components over representative inputs and closes
  the raw downset under verified generators after every predecessor step.
- Groups with at most four clients use the exact explicit-orbit sweep.  Staged
  diagnostics showed that generator closure cost 4.75 s and 6.66 s in two
  iterations of `arbiter_on_inpchange4`; the sweep reduced the instance from a
  repeatable 17 s timeout to about 2.3 s.
- Focused benchmark runners now stop their named systemd scope on timeout, so
  decomposed solver children cannot escape process-group cleanup.

## Correctness verification

- The permutation fast path is compared with the generic `apply()` path on
  invariant and non-invariant random downsets.
- Generator closure is compared with the full permutation-group intersection.
- The representative/picker loop and production orbit sweep are each compared
  with an independent retained sweep oracle.
- End-to-end real and unreal fixtures check K, the returned downset, group
  invariance, initial-state membership, and closure for every concrete input.
- Assertions-enabled and release symmetry suites both pass 6/6.
- The posets unchecked-factory test passes; its implementation is merged in
  posets PR #27 and the submodule points to merge commit `cc53ca4`.

## Fixed-panel gate

Three sequential repeats used a 17 s timeout, one test job, and an 8 GiB
systemd memory limit with swap disabled.

| Configuration | Solved per round | Mean PAR-2 |
|---|---:|---:|
| `best_decomp_mona` baseline | 8/11 | 193.519 s |
| equivariant hybrid | 8/11 | 180.775 s |

There were no verdict mismatches.  The hybrid improved mean PAR-2 by 6.6%.
`arbiter_on_inpchange4` improved from a 12.536 s median to 2.281 s.  The loss
panel stayed within the specified `max(0.3 s, 5%)` noise threshold.

## Full-corpus gate

The same-checkout four-slice `ab/syntcomp24/0s-1s` campaign covered all 994
instances with a 17 s cap.

| Configuration | Meson-OK | Timeouts | PAR-2 |
|---|---:|---:|---:|
| `best_decomp_mona` baseline | 823 | 171 | 6247.546 s |
| equivariant hybrid | 823 | 171 | 6234.699 s |

The instance universes are identical and there are zero outcome differences.
The hybrid lowers PAR-2 by 12.847 s.  The largest win is
`arbiter_on_inpchange4` (13.860 s to 2.247 s).

The required post-flip matrix was then repeated under the shipping names and
with the configuration order reversed:

| Configuration | Meson-OK | Timeouts | PAR-2 |
|---|---:|---:|---:|
| `best_decomp_mona` shipping | 821 | 173 | 6291.175 s |
| `best_decomp_mona_noequivariant` | 821 | 173 | 6290.596 s |

That run also has zero outcome differences; its 0.579 s (0.009%) raw loss is
far below run-to-run noise.  Combining the two opposite-order full-corpus runs
as a counterbalanced pair gives mean PAR-2 6262.937 s for equivariant shipping
and 6269.071 s for classic-only, a 6.134 s net win.  Each paired run has equal
solved counts, and the three-repeat fixed panel independently retains the
large symmetry-family win without a loss outside its noise threshold.

The historical absolute `amba_decomposed_lock13 <= 4 s` gate is not applicable
to this checkout: both current configurations reach Spot's deterministic fast
path before the equivariant solver and time out under the normal 17 s
three-branch invocation.  Their same-checkout outcomes and timings agree.

## Phase-3 decision

Focused realizability-only diagnostics covered
`amba_decomposed_arbiter{3..6}`, `load_balancer{3..7}`,
`prioritized_arbiter{3..7}`, and `round_robin_arbiter{3..7}`.

- Reachable AMBA, load-balancer, and round-robin probes verified zero star
  generators, not a partial subgroup.
- Prioritized-arbiter probes had no indexed input family at the solver.
- AMBA and load-balancer `.part` files have complete indexed input/output
  families, so the sampled failures are not partial-index coverage failures.
- No case reaches the low-block-payoff guard.

Consequently, lowering `MIN_BLOCKS`, relaxing partial-family handling, and
Young-subgroup support have no evidence-backed market.  Phase 3 is skipped.
The ISO-search budget is also unnecessary because sampled detection overhead
is negligible.

## Shipping decision

The primary success bar is met: strictly lower full-corpus PAR-2, equal solved
count, zero mismatches, and no fixed-panel loss outside noise.  The shipping
`best_decomp_mona` preset enables the equivariant hybrid, with
`best_decomp_mona_noequivariant` retained as the escape hatch.

Output-side `compute_T` deduplication remains parked; profiling identifies
generator closure, not output enumeration, as the relevant bottleneck.
