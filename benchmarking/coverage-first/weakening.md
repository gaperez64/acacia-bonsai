# P3: experimental UNREAL weakening

`--weakening incumbent|extended|off` is a runtime switch; `incumbent` remains the default.
No compile-time option or portfolio member/order changed. The extended route uses the existing
frozen-graph sparse formula UNREAL worker; the automaton worker retains the incumbent route.
Requested and effective modes are recorded separately, including early bypasses.

The extended pre-pass starts from the exact parsed objective, before realizability
simplification. In positive conjunction/implication-consequent contexts it retains antecedents
and siblings verbatim, distributes `G` over conjunction, and deletes guarantee conjuncts. Positive `G` frames also expose conjunctions in
`G(A -> AND g_i)` without changing `A`; reconstruction keeps the scope as `G(A -> AND kept_i)`.
Positive `F` frames expose `GF(AND ...)`, `FG(AND ...)`, and `F(AND ...)` by monotonicity;
`F` stays around the entire selected conjunction, preserving its shared future witness.
A replay against the original AST/source context proves the implication. TLSF supplies its
exact linked-frontend normalized formula and original source digest; strict weak-until contexts
are conservatively declined. At the outer TLSF normalization boundary, conjunctions with multiple conditional children decline
rather than choosing a normalization spine that could remove an assumption scope. All original signals, including unused signals, remain registered
in the derived game. Ownership and the incumbent timing adaptation are retained.

Only the existing guarded backend's independently checked winning certificate can create a
proof receipt. The receipt binds the derived objective, runner objective, source/normalization,
partition/timing, derived game, and certificate. The parent checks the receipt and replays the
weakening derivation after reaping the attempt. A candidate Boolean, REAL/inconclusive result,
exception or cancellation cannot transfer a verdict. Certificate telemetry claims the derived
game only; the implication plus the checked derived result proves UNREAL of the original.

Singletons follow the incumbent order. Groups use bounded AP-support neighborhoods: each
anchor retains the first following guarantees sharing any AP, in guarantee order. There is no
pair enumeration or AP-name ranking. Safety-only groups participate on assume-guarantee/plain
conjunction inputs when an earlier contradiction path has not already discharged them.

Global experimental limits are frozen independently of corpus verdicts:

| Limit | Value | Reason |
|---|---:|---|
| Singleton attempts | 8 | Preserve the incumbent allowance/order |
| Concurrent attempt children | 1 | Reclaim each attempt before fallback |
| Additional dependency groups | 4 | Bound search breadth |
| Guarantees per dependency group | 4 | Keep group translation small |
| Unique original AST nodes | 16,384 | Bound planning/replay work |
| Original AST edges | 32,768 | Bound DAG traversal |
| Flattened guarantees | 8,192 | Bound indexing/allocation |
| AP-support memberships | 32,768 | Bound the inverted index |
| Support traversal visits | 32,768 | Bound repeated/shared subformula walks |
| Each attempt's wall allowance | 5% of entry remainder | Bound noninterruptible Spot operations |
| Total extended pre-pass slice | 20% of entry remainder | Reserve the rest for the original solver |
| Child cleanup/replay reserve | Half the currently remaining pre-pass slice | Stop a late attempt early |
| No-deadline allowance | 250 ms/attempt, 1 s total | Prevent unbounded interactive pre-passes |
| Supervisor polling | At most 1 ms, shortened at deadline | Bound cancellation latency |
| Incumbent nested-global threshold | More than 64 children, unchanged | Preserve legacy eligibility |

`--weakening-attempt-ms N` and `--weakening-total-ms N` are absolute research allowances for
`extended`, in nonnegative integer milliseconds. Their no-deadline defaults remain 250/1000;
for example, `--weakening extended --weakening-attempt-ms 2000 --weakening-total-ms 8000`
allows at most 2 s per attempt and 8 s for the entire pre-pass, including planning and replay.
Zero skips candidate work. The options do not affect `incumbent` or `off`.

When an invocation deadline is supplied, omitted options retain the original 5%/20% budgets;
explicit allowances are capped by those same fractions. The cleanup/replay reservation still
caps a child at half the remaining pre-pass slice, so its actual allowance can be smaller.
`weakening_budget_limits` records requested milliseconds, whether each option was supplied,
and absolute no-deadline caps. `weakening_local_budget` and `weakening_attempt_limit` record
actual limits. Arithmetic saturates before adding absolute deadlines. Attempts run serially in killable children in the invocation's
existing process group/resource scope, with Linux parent-death cleanup. No additional memory
cap is introduced; inherited invocation limits and existing guarded-backend work limits apply.
Late child results are rejected; children are reaped before fallback. Planning checks the same
slice deadline. The original invocation deadline is never reset. Telemetry records all these
limits, effective attempt deadlines, child CPU/RSS, retained indices and AST path steps.

The census now reports exhausted-budget fallback starvation even when no attempt started.
A never-started fallback after external interruption is starvation; winner cancellation is
separate. Missing delivery/lifecycle/budget telemetry remains UNKNOWN. Re-reading the supplied
old eligible invocation corrects both UNREAL worker rows to `full_solver_starved=True` without
modifying its archived evidence.

Correctness tests use generated formulas/TLSF, exact deterministic parity games, exact language
emptiness/implication checks, derivation/receipt mutations, renamed inputs, and killed/oversized
attempts followed by the original fallback. This is engineering evidence, not new corpus
coverage. #201 remains open pending fresh standalone and actual-portfolio admission measurements.
The two requested 17 s screens use one optimized binary and the existing coverage runner;
commands and the charter report are under `build_scratch/p3-extended/`.


P3 step 3 read the supplied 17 s, no-deadline screen: 29 eligible inputs, 149 attempts,
136 cancellations, 13 inconclusive attempts and zero proofs/gains. Absolute research allowances
address that observed resource limit without changing defaults. A read-only exact-source
analysis of the 52 `no_guarantee_conjunction` declines found 32 conjunctions under U, 7 under
negation, 7 under GF, 3 under F, 1 under equivalence, and 2 under G/implication consequents.
The last shape and the 10 F contexts are broadened. The rules follow direct monotonicity of
G, F and implication consequents, with unchanged antecedents and replay verification. F is never
distributed over conjunction. The planner never traverses negation, disjunction, X, U, equivalence
or assumptions. Generated language-emptiness and exact-game
checks cover both timing conventions and exact strict normalization. Strict weak-until and
ambiguous outer TLSF scopes still decline. Inside positive G/F scopes, conditional guarantees
are kept/deleted as whole conjuncts rather than interpreted as a normalization spine. The diagnostic with source identities is confined to
`build_scratch/p3-step3/shapes.md`. No new screen or admission measurement ran in this worktree;
#201 remains open and extended weakening remains opt-in.
