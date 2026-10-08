# Sparse guarded oracle layouts

`--oracle-layout scan|grouped` compares two exact representations in one executable. `scan`
remains the runtime default. This option applies to sparse guarded search and its independent
verifier; it does not change worker membership, translation, K scheduling, or the dense oracle's
aggregate cache. There is no new compile-time option.

The grouped layout combines guards at equal destination/level once, stores destinations and
levels in sorted vectors, and memoizes only requested threshold suffixes. Thresholds are true
for h <= -1, false for h > K, and otherwise OR active-source edge guards whose unsaturated
source value plus increment reaches h. Frozen increments still come from accepting destinations.
Equality uses !T(q,0) at bottom and T(q,K) at saturation. Boolean safety caps remain zero.

Preimage keys name immutable rank nodes in an oracle-owned exact interner. Full rank equality
resolves hash collisions; node addresses survive rehash and never refer to caller-owned ranks.
Reference counts span every source and preimage kind. Eviction erases each memo entry before
releasing its reference and reclaims a rank when its last reference disappears. Threshold entries
name only coordinates and levels, so they remain warm without retaining targets. Empty memo and
interner bucket arrays are released; nonempty interner buckets shrink below one-quarter occupancy,
a global storage policy based only on live entries. Failed checked queries clear prepared
predicates before destroying the interner. Identities and BDD answers never cross oracles, K
attempts, or managers. The verifier reconstructs its reader, rows, rank interner and oracle
independently. After recording search counters, both layouts release all search-oracle prepared
ranks, predicates, target identities and bucket arrays before constructing the verifier. The
canonical row store and certificate graph remain live for independent replay and source binding.
Complete groups, suffixes and preimages publish only after checked work; resource exhaustion
remains inconclusive. The optional aggregate-cache experiment is deferred.

Both layouts have the same memo byte budget for preimages, thresholds and target identities.
The default is the configured rank-node ceiling times the guarded node header size, plus the
choice ceiling times the guarded choice header size. This is a global structural allowance,
independent of input identity. Internal callers may override `Limits::max_oracle_memo_bytes`;
zero disables retention. Accounting includes rank vector capacity, memo vector capacity,
container values, node links and allocated hash buckets. BDD manager nodes, prepared edge groups
and allocator metadata are outside this estimate.

A completed memo insertion that exceeds the allowance clears all predicate memos and target
identities, including threshold vector capacity and hash buckets. Prepared groups remain stable
so a query can continue safely; every cache miss recomputes the exact predicate. One insertion
(including any container growth) may transiently exceed the allowance. Target identities are
interned after threshold computation, so an eviction cannot invalidate an in-progress preimage.
The existing reporter exposes `search_` and `verify_` versions of `cache_evictions`,
`oracle_memo_bytes_estimate`, `peak_oracle_memo_bytes_estimate` and `oracle_memo_budget_bytes`.
Search retained-cache observations describe search completion, before release; the existing
`retained_search_payload_bytes_estimate` describes what remains at verification entry.

Existing threshold/preimage call, miss and hit counters remain available. Additional observed
fields count prepared edges/levels, threshold levels visited, target interner misses, per-call
lookup-key entries copied, and retained memo sizes. `interned_targets` counts currently referenced
identities; `cache_rank_bytes` includes their owned ranks, and `oracle_payload_bytes_estimate`
includes reference counts and retained bucket arrays. Payload estimates omit allocator/node
headers and BDD ownership; they are not process or cgroup memory measurements.

Generated tests compare both layouts with independently enumerated arithmetic and an explicit
fixed-K game, including bottom/saturation, absent coordinates, parallel accepting/nonaccepting
edges, Boolean caps, empty AP sets, real rank-hash collisions, target reassignment, lazy suffixes,
shared identities across source/kind memos, partial and complete eviction, long generated churn,
all checked-query resource checkpoints and independent corrupted-certificate rejection. The
existing provider replay suites run both layouts in all five compiled test configurations.

The implementation is an unadmitted experiment. Short diagnostic profiles show less level
scanning and copied-key/tree traffic, including independent verification. They do not establish
shipping coverage, PAR-2, or the cgroup memory gate. Ordinary rotated paired screens must decide
admission; keep `scan` as default until that decision.

The local implementation report, dependency and binary hashes, correctness logs, profile command
manifests, captures and raw perf data are in `build_scratch/legacy-p1a/`. These local diagnostics
are not an archived campaign or an evidence-index admission row.

Run the following commands **outside the sandbox**, with no other campaign running. They use the
existing runner, 8 GiB/no swap, captured worker records, rotated treatments and an external cap.
No invocation deadline is supplied. The binary remains the uncommitted implementation on base
`54ddfb83`; the runner additionally records its SHA-256 (currently
`9bd189cabc4f46a44ae0a2231412b127cd88eb466223b35023a73156e2d30ba0`).

```bash
R=/home/gperez/GIT-repos/acacia-bonsai
PY="$R/.venv/bin/python3"
SCREEN="$R/_bm-logs.cov-20261006/screen.py"
BIN="$R/build_scratch/cov/wt-legacy-p1a/build_scratch/legacy-p1a/release/src/acacia-bonsai"
LIST="$R/_bm-logs.legacy/panel.list"
unset ACACIA_OUTER_DEADLINE_MONOTONIC
export SCREEN_CAPTURE=1

STANDALONE="[[\"scan\",\"$BIN\",\"54ddfb83+uncommitted\",\"--arms unreal:formula:spot-guarded-sparse --oracle-layout scan\"],[\"grouped\",\"$BIN\",\"54ddfb83+uncommitted\",\"--arms unreal:formula:spot-guarded-sparse --oracle-layout grouped\"]]"
RACE="[[\"scan\",\"$BIN\",\"54ddfb83+uncommitted\",\"--oracle-layout scan\"],[\"grouped\",\"$BIN\",\"54ddfb83+uncommitted\",\"--oracle-layout grouped\"]]"

"$PY" "$SCREEN" "$R/_bm-logs.legacy/p1a-standalone60" "$LIST" 60 "$STANDALONE"
"$PY" "$SCREEN" "$R/_bm-logs.legacy/p1a-race17" "$LIST" 17 "$RACE"
"$PY" "$SCREEN" "$R/_bm-logs.legacy/p1a-race60" "$LIST" 60 "$RACE"
```

Keep default R, disabled U, the 3-second equivariance allowance, native worker, weakening and
worker order intact. Investigate every loss with cooled alternating repetitions. Admission needs
exact outcomes, complete memory observations and coverage or PAR-2 improvement beyond local
noise in the actual race. Profiling and cache byte estimates do not replace those gates.
