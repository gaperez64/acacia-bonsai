# A2b: what the scan and the verifier are made of

Sprint step A2b. A1 split three counters that had hidden the answer to two
package decisions: which structure D1 should build, and which verifier cost V1
should attack. This is the first measurement that reads them.

**The broad scan is almost entirely tombstones, and verification is entirely the
losing-proof reconstruction.** D1 builds the incremental bad-predicate cache
(D1b), not the live-node index (D1a). V1 keeps minimal dependency sets and the
verifier cache policy, and drops cone-restricted verification, which failed its
pre-registered gate.

Every median and maximum below was computed twice, independently: by a direct
pass over the worker records, and by `benchmarking/summarize-worker-phases.py`
(report: `_bm-logs.20260915-a2b-profile/summary-check/a2b.md`). The two agree on
every figure. The minimum columns come from the direct pass only, since the
summarizer reports median and maximum.

## Run identity

| | |
|---|---|
| Binary | `otf_sparse_formula` at master `c4e0eb80`, sha256 `8a462deb…`, release profile |
| Source | clean worktree, submodules materialised from their pinned commits |
| Cohort | the frozen 46 instances in `diagnostic-cohort.list` |
| Regime | 17 s cap, `MemoryMax=8G`, `MemorySwapMax=0`, one invocation at a time, no CPU quota |
| Machine | post-reboot, kernel 7.2.5; not timing-comparable with earlier campaigns |
| Raw data | `_bm-logs.20260915-a2b-profile/`, per-worker records under `wrec/` |

These counters describe the **last verified K attempt** of each worker: every
fixed-K attempt writes into the same record, so they are snapshots, not totals
across K.

## The routing is unchanged

Coverage is 11 of 46, the same count as the pre-reboot controls. The sparse
guarded worker's furthest stage splits exactly as S0 measured it: 29 stop before
translation, 2 in preprocessing, 14 reach search and 1 a verified attempt. The
unconditional S3 and A1 changes and the kernel change did not move which
instances reach the game.

Thirteen of the fifteen that reach the game carry the new counters.

## Scan composition

Share of `subsumption_nodes_checked`, n = 13:

| part | median | min | max |
|---|---:|---:|---:|
| tombstones (already losing, skipped) | 97.8% | 49.8% | 99.8% |
| prefilter rejections | 1.8% | 0.1% | 49.9% |
| exact comparisons | 0.5% | 0.0% | 6.9% |

The three parts sum exactly to `subsumption_nodes_checked` on every worker.

The figure that motivated D1a — a median of 2.9 million and a maximum of 101
million node checks per worker — is almost all loop iterations over nodes that
have already lost. A support-posting index narrows the candidates that reach an
exact comparison, which is the 0.5%. **D1a is not built.**

These counters give no time attribution. A tombstone skip is a vector read and a
Boolean test, while the search's BDD operations number about 15 million per
worker in S0; the choice of D1b rests on that difference, and D1b must justify
itself in its own A/B.

## Verifier phase split

Share of `verify_bdd_operations`, n = 13:

| phase | median | min | max |
|---|---:|---:|---:|
| per-choice traversal | 0% | 0% | 0% |
| invariant over the maximal antichain | 0% | 0% | 0% |
| per-proof `bad` reconstruction | **100%** | 100% | 100% |

Phases sum exactly to the cumulative total on every worker. Every one of the 13
last verified attempts was `LOSE_K`, so the winning-certificate phases did no
work. The verification cost S0 measured is entirely `verify_losing_proof`.

## Certificate shape

n = 13, all `LOSE_K`:

| measure | median | min | max |
|---|---:|---:|---:|
| `proofs_total` | 2,668 | 105 | 14,217 |
| `proofs_in_initial_cone / proofs_total` | 1.000 | 0.514 | 1.000 |
| `dependency_list_len_max` | 192 | 6 | 931 |
| `dependency_list_len_sum` | 441,396 | 498 | 1,552,045 |

Each losing-input proof records the entire losing antichain as its dependencies,
and the verifier rebuilds one preimage per dependency. A median certificate
therefore asks the verifier for about 441 thousand reconstructions, and the
largest for 1.55 million. **Minimal dependency sets are justified.**

The initial proof's dependency cone already covers every proof at the median.
Verifying only the cone was pre-registered as conditional on that ratio being
materially below one; it is not, apart from a tail reaching 0.514.
**Cone-restricted verification is dropped.**

## Decisions

| package | decision |
|---|---|
| D1 | build **D1b**, incremental bad-predicate unions; do not build D1a |
| V1 | lever 1, minimal dependency sets: **build** |
| V1 | lever 2, cone-restricted verification: **drop** (median cone ratio 1.000) |
| V1 | lever 3, verifier cache policy: **build** (earlier runs: `verify_preimage_hits == 0`, ~195 MB retained) |

## Caveats

- Thirteen workers from a cohort chosen for difficulty is a small sample. The
  decisions rest on shares that are far from their thresholds (0.5% exact
  comparisons, 100% proof reconstruction, a median cone ratio of exactly 1), not on
  marginal differences.
- Snapshots of the last verified K attempt say nothing about earlier attempts.
  Both V1 and D1b are evaluated end to end in same-revision A/B runs before any
  claim about their effect.
