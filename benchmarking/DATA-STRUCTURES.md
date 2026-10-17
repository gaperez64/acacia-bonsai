# Architecture and runtime contracts

This is the living reference for state representations, solver arms and controller
semantics. [Phase records](legacy-phase-records.md) defines observations;
[RESULTS](RESULTS.md) holds measurements and gaps; [decisions](decisions.md) records
admission policy. Exact predecessor documents, including the earlier reviews in
this file, are in `refactor-closeout20261017`.

## Configuration and ownership

The JSON [option registry](../config/acacia-options.json), Meson options and
header fallbacks must agree. The [preset registry](../config/acacia-presets.json)
selects compile-time variants; inspect the current shipping group with:

```sh
python3 scripts/acacia-config.py list-group docker_default
```

K defaults to 2, advances by 3, and stops at 99; CLI K options override these.
Applicable paths always enable decomposition and realizability simplification.
Each fixed-K forward search owns its graph, rank interner, losing antichain and
queues, and discards them before changing K. Long-lived components must own their
state; builders must not retain references to temporary makers or moved objects.

The REAL worker translates ¬φ directly to a Büchi automaton, called the
**negated-specification automaton** (`negated_spec_aut`), and reads it universally
as co-Büchi for φ. Reserve “complement” for an actual complementation of an existing
automaton: parity tier 4 uses `spot::dualize` to complement a captured REAL worker's
automaton; original-formula mode reuses its positive translation.

## Ranks, losing sets and graph identities

The ordinary dense state uses the bare Vector, without a zero-length tail wrapper.
Numeric rank coordinates use signed `-1` for bottom. Boolean-tail coordinates
have domain `{-1,0}`; packed Boolean encoding maps those to 0 and 1. SIMD padding
must be initialized consistently or masked, with element-aligned loads unless
both the base and stride satisfy the required alignment. Byte comparisons must
preserve signed order.

A sparse rank stores sorted `(state, value)` pairs, omits bottom and merges
repeated states by maximum. `x <= y` requires a matching state with at least the
value of every stored pair in x; other states in y are unconstrained. A losing
upset stores minimal generators: reject a rank already above a stored generator,
otherwise remove generators above it, preserve survivor order and append it.
For positive K, sparse values lie in [0,K]; safety requires every value below K.
Accepting increments widen before addition and saturate at K; bottom stays bottom.
Immutable entries own their cached hash and shifted-support mass. Overflow disables
the mass prefilter; it never decides dominance. Dense safe envelopes use K−1 for
numeric coordinates and zero for the Boolean tail. Sparse lazy paths have no global
Boolean-state threshold. Sparse storage saves space only when enough coordinates are absent. Source:
[sparse ranks](../src/solver/sparse_forward_rank.hh) and
[losing antichains](../src/solver/minimal_losing_antichain.hh).

The forward graph has separate environment/controller arrays and numeric IDs.
Environment ranks are hash-interned with full equality checks for collisions.
Controller actions advance lazily until a viable successor is found; reverse
selected-successor dependencies propagate losses. Appends can relocate arrays,
so references and side indexes must use stable IDs.

Membership in the small losing-generator antichain and reporting all live graph
nodes dominated by a generator are different operations. A filter may produce
candidates only; exact pointwise comparison must precede proof/status/dependency
updates. Clear a node's live membership when it loses, preserve deterministic ID
order and charge index storage to existing memory limits. No proposed index or
SIMD replacement is admitted by this reference. The original forward review and
zero-tail study are retained in `refactor-closeout20261017` and
`benchmarking-root-legacy`. Sparse-upset example history is in
`refactor-closeout20261017` and `demand-sparse-20260916`.

## Compact action payloads

One allocation contains a header, sorted nonempty destination/absolute-offset
records and contiguous endpoints in original order. Construction alone uses a
count/cursor array. The action handle, list nodes, picker splices, lexicographic
comparison including empty rows/dimensions, endpoint order and RNG consumption
retain their dense semantics. Equivariance may derive dense tables without
changing those semantics. There are no input-specific ordering rules. Source:
[compact payload](../src/actioners/compact_payload.hh).

The dense oracle in [action_payload_reference.hh](../tests/action_payload_reference.hh)
checks tables, pickers, RNG state, ranks and strategies. Development measurements
are in RESULTS; exact action-layout history is in `refactor-closeout20261017`,
with campaign `legacy20261010-issue-sweep`.

## Sparse guarded oracle

`--oracle-layout grouped|scan` is a runtime selection for search and its independent
verifier; grouped is the default. Equal destination/level guards are grouped and
threshold suffixes computed lazily. Thresholds are true below zero, false above K,
and otherwise OR guards whose unsaturated source value plus increment reaches the
threshold. Accepting destinations supply the increment; Boolean safety caps are zero.
Equality uses `!T(q,0)` at bottom and `T(q,K)` at saturation.

Preimage keys refer to immutable ranks owned by the oracle's collision-safe exact
interner. References span all memo kinds; erase memo entries before releasing their
ranks. No identities or BDD results cross oracles, managers or K attempts. Threshold
entries retain coordinates/levels without retaining targets. Empty buckets release
storage; nonempty interner buckets shrink below one-quarter occupancy. Checked-query
failure clears prepared predicates before destroying the interner.

The structural memo allowance is rank-node ceiling times node-header size plus
choice ceiling times choice-header size. `max_oracle_memo_bytes=0` disables retention.
Account rank/vector capacities, container values, links and buckets; BDD nodes,
prepared edge groups and allocator metadata are outside this estimate. A completed
insertion may transiently exceed the allowance, then clears all predicate memos and
target identities. Prepared groups remain valid, and misses recompute exactly.
Intern targets after threshold computation so eviction cannot invalidate a query.
Publish groups, suffixes and preimages only after checked completion.

Release search predicates, prepared ranks, target identities and bucket arrays
before constructing the independent verifier. Canonical rows and certificate graph
remain live. The verifier rebuilds its own rows, reader, interner and oracle.
Search and verifier counters stay separate; payload estimates are not RSS or
cgroup memory. Original layout history: `refactor-closeout20261017`,
`legacy20261008-screens`, `legacy20261008-closing`.

## Native GR(1) and lifting arms

Every build includes OxiDD and the native frontend. The exact reduction supplies
`both:gr1:oxidd` and the one-sided `real:gr1:oxidd`/`unreal:gr1:oxidd` arms.
`both:gr1-lift:oxidd` tries checked system/environment lifting from shared seeds,
then the exact direct game. `both:gr1-real-lift:oxidd` tries only system lifting;
unreal seeds and declined lifts go directly to the exact solve.
`real:param-lift:oxidd` is the separate parameter-lift arm. These require native
TLSF input and reject explicit synthesis requests. Build-default native members
are omitted for LTL and synthesis. A signalled or exhausted worker supplies no verdict.

The source-bound target context keeps a separate hashed game copy, gives candidate
generation a const view and rechecks its binding before independent verification.
Candidate generation alone is never a target proof. Shared seeds/reductions and one
final check avoid duplicated certification; direct solving remains the fallback.
The R pre-pass is on by default. `--r-prepass off` disables it inside combined arms,
leaving direct budgets unchanged; it does not disable the separate parameter arm.
Direct solving/checking retain 64 MiB artifact caps and OxiDD node/cache caps
2^22/2^20; the construction budget and invocation deadline do not change with R.
Native construction limits remain global (4,096 formula nodes, 2,048 APs, 1,024
conjuncts, 1,024 nodes/conjunct, depth 16, predicted states 2^24, monitor
10,000 states/150,000 edges); RSS is half the bounded invocation limit divided
by active arms. Limits/exhaustion remain inconclusive, and the library owns
structural preparation rather than an extra parent-side BDD precheck.
`--equivariance on|off` controls the compiled backward pre-pass independently;
synthesis excludes it. Worker records retain requested switch states even on bypass.

`TlsfGr1BothOptionsV2` / `tlsf_gr1_both_from_target_v2` borrows the legacy lift
options and observer for the call; it does not take ownership. Null options preserve
v1 defaults. Existing public struct layouts, entry-point ABI and legacy behavior
remain unchanged. With both R and U disabled, skip seed discovery, reduction,
solving, checking and candidate fitting, then use the same prepared target and
direct solver with unchanged budgets. R-off may coexist with U-on for API callers.
Routing API history: `refactor-closeout20261017`, `cov20261006-p1ablation`.

Before threads start, Acacia and tlsf-tools CLIs default an unset `OXIDD_STACK_SIZE`
under finite `RLIMIT_AS` to `min(1 GiB, max(8 MiB, RLIMIT_AS / 16))`. Explicit values
win. Library manager construction and Python imports do not mutate the environment;
Python hosts can set it before starting threads. Meson owns native preparation and
packaging. Retired Python/research tools and source-bound proof procedures are in
`refactor-closeout20261017` and the original `gr1-par2-20260923` (partial),
`gr1-par2-20260923-selection-N`, `gr1-par2-20260923-stagec-scratch` and
`opt20260927-p4close` archives.

## Obligation arm

`both:obligation:spot` is an explicit registry member. It races one original-game
Spot worker when the positive normalized objective satisfies
`is_syntactic_obligation()`, after decision-mode realizability simplification and
before decomposition or UNREAL transformations. Synthesis skips that simplification.
Names, fingerprints and tuned size thresholds do not select eligibility.

An existing legacy child publishes eligibility; native-only portfolios use an
existing native child. A helper-only invocation uses one supervised preparation
child and returns UNKNOWN if ineligible. Launch ordinary arms first and omit the
optional helper from their construction/RSS/thread shares. Ineligible default
invocations launch no helper. The eligible helper re-normalizes/rechecks inside its
own supervised process, inherits the cgroup and is killed/reaped on race loss,
deadline, interruption or parent failure; Linux also uses parent-death SIGKILL.

Decision mode calls Spot once with `realizability=true` and `debug=-1`, retaining
no strategy and performing no recheck. Normal REAL/UNREAL returns have matching
true/false root colors and acceptance. Synthesis calls with `false`, retaining a
REAL strategy; losing synthesis declines to ordinary workers. Null/empty/malformed
returns, exceptions, abnormal exits and mismatched published outcomes never decide.
Allocation failure is exhausted. Typed outcomes are `completed_REAL`,
`completed_UNREAL`, `inapplicable`, `exhausted`, `cancelled`, `error`.

Both decisions and exported REAL controllers trust Spot exactly:
`trust=spot_exact`, `same_library_checked=false`, `independent_checked=false`.
No general Büchi translation or independent check runs in production. Independent
controller/strategy, corruption and timing checks remain offline tests. Historical
same-library/independently-checked policies are preserved in
`refactor-closeout20261017`; evidence: `postmem20261011-p2`, `postmem20261012-closing`,
`obligation-trust20261013`, `otd-A-obligation20261015`.

Registration traverses the formula DAG: inputs first for Mealy, outputs first for
raw Moore, with the remaining block in traversal order. Retain unused declared
signals in the interface. Native TLSF already supplies effective-Mealy lowering
and strict normalization; do not shift or swap again. Decision and synthesis have
no fixed local wall allowance. The invocation acceptance deadline still applies.

`--obligation-memory-share equal|alone` defaults to equal. The helper allowance is
half the smaller of the run limit and 8 GiB (at most 4 GiB); equal divides it by
launched workers, alone gives it all to the helper. The separate 8 GiB address cap
remains. Parent polling can overshoot soft RSS. The default share is unchanged.
The exact [parity arm](coverage-first/parity-arm.md) has its own concise route,
trust, memory and licensing contract.

## Equivariance allowance

`--equivariance on` requires compiled `acacia_enable_equivariant_solver=true`;
otherwise explicit `on` is an error and an omitted switch defaults to off.
Synthesis excludes equivariance even when requested. This compiled-support
contract is retained from `refactor-closeout20261017`, `cov20261006-p1ablation`.

`--equivariance-budget` defaults to absolute `3s`. It applies only to enabled
backward decision workers. `unbounded` retains the unrestricted pre-pass; a
fraction `0 <= F < 1` uses remaining time at first entry or a fixed 5 s reference
without a deadline. Durations accept finite nonnegative `s`/`ms`. Zero immediately
falls back to ordinary backward work. The first entry fixes the allowance; charge
only equivariance calls across decomposed games, including exception cleanup.
Translation and ordinary backward work do not spend it. Exhaustion is STOPPED,
not inapplicability or a proof; noninterruptible calls can overrun a slice.

Positive values must represent at least one nanosecond before truncation; fraction
validation always uses the 5 s reference (minimum `2e-10`, stored fraction below 1).
Reject negative zero, underflow, nonfinite/out-of-range or invalid-unit values with
exit 3 naming the option. Repetition replaces the earlier value. A shorter actual
deadline can still force fallback. Runtime CLI state owns the policy; compact
`equivariance_limit` records disclose mode, absolute and reference nanoseconds.
Archive history: `refactor-closeout20261017`, `cov20261007-eqfull`,
`cov20261007-screens-localrest`.

## Weakening and fallback

`--weakening basic|extended|off` defaults to basic. Extended remains opt-in and uses
the frozen sparse formula-UNREAL route; automaton-UNREAL retains basic. Preserve
requested/effective modes. Basic retains the original safety-core rules: more than
64 immediate children of an immediate G conjunction in a top-level AND, at most
eight singleton obligations in existing order, and the invocation deadline.

Extended starts with the exact parsed objective, retains assumptions/antecedents
and siblings, and weakens only positive conjunction/implication-consequent and G/F
scopes. G may distribute over conjunction; F wraps the whole selected conjunction
and preserves its shared witness. Do not traverse negation, disjunction, X, U,
equivalence or assumptions; strict weak-until and ambiguous outer TLSF scopes decline.
Replay the AST/source derivation. A checked guarded derived-game certificate plus a
source/normalization/partition/timing/game-bound receipt and parent replay proves
original UNREAL. Candidates, exceptions, cancellation and inconclusive results cannot.

At most eight singleton attempts and four AP-support groups of four guarantees run
serially in killable children, reaped before fallback. Each group anchor retains
the first following guarantees sharing any AP, in guarantee order; selection
uses neither pair enumeration nor AP-name ranking. Structural ceilings are
16,384 unique AST nodes, 32,768 edges, 8,192 flattened guarantees, 32,768 AP-support
memberships and 32,768 support visits. Without a deadline defaults are 250 ms per
attempt and 1 s total. `--weakening-attempt-ms`/`--weakening-total-ms` select nonnegative
integer research allowances, zero skips work. With a deadline they are capped by
5%/20% of entry remainder; reserve half the remaining slice for cleanup/replay.
Planning shares that deadline, polling is at most 1 ms, arithmetic saturates and the
original invocation deadline never resets. Inherited memory/work limits remain.
Missing telemetry leaves fallback starvation UNKNOWN; winner cancellation is separate.
History: `refactor-closeout20261017`, `cov20261006-p1ablation`, `cov20261007-screens`.

## Forced-output contradiction proof

The direct checker declines a top-level implication. Consequence collection
descends only through conjunctions and direct G bodies, splitting immediate
`G(A & B)` into global conjuncts. It never searches under disjunction, implication,
negation or temporal binary operators for consequences. Invariants are output-only
Boolean bodies of these global consequences; Boolean disjunction inside a selected
body is allowed. Their conjunction `chi_all=false` proves unsatisfiability directly.

Responses require output-only Boolean `beta` after a finite X chain or F, with
either an unconditional true trigger or a satisfiable bounded input-only trigger.
Supported triggers use input APs, Boolean conjunction/disjunction, literal negation
and finite X offsets; unsupported syntax declines. The environment can force such
a finite trigger. A response proves UNREAL only when `chi_all & beta=false`.
With no invariants, `chi_all=true`, so only `beta=false` can match; an empty
invariant set alone proves nothing. Exact proof review: `refactor-closeout20261017`
(`DATA-STRUCTURES.md`, forced-output checker section).

## Guarded translation

Count distinct F/U/M promises in the unabbreviated negative-normal-form DAG of the
actual worker objective. Only above Spot's `max_accsets()` run language-preserving
`favor_event_univ=true` with the existing syntactic `tls-impl=1` rules. Read the width
from Spot; do not hardcode the installed 64. Keep original alphabets, timing,
translator preference, Büchi target, backend and certificate checker. At/below the
width use the original translator. Diagnostic observation never enables rewriting.

The guard is structural eligibility, not an exact acceptance-allocation prediction
or a verdict. Census counts are prospective, not executed routes or solves. Retain
raw/simplified/component frames and the rule discarding a sole decomposition result.
Isolated per-input census processes avoid accumulated Spot/BDD state. Normalization
or HOA replay is not an end-to-end solver replacement. Broader unconditional
normalization remains unadmitted. Exact census/narrative history:
`refactor-closeout20261017`, `census20261013-isolated`, `legacy20261010-issue-sweep`,
`postmem20261011-p1`, `benchmarking-root-legacy`.

## Controller timing and publication

AIGER evaluates output and next-latch functions from current latches and inputs,
then updates all latches simultaneously. Moore outputs must be independent of the
current inputs in reachable states. Native TLSF performs source-to-effective-Mealy
lowering once. Moore-target export reuses `mealy_to_moore()`, advancing source latches
through the dummy initial round introduced by Moore-source lowering. Logical true
resets use complemented physical latches so emitted AIGER latches start at zero.
Retain the declared IO interface, including unused signals; used-signal order may
follow Spot's strategy. The independent [AIGER oracle](../tests/moore_aiger_oracle.py)
checks final emitted bytes and Moore independence offline. Investigation and master
port history: `refactor-closeout20261017`, `binary-S4-2e10b4fe`.

With `-s`, the first conclusive full-objective verdict enters the parent rendezvous.
Select one worker and suspend competitors before controller construction/export;
ordinary workers retain solved arenas for all decomposed games. Only the selected
REAL worker may open/write/close the destination. Accept REAL only after a successful
close; construction/open/write/close/export failure resumes suspended fallbacks.
UNREAL never opens the destination. Helper-only synthesis retains ordinary fallback
workers. The process supervisor and invocation deadline cover blocked exporters too.

This verdict/controller publication does not roll back destination bytes: ordinary
streams follow symlinks and support FIFOs; failed/interrupted invocations may leave
partial bytes and accept no REAL answer. Synthesis wall time includes selected export
work and retained proofs can affect peak memory; those effects are unmeasured here.
The exact synthesis-owner history is in `refactor-closeout20261017` and
`obligation-trust20261013`.
