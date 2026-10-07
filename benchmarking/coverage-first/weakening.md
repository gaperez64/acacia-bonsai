# P3 step 2: experimental UNREAL weakening

`--weakening incumbent|extended|off` is a runtime switch; `incumbent` remains the default.
No compile-time option or portfolio member/order changed. The extended route uses the existing
frozen-graph sparse formula UNREAL worker; the automaton worker retains the incumbent route.
Requested and effective modes are recorded separately, including early bypasses.

The extended pre-pass starts from the exact parsed objective, before realizability
simplification. In positive conjunction/implication-consequent contexts it retains antecedents
and siblings verbatim, distributes `G` over conjunction, and deletes guarantee conjuncts.
A replay against the original AST/source context proves the implication. TLSF supplies its
exact linked-frontend normalized formula and original source digest; strict weak-until contexts
are conservatively declined. TLSF conjunctions with multiple conditional children also decline
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

The no-deadline allowances are computed from a fixed 5 s reference remainder, solely to derive
the disclosed 250 ms/1 s caps. Attempts run serially in killable children in the invocation's
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
