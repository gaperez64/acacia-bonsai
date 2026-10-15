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
`--parity-buchi-engine spot-game|antichain|strpm-simd|gr1`. The default, `spot-game`, preserves
the existing deterministic forbidden-Buchi fast path: completion, a co-Buchi game, and Spot's `solve_game`.
Production requests only the verdict and, for synthesis, the shortcut's deferred controller.
Verification and game/solution observers retain the full winning region and solved arena via
`solve_parity_game`, preserving the research tool's diagnostic output.
`antichain` instead enters the ordinary worker's K-bounded solver after the Spot shortcut,
using the build's configured automaton preprocessing, Boolean-state discovery, downsets,
backend defaults and K schedule (`-M`, `-K`, `-I`). Synthesis uses the ordinary backward
fallback and deferred strategy builder.
`strpm-simd` builds the same completed co-Buchi game as `spot-game`, including the rejecting
sink, declared synthesis outputs, and two-step split in the requested Mealy/Moore frame.
It then uses tier 4's explicit `parity::arena_game` conversion and `parity::solve_strpm`,
with the same adaptive `(k,t)` bounds and Spot fallback on bound exhaustion. The solution
is installed on the split arena before the ordinary controller construction and AIG export.
Completed decisions therefore use Acacia's parity solver unless the recorded fallback occurs.
The antichain K options (`-M`, `-K`, `-I`) do not set the parity solver's bounds.
`gr1` instead encodes the same classified forbidden DBA, completed with a rejecting
sink, as an AbsSynthe AIGER game. State numbers are binary codes XORed with the
initial state, so every latch starts at zero. Edge-label BDDs become shared AIG
cones for the transition function. A separate latch samples accepting transitions,
which preserves infinite acceptance even when the DBA is transition-based.
Uncontrollable and controllable inputs represent specification inputs and outputs;
private positional names avoid the AbsSynthe `controllable_` prefix colliding with
user signal names. Export restores the complete original partition, including unused
and indexed signals, and preserves the solver's state and goal-counter latches.

Its one fairness assumption is `accepting`; its one singleton justice goal is
constant false. Thus the system objective is `GF accepting -> GF false`, exactly
`FG !accepting`. An empty justice record would instead mean true and is not used.
Both `tlsf_gr1_validate_game` and `solve_gr1_oxidd` accept this profile unchanged.
One solve decides both polarities: a returned strategy means REAL, `*unreal=1`
means UNREAL, and any internal failure means UNKNOWN. No opposite-polarity search,
Spot game solve, local allowance or benchmark-based dispatch is used.

AbsSynthe's native frame is Mealy. For native Moore, pending-output latches hold the
previous round's controllables and a started latch skips the first DBA transition.
The dummy first round commits the initial output; each later transition consumes
that committed output with the new environment input. A Moore strategy extends by
ignoring the dummy input. Conversely, fixing the dummy input to zero produces a
Moore strategy from any winning Mealy strategy of the encoded game. This finite
prefix cannot affect GF/FG acceptance. The existing `mealy_to_moore(..., true)`
conversion advances all controller latches through the fixed dummy round and delays
the committed outputs, giving exact initial outputs and output-before-input timing.
TLSF continues to use the frontend's effective-Mealy lowering once and its existing
target conversion.

The GR(1) solver returns its strategy AIG during solving, including in decision-only
calls; decision calls immediately release it. Synthesis retains it through the
rendezvous and only a selected REAL worker converts it to Spot's AIG representation
and uses the shared controller writer. No file is opened by an unselected or UNREAL
worker. There are no tlsf-tools source/API changes. All CLI builds now link the
existing OxiDD GR(1) library, including base builds without native TLSF arms.
No compile-time option is added; the default remains `spot-game`.

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
checks all four tier-1–3 engines on small REAL/UNREAL Mealy/Moore games, checks exported AIGs
against a fresh negated-specification translation and Moore output independence, and retains
the existing scalar oracle and 480-game Spot comparison. A further 480 generated
DBA games compare the GR(1) route directly with Spot's `solve_game` in both timing
frames, including partial transitions, nonzero initial states, state and transition
acceptance, empty partitions and signal names starting with `controllable_`.
Winning random-game controllers are checked against the forbidden DBA; formula and
TLSF controllers are checked against fresh negated-specification translations. Tier-3 tests also force the Spot
fallback with the existing operation budget and verify its installed strategies and controllers.

Except for the strategy AIG returned inside the GR(1) solve, decision-only calls do not
construct or encode controllers. During synthesis, both verdicts
enter the existing parent rendezvous; only a selected REAL worker exports its controller
and uses the ordinary controller writer. UNREAL never opens the destination. A failed export
resumes the suspended competitors. Parent death, interruption, race cancellation and the outer
invocation deadline use the existing process-group supervisor. The solver receives no local
wall allowance or deadline. The new arm uses the obligation helper's address-space cap and
cgroup-aware soft RSS share (half the available cap divided by scheduled workers). Its resource
stop is never accepted as a verdict.

Worker records include translate/orient/classify, tier, ACD/split/explicit game construction,
solve, parameter changes, fallback boundaries and trust declarations. Every parity event identifies
the selected tier engine in `route` and `effective_backend`: `spot-fast`, `antichain`, `strpm-simd`, or `gr1`. The GR(1) worker route is also `gr1`;
its stages are `gr1_encode`, `gr1_solve`, `gr1_export`, and `parity_export`. Solve-stage
metrics report `bdd_nodes_at_export` and `bdd_nodes_before_teardown`. The API does not
provide a peak, so these samples are not labeled as peaks. GR(1) uses zero `k`, `t`
and `pairs`, with no fallback. Antichain search events retain each K reached;
`parity_parameters`, `parity_solved` and `parity_verdict` report the winning K (`t=0`, `pairs=0`).
Its trust declaration is `exact_antichain`; game engines retain `exact_game`. For `strpm-simd`
in every tier, `parity_tier`, `parity_solved` and `parity_verdict` include the tier, engine,
`k`, `t`, `pairs`, `fallback` and `fallback_reason`. The classification event precedes solving
and starts with zero bounds and no fallback; `parity_verdict` carries the completed adaptive
`k`, `t`, `pairs`, `fallback` and `fallback_reason` from `parity_solved`, including fallback outcomes. Cancellation
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
The same runtime comparison can now include `--parity-buchi-engine strpm-simd`; its parameter
and fallback records distinguish Acacia parity decisions from the Spot fallback. No measurements
or default changes accompany this extension.
