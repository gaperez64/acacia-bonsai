# Research oracle: source-bound GR(1) portfolio route

This package is retained for differential tests and archived experiments.
The shipped solver entry point is `acacia-bonsai` with native arms.

`python -m acacia_lift.runner --request-mode source` hashes and snapshots the
actual TLSF bytes. It first lowers that snapshot in exact mode and, if the
exact reduction declines, tries a strict reduction for REAL-only lifting. For a concrete
parametric input with unambiguous frontend provenance, it tries online lifting
inside the wrapper's lift slice. It then gives remaining time to the generic
exact direct route. An input without `PARAMETERS` goes directly to the exact
route. Either route returns a verdict only after an independent checker accepts
the source-bound target game and certificate; lifted candidates can return only
REALIZABLE. The wrapper owns the outer budget and Acacia fallback.

The one global rule is `lift-then-direct`. All global route and cost knobs are
declared in `lifting/settings.py`:

| Setting | Default | Effect |
|---|---:|---|
| `MAX_SIZES_PER_AXIS` | 6 | Maximum smaller sizes inspected per parameter axis |
| `MAX_PREDICATE_ARITY` | 4 | Largest invariant/rank schema arity |
| `MAX_SUBSETS_PER_PREDICATE` | 2,000 | Projection and instantiation capacity limit |
| `DISCOVERY_SHARE` | 0.20 | Fraction of the lift slice for discovery and seeds |
| `SEED_CONFIRMATION` | true | Use a third stable size when available |
| `LEARN_MOVE_SCHEMAS` | false | Try exact seed move reconstruction instead of target-derived moves |
| `MOVE_SCHEMA_SECONDS` | 0.05 | Per-goal learned move attempt deadline when enabled |
| `POLICY_PROOF_FRACTION` | 0.75 | Proof time reserved for policy export/check |
| `SOLVER_NODE_CAP`, `SOLVER_CACHE_CAP` | 2²⁵, 2²³ | Direct and seed solver allocation limits |
| `CHECKER_NODE_CAP` | 2²⁶ | Direct and lifted checker node limit |
| `BUDDY_INITIAL_NODES`, `BUDDY_INITIAL_CACHE`, `BUDDY_MAX_INCREASE` | 4,000,000; 400,000; 1,000,000 | BuDDy allocation limits |
| `ROUTE_ORDER` | `lift-then-direct` | Route order recorded in evidence |
| `DEFAULT_LIFT_FRACTION` | 1/3 | Wrapper lift share unless overridden by CLI |
| `DEFAULT_RUNNER_BUDGET_SECONDS`, `DEFAULT_ELIGIBILITY_BUDGET_SECONDS` | 120; 1 | Standalone runner and admission defaults |
| `ELIGIBILITY_CAP_FRACTION` | 0.05 | Maximum admission share of outer cap |
| `CHECKER_TIMEOUT_FLOOR_SECONDS` | 0.001 | Minimum positive checker timeout argument |
| `PROCESS_GROUP_CLEANUP_SECONDS`, `PROCESS_POLL_INTERVAL_SECONDS` | 1; 0.005 | Wrapper child cleanup bound and polling interval |

Seeds vary one
axis at a time with all other parameters at their target values; the target
size is never solved as a seed. Two consecutive sizes must agree structurally;
a third is used for confirmation when available. The route declines lifting
when source origins, roles, bus semantics, rank depths, or exact predicate
reconstructions disagree. By default, each move relation combines the lifted
invariant and rank predicates with the exact target game's transitions. The
policy is Skolemized from those relations. If learned move schemas are enabled,
every move must reconstruct and instantiate exactly within its budget; any
failure declines lifting. Evidence records `move_source` as `target_transition`
or `learned_schema` for a constructed lifted candidate.

The default proof order is policy construction plus certificate checking, then
region checking of the same certificate only on policy capacity or deadline
failure. Evidence records source, game and artifact hashes, override vectors,
per-predicate arities, provenance version, all completed stage timings, and the
checker method. `attempted-declined` labels unresolved attempts; `direct-certified`
and `lifted-certified` appear only after independent verification. The checker
is the sole decision authority for lifted targets.

Historical experiments remain outside the production runner.
