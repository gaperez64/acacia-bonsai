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
conversion, then genuine parity acceptance. Tiers 1–3 select their engine at runtime with
`--parity-buchi-engine spot-game|antichain`. The default, `spot-game`, preserves the existing
deterministic forbidden-Buchi fast path: completion, a co-Buchi game, and Spot's `solve_game`.
`antichain` instead enters the ordinary worker's K-bounded solver after the Spot shortcut,
using the build's configured automaton preprocessing, Boolean-state discovery, downsets,
backend defaults and K schedule (`-M`, `-K`, `-I`). Synthesis uses the ordinary backward
fallback and deferred strategy builder. No compile-time option is added.

The classified DBA recognizes the REAL worker's forbidden language, `!phi`. Acacia interprets
an existential NBA for a language L as a universal co-Buchi automaton for its complement:
all runs must visit accepting states only finitely often. Thus this DBA directly serves the
REAL check. For UNREAL, `dualize` first completes the DBA (including missing transitions),
then complements it to a deterministic co-Buchi automaton for `phi`. Spot's exact `remove_fin`,
`to_generalized_buchi`, `degeneralize` and `sbacc` produce an existential, state-based NBA for
that same language. Fin elimination nondeterministically guesses a point after the final
bad visit and enforces no later bad visits; this is an exact language conversion, without a
deterministic-Buchi restriction. Starting instead with a DBA for `phi` reverses these roles:
it directly serves UNREAL, and its complemented NBA serves REAL. Neither bounded failure
nor failed preparation proves the opposite polarity; if both checks fail, the worker is UNKNOWN.
Each polarity first copies the NBA into a fresh dictionary with solver inputs registered
before solver outputs, exactly as the ordinary worker does. All edge labels are renamed
bijectively; the UNREAL I/O swap also changes this variable order. This preserves signal
identity and the MONA precomputer's input/output/state-variable layout.
Each final NBA is completed with a rejecting sink before state-acceptance lowering. This
preserves its language and retains legal letters with no original transition, which the
configured MONA precomputer otherwise omits. Completion also follows any input shift.

The checks run sequentially within the same worker, REAL first through its full configured
K schedule, then UNREAL through its full schedule. No new process, local deadline, allowance,
or memory share is introduced. A costly REAL check can therefore leave UNREAL unfinished at
the outer cap; this tradeoff is part of what the driver measures. UNREAL swaps the I/O cubes
and uses the existing `push_aps` transformation on its solver inputs to enforce environment
Moore timing for an original Mealy objective. An original Moore objective instead requires
that transformation for REAL, with ordinary input-shift controller conversion afterwards;
its environment check is Mealy. Production TLSF already uses effective Mealy lowering, so
it still undergoes the frontend's timing lowering exactly once.

Tier 4 is unaffected by the flag. It uses the positive original automaton, ACD conversion,
two-step splitting, and the ported strpm-simd solver with its existing adaptive `(k,t)` bounds and Spot fallback.
The port's GPL-3.0 provenance is unchanged; no additional Oink logic is included.

Production trusts completed REAL/UNREAL game results and strategies under the owner's trust
rule. It performs no game strategy or original-specification product check. The research tool
retains `--verify on --cross-check` as an explicit diagnostic. The unit suite independently
checks both tier-1–3 engines on small REAL/UNREAL Mealy/Moore games, checks exported AIGs
against a fresh negated-specification translation and Moore output independence, and retains
the existing scalar oracle and 480-game Spot comparison.

Decision-only calls do not construct or encode controllers. During synthesis, both verdicts
enter the existing parent rendezvous; only a selected REAL worker constructs its controller
and uses the ordinary controller writer. UNREAL never opens the destination. A failed export
resumes the suspended competitors. Parent death, interruption, race cancellation and the outer
invocation deadline use the existing process-group supervisor. The solver receives no local
wall allowance or deadline. The new arm uses the obligation helper's address-space cap and
cgroup-aware soft RSS share (half the available cap divided by scheduled workers). Its resource
stop is never accepted as a verdict.

Worker records include translate/orient/classify, tier, ACD/split/explicit game construction,
solve, parameter changes, fallback boundaries and trust declarations. Every parity event identifies
the selected tier engine in `route` and `effective_backend`: `spot-fast`, `antichain`, or
`strpm-simd`. Antichain search events retain each K reached;
`parity_parameters`, `parity_solved` and `parity_verdict` report the winning K (`t=0`, `pairs=0`).
Its trust declaration is `exact_antichain`; game engines retain `exact_game`. Tier-4
`parity_verdict` now carries the completed adaptive `k`, `t`, `pairs`, `fallback` and
`fallback_reason` from `parity_solved`, including fallback outcomes. Cancellation
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

The deterministic-Buchi engine comparison handoff is
`build_scratch/parity-buchi/DRIVER-RUN.md`. Its preparation script derives the focused panel
from all `parity_tier` events with `tier < 4` in the previous OTP screen, including later
cancelled arms. It then compares the same frozen candidate binary with the default flag and
`--parity-buchi-engine antichain`, first on that panel, then on the original complete
204-input screen list. Implementation performs correctness tests only; the driver chooses
an engine after measuring coverage, tier costs, K reached, resource stops and race outcomes.
