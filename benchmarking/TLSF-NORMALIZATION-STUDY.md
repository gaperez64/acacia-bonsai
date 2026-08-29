# TLSF normalization to Acacia automata

## Outcome

The equivalence-preserving TLSF normalization ladder does not produce smaller
or more solvable automata for the three decisive `lift`/`robot_grid` targets.
It is rejected as an Acacia-tailoring mechanism: it has zero losses, but also
zero alternate-only wins and no graph-size reduction.

The durable experiment infrastructure lives in `tlsf-tools`: source-only guard
features, normalized TLSF and LTL artifacts, exact Acacia-oriented HOA bundles,
per-orientation translation failures/timeouts, replay orchestration, atomic
resume files, and zero-loss/opposite-conflict summaries. Acacia supplies
uninstalled Spot-dependent automaton-generation and HOA-replay helpers behind
`-Dbuild_research_tools=true`; tlsf-tools links to neither and exposes no Spot
types. The production CLI and TLSF frontend are unchanged.

## Ladder and boundary

The schedules were `off`; `split`; `split,nnf,weak,bool-canon`; `pre-safe` plus
`split,match-safe`; `split,route-safe`; and bounded Sickert normalization at
one and two iterations. Each used Acacia's optional realizability simplifier,
worker orientation and X shift, input-before-output BDD registration,
Büchi/state-based acceptance request, `Small` preference, and translator
options (`simul=0`, `ba-simul=0`, `det-simul=0`, `tls-impl=1`,
`wdba-minimize=2`).

This is the formula-to-automaton boundary. Formula-level direct-strategy
shortcuts, safety-core witnesses, and Acacia's formula decomposition occur
before or around that boundary, so HOA replay is intentionally not described
as an end-to-end CLI replacement.

## Decisive screen

The official SYNTCOMP 2024 TLSF archive supplied `lift_gr13`,
`lift_unary_enc3`, and `robot_grid2_2`. For every schedule, the three worker
orientations had these exact sizes:

| target | real states/edges | unreal-formula | unreal-automaton |
|---|---:|---:|---:|
| `lift_gr13` | 225 / 2,758 | 89 / 1,457 | 33 / 206 |
| `lift_unary_enc3` | 221 / 2,855 | 615 / 13,117 | 172 / 2,262 |
| `robot_grid2_2` | 78 / 770 | 1,001 / 31,539 | 187 / 2,116 |

Every schedule therefore totaled 2,621 states and 57,080 edges. Aggregate
formula nodes were 3,599 for `off`, `split`, `pre-match`, and both Sickert
schedules; `split-safe` increased this to 4,056 and `route` to 4,209.
Different HOA hashes reflect syntactic/state-order choices, but not a smaller
automaton.

At a 5-second, 8 GiB, zero-swap replay cap, each schedule won exactly one of
nine workers: `robot_grid2_2` real in 0.10--0.12 s. The other eight timed out.
There were zero baseline losses, gains, errors, or simultaneous real/unreal
wins. A 20-second confirmation of all 21 real workers was identical:
`robot_grid2_2` solved under all seven schedules in 0.111--0.121 s, while both
`lift` targets timed out under all seven.

This agrees with the earlier B1 result: global formula-level normalization
lost coverage and gained no answer. B3's few gains came from constructing a
different specialized MP-NBA, not from presenting Spot with equivalent syntax;
that construction also lost 15 baseline answers and remains rejected as a
global frontend. In particular, `lift_unary_enc3` is still undecided.

## Next experiment boundary

Do not add more unconditional normalization passes. Any follow-up should use
the new bundle/replay infrastructure to evaluate an explicitly alternate
automaton construction or translator portfolio. A selector may use only the
stored `guard_*` fields and declared input/output counts, with at most three
predicates, family-grouped holdouts, zero baseline losses/opposite conflicts,
non-worse PAR-2, and wins in at least two families. Automaton metrics, solver
results, source paths, and filenames are forbidden selector inputs.
