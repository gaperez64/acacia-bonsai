# Coverage-directed forward solving and two-arm selection

Closing report for the sprint opened by `acacia_coverage_frontier_sprint_2026-09-12`. It does not replace
[ADAPTIVE-PORTFOLIO-OTFUR-SPRINT.md](ADAPTIVE-PORTFOLIO-OTFUR-SPRINT.md).

**Headline: the sprint did not move coverage.** Four mechanism packages were implemented, verified and measured; none
changed what the solver answers. The one robust coverage effect found anywhere is an *arm-count* effect that belongs to
configuration, not to any new mechanism. Raw data is in the ignored run directories named below.

## A. Run identity and scope

| | |
|---|---|
| candidate | `f7919b83` — master plus this sprint's stack, tree `9b529b7f…`, binary sha256 `0f4f48b6…` |
| baseline | `1557235d`, binary sha256 `5ed5dccf…` |
| configuration | preset `otf_sparse_formula`, release profile (`-Ofast -march=native`, LTO), `acacia_tlsf_corpus_dir` set |
| shipped arms | `real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward` |
| external | ltlsynt (spot) 2.15.1.dev sha256 `ea761a1c…`; SyFCo v1.2.1.2 |
| dataset | SYNTCOMP26 selection, 1,524 instances; parity cohort 1,579 |
| regime | 17 s cap, `MemoryMax=8G`, `MemorySwapMax=0`, **no CPU quota**, strictly sequential |
| kernel | 7.2.5-200.fc44 (the sprint's earlier measurements predate a kernel change and are not compared across it) |
| raw data | `_bm-logs.20260915-closing/` (campaigns, repetitions, gates, three-way, closing diagnostic) |

**Shipping changed during this sprint**: S3 and A1 were merged unconditionally, so the candidate is not a null comparison
against the baseline — the closing run is their regression check. **Arm count and CPU capacity are confounded** under this
regime: a race with more arms also gets more cores.

## B. End-to-end outcomes

| series | solved / 1,524 | REAL | UNREAL | PAR-2 total (s) |
|---|---:|---:|---:|---:|
| ltlsynt | **1,257** | 573 | 684 | 9,517.963 |
| Acacia new, five-arm | 1,175 | 537 | 638 | 12,598.876 |
| Acacia before the sprint | 1,175 | 532 | 643 | 12,641.465 |
| Acacia new, shipped four arms | 1,174 | 533 | 641 | 12,660.179 |
| best fixed pair `rf-ugf` | 1,146 | — | — | — |

PAR-2 above is a corpus **total** (`cactus-report.py` convention). Per-instance means: ltlsynt 6.245 s, candidate 8.307 s,
baseline 8.295 s, five-arm 8.267 s. Figures: [closing/three-way.png](coverage-frontier-20260912/closing/three-way.png),
[closing/three-way-par2.md](coverage-frontier-20260912/closing/three-way-par2.md).

**Candidate vs baseline: no coverage change.** 1 gain, 2 losses, **0 verdict conflicts** over 1,524 instances, all three
decided between 16.8 and 17.0 s against a 17 s cap. Five alternating paired rounds show all three unstable in *both*
binaries — `g-unreal-115` reverses (candidate 5/5, baseline 3/5), `GF-G-contradiction7` is 4/5 vs 5/5,
`workstation_resupply_pb_3_pe_` 3/5 in both. The PAR-2 difference is 0.012 s per instance against a measured 12.1 s noise
floor on this corpus.

## C. Mechanism table

Counters are per worker on the frozen 46-instance cohort unless stated; "on/off" are medians of paired ratios.

| package | what moved | coverage effect |
|---|---|---|
| S1 existential output covering | fires where the structure matches, inert elsewhere | none |
| S2 symbolic losing-input search | one instance repeatably ~8% faster | none |
| S3 incremental coverage | shipped; decisions tested identical | none (B) |
| V1 lean verifier | dependency lists ×0.038, verifier BDD ops ×0.236, verification time ×0.205, verifier cache bytes ×0.016; **search BDD ops ×1.36** | none |
| D1b incremental bad-unions | on identical attempts: search time **×2.85**, BDD ops ×2.70, steps ×3.30, cache bytes ×4.37; cache hit on 2.0% of queries, 0 budget fallbacks | **negative** (robust loss) |
| T1 translation | preference inert (0/46 outcomes differ, 24/24 automata identical); single arm with the whole budget still fails 22/46; transition acceptance smaller but 8% slower; AST hand-off 0.03% | none |

**Four independent mechanism improvements produced no coverage.** D1b's mechanism explains itself: a median 84% of
generators inserted into the losing antichain are later removed as dominated, yet the append-only journal replays all of
them — a median 3,252× the live antichain size.

## D. Family-level frontiers

The only robust gains anywhere are the five instances the five-arm race solves and the shipped four never do:
`SPIPureNext`, `heim-double-x-real`, `ordered-visits-choice-real`, `robot_grid_pb_5_5_pe_`, `thermostat-F-real` — five
different families, solved in 0.02–9 s in **every** repetition round, against one robust near-cap loss
(`workstation_resupply_pb_3_pe_`, 0/5 vs 4/5) and one partial (`sort50`, 3/5 vs 5/5). No parameterised family moved as a
family; no generator produced a run of near-cap gains.

## E. Selection evidence — P1 (pair selection)

| configuration | solved |
|---|---:|
| best fixed pair `rf-ugf` | 1,146 |
| shipped four-arm race (candidate) | 1,174 |
| five-arm race | 1,175 |
| oracle over `rf-ugf` / `rf-ufa` | 1,163 |

The selector was evaluated under a rule fixed **before** any full-corpus labels existed: depth-1 stump, folds grouped by
`family_key`, success iff held-out solves exceed the per-fold best fixed pair by ≥ 2 in total, are not lower in any fold,
and the margin survives stricter directory folds.

| variant | selector | best fixed pair | verdict |
|---|---:|---:|---|
| **primary: depth 1, family folds** | **1,147** | 1,146 | **fails** — +1 < +2, and lower in folds 1 (236 vs 238) and 2 (194 vs 195) |
| check: depth 2, family folds | 1,152 | 1,146 | +6 but lower in fold 1; not the pre-registered model |
| check: depth 1, directory folds | 1,073 | 1,146 | **−73** — the margin does not survive stricter grouping |

Learned rules are single thresholds (`guard_g <= 8.5`, `outputs <= 9.5`). The oracle gap over the two measured pairs is
17 instances. **Arm count beats pair choice**: adding `real:small:spot-guarded-sparse` is worth +3/+4 robustly, while
perfect selection between the two pairs would be worth at most 17 and none of it is learnable from these features.

## F. Per-package decisions

| package | decision |
|---|---|
| S0 failure phases | KEEP RESEARCH TOOLING |
| S1 existential output covering | KEEP DEFAULT-OFF |
| S2 symbolic losing-input search | KEEP DEFAULT-OFF |
| S3 incremental coverage | shipped; coverage-neutral in the closing comparison |
| P1 pair selection | **STOP** on a runtime selector; folds and stump kept as research tooling |
| D1 (variant D1b) | **STOP**; implementation dropped, branch retained as the negative record |
| T1 demand-generated automata | **STOP** on the prototype; keep the attribution evidence |
| V1 lean verifier | KEEP DEFAULT-OFF |

## G. Where coverage is lost

The candidate was rerun over the 350 instances it cannot solve, with worker records: **0 solved again**, identical failure
mix — the misses are reproducible, not noise.

| furthest stage reached (per instance, any of the four arms) | all 350 unsolved | of the 150 ltlsynt solves and Acacia does not |
|---|---:|---:|
| before translation completes | 133 (38.0%) | 44 (29.3%) |
| preprocessing | 131 (37.4%) | 45 (30.0%) |
| **search** | 84 (24.0%) | **59 (39.3%)** |
| verified attempt | 2 (0.6%) | 2 (1.3%) |

ltlsynt solves 150 instances Acacia misses; Acacia solves 67 ltlsynt misses; the union is 1,324. On those 150, ltlsynt takes
a **median of 0.10 s** (113 under 1 s, 8 near the cap), so no constant-factor speedup recovers them.

**Reading.** Translation plus preprocessing is the majority of the gap (59%), but the search is the largest single bucket
(39%), and this sprint's search and verifier work is exactly what did not help. The levers this evidence supports, in order:
build less before searching (decompose conjunctive specifications, or demand-driven construction); find a different search
lever for the 39%; keep arm-count selection; and stop spending on verifier and scan micro-optimisation for coverage.

## H. Gates

| gate | result |
|---|---|
| G0 | PASS — candidate, baseline, posets |
| G1 | PASS — 40/40 sentinels, 0 verdict changes, 0 coverage losses |
| G3 | PASS both panels — syntcomp25 126 vs 125, syntcomp26 140 vs 140, 0 verdict changes, 0 losses |
| G4 | PASS on both builds with identical counts — Ok 575, Fail 0, Timeout 49 |
| G2s | FAIL **on its improvement threshold only** (geomean −0.27%, needs ≥ 5%). No regression: worst target +0.50% against a 6% ceiling. The sprint claims no kernel speedup, so the threshold does not apply. |
| G5 parity | PASS — 1,579 instances, 0 opposite verdicts, 0 frontend errors, 1 converted-only answer |
| G5 spot-check | 50/50 SyFCo pairs regenerate identically, 48/50 formula-AST matches; the two exceptions are the documented deliberate enum-validity divergence and an `ltlfilt` canonicalization exceeding 600 s. Script status FAIL; no new divergence. |

## I. Caveats and missing artifacts

- ltlsynt's times exclude SyFCo conversion, which runs outside the timed scope; Acacia's include its native TLSF frontend.
- TLSF **Strict** semantics has no ltlsynt counterpart; on those instances ltlsynt solves the plain reading.
- Seven `finding_nemo` specifications cannot be converted by SyFCo and are charged SYFCO-FAIL to ltlsynt.
- The full-corpus pair campaign was resumed after a driver fix; its metadata file kept the original run's settings and so
  lacks `error_policy`. The policy is recorded in the campaign log.
- `summarize-worker-phases.py` only accepts the frozen 46-instance cohort, so section G was computed directly from worker
  records. Generalising it is outstanding.
- No external ltlsynt claim is made beyond the table in section B: both tools were measured in the same regime on the same
  machine, but the routes differ as noted above.
