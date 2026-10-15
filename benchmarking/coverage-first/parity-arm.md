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
