# Remaining work to close the sprint

M0–M4 are done and recorded in `decisions.md`. This is the ordered plan for the rest, written after
the coverage audit that showed four of M4's five demonstration targets were instances that already
solved at 120 s — so the measured result at that point was **one** previously-unsolved instance
closed (`arbiter` n=10), not five. Targets from here on are drawn from the *unsolved* set only.

## Standing correction to the plan

The plan expected M4 to fix `round_robin_arbiter` n=10, the previous sprint's
`no_recognized_candidate`. Measurement superseded that: the family's winning region has separability
arity `n` at every `n`, so no conjunctive template reaches it at any size, and the honest outcome is
the named decline M4 now produces. It is listed under B3 below, not as an M4 defect.

## B1 — work not run or not wired (largest, cheapest)

| item | instances | state |
|---|---|---|
| generalizer family table covered only 5 of the 10 measured families | 18 | table extended; sweep running |
| `m4-invariant-separability-round2.tsv` never read by the generalizer | — | merged into the canonical file |
| 83 reducible instances never classified | 83 | see B1a–B1c |

- **B1a** arity measurement for `amba_case_study` (2). REAL, measurable now.
- **B1b** wider-`n` probe for `rw_arbiter` (2) and `lift_unary_enc` (3), both `unstable` because the
  probe range did not reach their stable regime.
- **B1c** two-parameter handling for `chomp` (12) and `robot_grid` (3). `chomp` measured
  `replicated` on *both* axes in `m4-alignment-2param.tsv`, so it may be reachable despite the
  plan's prediction; `robot_grid` is bus-dominated (local fraction 0.18) and probably is not.

## B2 — the one-hot monitor encoding (28 instances, the largest single block)

`gr1_monitor_game.py` spends `2^u` latches on a `u`-bit shift register, which is what puts the `u`
axis of `round_robin_arbiter_unreal1` (21) and `load_balancer_unreal1` (7) out of reach. Brief
written (`brief-m1b.md`): add `--latch-encoding {one-hot,binary,auto}`, keep the reduction exact,
and report how far up the `u` axis each encoding reaches. Note both families are UNREAL, so closing
them also needs M5.

## B3 — algorithmic gaps

- **Streett/Rabin** (25 instances, 7 families). Their guarantee side contains an implication between
  recurrence properties, which the single top-level GR(1) split cannot absorb. Out of reach of this
  route by acceptance condition, not by parameterization. Record as such; do not attempt.
- **Invariant strengthening** (17 instances: `round_robin_arbiter`, `lift`,
  `amba_decomposed_arbiter`). `min_k = n`, and the cheap existential route is refuted (§4 of
  `m4-structure.md`). Needs a real strengthening search. z3 5.1.0 is now installed in
  `build_scratch/smt`, so a bounded SyGuS-style search is possible; this is the one place where the
  Maderbacher–Bloem recipe is followed literally rather than adapted.

## B4 — solver capacity

`tlsfsolve` saturates its BDD exponent at 22 with no override, and reports exhaustion as a generic
error sharing an exit code with real failures. Blocks seeds for `collector_v1` (n=5),
`abcg_arbiter` (n=4) and `collector_v3` (n=5). Brief written (`brief-m2b.md`). Separately,
`tlsfcertcheck`'s closed-loop route builds a monolithic transition relation over
`nstate + ngoals` variables and stops deciding for `arbiter` from n=5 up at ~460 k nodes,
independent of its cap — that is what costs the independent second opinion at a target, so it is
briefed with B4 rather than left as a footnote on M4.

## M5 — UNREAL families

Dual certificate from M2 (environment region, counter-strategy, environment rank), generalized the
same way as the REAL side, plus small-support padding for the pairwise obstructions. Covers
`round_robin_arbiter_unreal2`, `prioritized_arbiter_unreal2`, `load_balancer_unreal2`,
`amba_case_study_unreal`, and — with B2 — the two `*_unreal1` families. M2 currently exports the
unrealizable side flagged as having no environment counter-strategy; that flag is the gap.
`rru2` n=7 is the previous sprint's named leftover and is the regression target here.

## M6 — integrate and measure

Copy `acacia-witness-lift.py` forward from the closed sprint tree (it is read-only there), swap in
`generalize_gr1.py` + `tlsfcertcheck`, keep its shared deadline, cold workspace, VERIFIED-only
invariant and evidence sidecar. Then a cold campaign over the unsolved reducible instances charging
every stage — seed solves, generalization, grounding, target check, wrapper overhead — reporting per
family: census class, lifted?, verified at target?, failing stage; and coverage deltas against B, S
and ltlsynt. Warm reuse reported separately from cold, per the plan.

## M7 — all-n proof (tier 2)

z3 5.1.0 is installed, so this is no longer blocked on tooling. The real difficulty stands: M&B's
single QF-LIA query assumes a fixed variable set, while these families grow their interface with
`n`, so it needs quantified index reasoning or a cutoff argument. Attempt on the arity-1 families
first, where the per-client template makes the induction cleanest, and report honestly if the
quantified query does not discharge.

## Closing

A `closing/` synthesis in the style of the previous sprint, checked against the plan's own
definition of done. Plus one decision that is not mine: whether to bump Acacia's `tlsf-tools`
submodule pin off `b42d5ef` to pick up the `param-lift-gr1` commits. The gitlink has stayed
unstaged throughout and needs explicit approval.
