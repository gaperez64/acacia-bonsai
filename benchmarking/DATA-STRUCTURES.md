# Solver contracts

[Configuration](../docs/configuration.md), [phase records](legacy-phase-records.md)
and the [parity arm](../docs/configuration.md#parity-arms) cover options,
diagnostics and exact-game routing. Earlier reviews are archived in docs-20261122.

## Native GR(1) and lifting

Every build includes OxiDD and the native frontend. The exact reduction supplies
`both:gr1:oxidd`, `real:gr1:oxidd` and `unreal:gr1:oxidd`.
`both:gr1-lift:oxidd` tries checked system/environment lifting from shared seeds,
then direct solving. `both:gr1-real-lift:oxidd` tries system lifting only;
`real:param-lift:oxidd` is separate. These arms require native TLSF and reject
explicit synthesis; default native members are omitted for LTL and synthesis.
Signals and exhaustion supply no verdict.

The source-bound target context owns a separate hashed game copy. Candidate
generation gets a const view; verification rechecks the binding. Candidates alone
prove nothing. Shared seeds/reductions and one final check avoid duplicate checks;
direct solving remains the fallback. R defaults on; `--r-prepass off` disables it
inside combined arms, preserving direct budgets and the separate parameter arm.

Direct solving/checking use 64 MiB artifact caps and OxiDD node/cache caps
2^22/2^20. Native construction limits are 4,096 formula nodes, 2,048 APs, 1,024
conjuncts, 1,024 nodes per conjunct, depth 16, predicted states 2^24 and monitor
limits 10,000 states/150,000 edges. RSS is half the bounded invocation limit divided
by active arms. R does not change construction or direct budgets. Limits are
inconclusive; the library owns preparation.

`TlsfGr1BothOptionsV2` / `tlsf_gr1_both_from_target_v2` borrows lift options and
observer for the call. Null options preserve v1 defaults and existing ABI layouts.
Disabling R and U skips seed discovery, reduction, seed solving/checking and
candidate fitting, then uses the same prepared target and direct budgets. API
callers can disable R while keeping U.

Before starting threads, Acacia and tlsf-tools CLIs set an absent
`OXIDD_STACK_SIZE` under finite `RLIMIT_AS` to
`min(1 GiB, max(8 MiB, RLIMIT_AS / 16))`. Explicit settings win. Library manager
construction and Python imports leave the environment alone; Python hosts can set
it before starting threads.

## Obligation arm

`both:obligation:spot` runs Spot on the original normalized positive objective when
`is_syntactic_obligation()` accepts it, after decision-mode realizability
simplification and before decomposition or UNREAL transformations. Synthesis skips
that simplification. Names, fingerprints and tuned size thresholds do not select
eligibility.

A legacy child publishes eligibility, or an existing native child does so for a
native-only portfolio. Helper-only calls use one supervised preparation child and
return UNKNOWN if ineligible. Launch ordinary arms first; exclude the optional
helper from their construction/RSS/thread shares. Ineligible defaults launch no
helper. The helper rechecks eligibility, inherits the cgroup and is killed/reaped
on race loss, deadline, interruption or parent failure (parent-death SIGKILL on
Linux).

Decision mode calls Spot once with `realizability=true`, `debug=-1`, retains no
strategy and performs no recheck. Completed REAL/UNREAL returns require matching
root colors and acceptance. Synthesis calls with `false` and retains a REAL
strategy; a losing synthesis attempt declines to ordinary workers. Malformed
returns, exceptions, abnormal exits or mismatched outcomes cannot decide;
allocation failure is exhausted. Outcomes are `completed_REAL`, `completed_UNREAL`,
`inapplicable`, `exhausted`, `cancelled`, `error`.

Production trusts decisions and exported REAL controllers: `trust=spot_exact`,
`same_library_checked=false`, `independent_checked=false`. Independent strategy,
controller, corruption and timing checks run in tests.

Registration walks the formula DAG, inputs first for Mealy and outputs first for
raw Moore, then the remaining block in traversal order. Keep unused declared
signals. Native TLSF already applies effective-Mealy lowering and strict
normalization; do not repeat them. Only the invocation deadline limits wall time.

`--obligation-memory-share equal|alone` defaults to equal. The helper receives half
the smaller of run limit and 8 GiB, divided by launched workers for equal or all
assigned to the helper for alone. A separate 8 GiB address cap remains. Parent
polling can overshoot soft RSS.

## Equivariance

Explicit `--equivariance on` requires compiled
`acacia_enable_equivariant_solver=true`; otherwise it is an error. Without compiled
support the omitted switch is off. Synthesis excludes this prepass. Records retain
requested switch states even on bypass.

`--equivariance-budget` defaults to absolute `3s` for enabled backward decision
workers. `unbounded` removes the allowance. A fraction `0 <= F < 1` uses time
remaining at first entry, or a fixed 5 s reference without a deadline. Finite
nonnegative durations accept `s`/`ms`; zero falls back immediately. First entry
fixes the allowance across decomposed games. Only equivariance calls spend it,
including exception cleanup. Exhaustion is STOPPED; noninterruptible calls may
overrun.

Positive values must represent at least one nanosecond before truncation. Fraction
validation uses the 5 s reference (minimum `2e-10`, stored fraction below 1).
Reject negative zero, underflow, nonfinite/out-of-range values and invalid units
with exit 3 naming the option. Repetition replaces the value; the invocation
deadline can shorten it. `equivariance_limit` records mode, absolute and reference
nanoseconds.

## Weakening and contradiction checks

`--weakening basic|off` defaults to basic. Basic requires more than 64 immediate
children of an immediate G conjunction in a top-level AND. It tries at most eight
singleton obligations in existing order within the invocation deadline. Each
candidate follows logically from the simplified original; only a proved candidate
UNREAL transfers to original UNREAL. Otherwise solve the full objective. Synthesis
skips weakening. Extended weakening and its allowance flags were retired in #403.

`check-weakening-games.py` compares basic/off and implicit defaults with Spot's
exact parity oracle on generated LTL/TLSF, strict semantics and renamings.
`unreal_safety_core_witnesses_test.cc` checks threshold, order and implication.

The forced-output checker declines top-level implications. It collects consequences
only through conjunctions and direct G bodies, splitting immediate `G(A & B)`.
Global output-only Boolean invariants form `chi_all`; `chi_all=false` proves
unsatisfiability. Responses need output-only Boolean `beta` after finite X or F,
and a true or satisfiable bounded input-only trigger. Triggers support Boolean
conjunction/disjunction, literal negation and finite X offsets. Unsupported syntax
declines. The environment can force the trigger; `chi_all & beta=false` proves
UNREAL. Without invariants, `chi_all=true`, so only `beta=false` matches.

## Guarded translation

Count distinct F/U/M promises in the actual worker objective's unabbreviated
negative-normal-form DAG. Above Spot's `max_accsets()`, apply language-preserving
`favor_event_univ=true` with existing `tls-impl=1` rules. At or below that width,
use the original translator. Preserve alphabets, timing, translation preference,
Büchi target, backend and checker. Observation never enables rewriting.

Eligibility predicts neither allocated acceptance sets nor a verdict. Censuses
keep raw, simplified and component frames, discard a sole decomposition result,
and isolate inputs in separate processes. HOA replay does not replace an
end-to-end solve; unconditional normalization is unadmitted.

## Controller timing and publication

AIGER computes outputs and next latches from current latches and inputs, then
updates all latches together. Moore outputs must be independent of current inputs
in reachable states. TLSF lowering occurs once; Moore-target export uses
`mealy_to_moore()` and advances source latches through the dummy initial round.
Complemented physical latches encode logical true resets while AIGER starts at
zero. Preserve unused declared signals; used-signal order can follow Spot's
strategy. The independent [AIGER oracle](../tests/moore_aiger_oracle.py) checks
emitted bytes and Moore independence.

With `-s`, the first conclusive full-objective verdict enters the parent
rendezvous. Suspend competitors before the selected worker constructs/exports.
Ordinary workers retain solved arenas for all decomposed games. Only the selected
REAL worker may open/write/close the destination; accept REAL only after close
succeeds. Export failure resumes fallbacks. UNREAL never opens the destination;
helper-only synthesis retains ordinary fallbacks. Supervision and the invocation
deadline also cover blocked exporters.

Streams follow symlinks and support FIFOs. Failure or interruption can leave
partial destination bytes without an accepted REAL result. Synthesis wall time
includes export; retained proofs can affect peak memory.
