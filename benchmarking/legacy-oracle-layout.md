# Sparse guarded oracle layouts

`--oracle-layout scan|grouped` selects between two exact representations in one executable.
`grouped` is the runtime default; `--oracle-layout scan` selects the alternative. This option
applies to sparse guarded search and its independent verifier. There is no compile-time option
for this choice.

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

## 2026-10-08 — grouped becomes the default

The driver admitted `grouped` after paired, rotated, cooled screens of the frozen release
build of `325a90d3` against the master binary on the 28-input legacy panel. The screens ran
standalone formula-UNREAL at 60 s and the shipping race at 17 s and 60 s. They showed no lost
solve, no memory stop and no solved input slower by more than 1 s. Same-K search plus independent-check
cost was 0.54–0.67 times master's on the three primary targets and 0.71 on the secondary
collector control. The reproduced
60 s race PAR-2 change was −14.7 s (−10.8 s for grouped against scan in an earlier session), with far lower peak memory.
These driver-supplied measurements support the default change; `scan` remains explicitly
selectable for comparisons.

The measured frozen release binary's SHA-256 is
`9a774cbf87abd0433803c8a0f5f9e7056eea70c1f50663fca2c2ac3235f41fbc`.
Raw screen rows remain in the ignored `_bm-logs.legacy/p1a2-*` directories and will be archived
at campaign close. The archive reference will follow publication and verification.
The earlier local implementation report,
correctness logs and diagnostic profiles in `build_scratch/legacy-p1a/` describe implementation
validation; the default decision uses the paired screens above.

Follow-up layout comparisons can select either value explicitly
with `--oracle-layout scan` or `--oracle-layout grouped`. Keep the measured frozen executable
and its digest with the original rows; rebuilding this revision produces a different binary.
