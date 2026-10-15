# Deterministic automaton and parity arm

`both:parity:spot` translates the original positive specification using Spot's deterministic,
generic-acceptance, high-level translation with the research route's translator options. It
preserves the declared input/output partition, including unused signals. Raw LTL is Mealy;
native TLSF uses the frontend's existing effective-Mealy lowering once, then the ordinary
Mealy-to-Moore controller conversion when its requested target is Moore. No realizability
simplification, decomposition, input/output swapping or extra timing shift precedes translation.

The production worker and `acacia-parity-route` share `src/solver/parity/route.{hh,cc}`. The
worker's forbidden language is classified using the same structural tiers as the research route:
Buchi or constant acceptance, finite-occurrence-free acceptance, exact deterministic-Buchi
conversion, then genuine parity acceptance. Tiers 1–3 use the existing deterministic forbidden
Buchi fast path. Tier 4 uses the positive original automaton, ACD conversion, two-step splitting,
and the ported strpm-simd solver with its existing adaptive `(k,t)` bounds and Spot fallback.
The port's GPL-3.0 provenance is unchanged; no additional Oink logic is included.

Production trusts completed REAL/UNREAL game results and strategies under the owner's trust
rule. It performs no game strategy or original-specification product check. The research tool
retains `--verify on --cross-check` as an explicit diagnostic. The unit suite independently
checks the exported AIG against a fresh negated-specification translation, checks Moore output
independence, and retains the existing scalar oracle and 480-game Spot comparison.

Decision-only calls do not construct or encode controllers. During synthesis, both verdicts
enter the existing parent rendezvous; only a selected REAL worker constructs its controller
and uses the ordinary controller writer. UNREAL never opens the destination. A failed export
resumes the suspended competitors. Parent death, interruption, race cancellation and the outer
invocation deadline use the existing process-group supervisor. The solver receives no local
wall allowance or deadline. The new arm uses the obligation helper's address-space cap and
cgroup-aware soft RSS share (half the available cap divided by scheduled workers). Its resource
stop is never accepted as a verdict.

Worker records include translate/orient/classify, tier, ACD/split/explicit game construction,
solve, final `k/t`, parameter changes, fallback boundaries and trust declarations. Cancellation
retains the last stage snapshot. Capacity/allocation/translation/export declines and stops are
included in the generated decline-cause census; diagnostics-off avoids the record channel.

`otf_sparse_formula_parity` inherits the registry's first `docker_default` configuration and
adds exactly this arm. It has registry role `sweep`; no shipping group or option default changes.
No new compile-time switch is necessary. The arm is available to an explicit `--arms` request
in every CLI build, including builds without research tools or native TLSF.

The driver handoff is `build_scratch/parity-arm/DRIVER-RUN.md`. It prepares the fixed 53-input
residual panel, 20 Acacia-only controls, and S's development/control panels, then compares the
complete frozen master portfolio with the candidate at 60 seconds, serially in 8 GiB/no-swap
scopes. Implementation ran correctness tests only. Coverage gains, losses and performance of
the complete race remain for the driver to measure before deciding on admission.

## Tiers 1–3: keep Spot's explicit solver

Tiers 1–3 use Spot's deterministic-Buchi shortcut: complete the automaton, build a co-Buchi
game, and solve it with Spot. Production requests only the verdict through `solve_game` and
builds a selected REAL controller through the deferred controller builder. A winning region
and diagnostic arena are kept only for verification or game/solution callbacks. Spot is the
only deterministic-Buchi engine. The comparisons below used a 60 s cap, serial 8 GiB scopes,
the same frozen binary within each comparison, and the 36 inputs whose parity worker reached
tiers 1–3. Every comparison had zero verdict conflicts. Spot (`spot-game`) decided 36/36,
with a PAR-2 total of 43–44 s.

Acacia's K-bounded antichain search on the deterministic-Buchi automaton decided 33/36,
with PAR-2 of 427 s and no gains. It lost `sort4-real` (0.4 s with Spot to timeout; the REAL
schedule exhausted near K=23), `workstation_resupply_pb_4` (2.1 s to timeout), and
`workstation_resupply_pb_5` (about 20 s to timeout). Each loss was confirmed in three cooled
repetitions. Verdict coverage was identical on the other 168 screened inputs.

The strpm-simd solver on the same co-Buchi game decided 35/36, with PAR-2 of 149 s and three
fallbacks to Spot. It lost `workstation_resupply_pb_5`: its adaptive `(k,t)` schedule raises
`t` one step at a time even on this two-priority game.

The tlsf-tools symbolic GR(1) solver using OxiDD decided 34/36, with PAR-2 of 265 s, losing
both workstation resupply inputs. The game was encoded as AIGER, with fairness on acceptance
and a false goal. The tier-3 deterministic-Buchi automata have 1,772 states / 101k edges for
`pb_4` and 5,504 / 639k for `pb_5`. Their state numbering has no structure, producing
next-state cones of 0.1–0.7 million AND gates. Even with state-first variable order and a
false-goal shortcut, solving takes 10.9 s and 164.5 s in single diagnostic runs, against 0.10 s
and 0.88 s for Spot's explicit split and solve on arenas of 17.5k and 99k states.

These are explicit games: an explicit attractor-based solver beats both symbolic GR(1) and
the bounded searches. Most time in tiers 1–3 goes into translation, such as 17.7 s of the
19 s total on `pb_5`, rather than solving the game (issue #261). The alternative engines remain on
`research/parity-buchi-engines`; they are not exposed as options on this branch. Evidence is
in `_bm-logs.otd/parity-buchi/`, `_bm-logs.otd/parity-strpm/`, and
`_bm-logs.otd/parity-gr1/`; archive IDs will be added later.
