# Package decisions — demand-sparse sprint

This is the documentation checkpoint while **P4 is running**; it is not experimental closure under plan §9.6. [manifest.json](manifest.json) and the original [reuse.md](reuse.md) preserve the P0 inventory, not a final candidate identity. Later measured binaries are recorded in [bin/freeze-manifest.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/bin/freeze-manifest.tsv) (SHA-256 `4a2ed2fb2da4306401701fa590b54f554f4333bcd835349c5713ace826291796`), with resolved `bin/*.options.txt`; the candidate listed there is `cand-89d67bcd`. Hashes for every small summary, driver and review cited below are collected in [evidence-sha256.tsv](evidence-sha256.tsv). Research-branch placement and P1a/P3a contamination are also recorded in the [sprint handoff](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/brief-docs1.txt).

**Admission rule:** [plan §8](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/plan.md) requires correctness **and** no validated regression **and** useful benefit. Coverage is a set requirement: no lost solve can be purchased with gains elsewhere. The default benefit is 5/5 versus 0/5 correct solves at 17 s; runtime requires ≥5% and ≥50 ms median reduction on an affected instance (or ≥5% family geometric-mean reduction), beyond control noise and supported in ≥4/5 pairs; memory requires ≥15% and ≥16 MiB less actual whole-invocation peak memory. Five alternating pairs confirm selected changes. An unresolved near-cap loss blocks admission. §8.4 requires a separate combined-candidate check and permits measurement/reporting repairs without a speedup claim.

## P0 — retained

**Intended work:** repair stale/missing worker boundaries, reset per-attempt counters, distinguish cumulative ended work, fallback segments and observed returns; reuse the existing sink and consumers. This removes measurement ambiguity, not solver work.

**Measured:** one 46-instance / 184-worker diagnostic and five profiling twins on `4a04e882`, condensed in [phases.md](phases.md) and [profile.md](profile.md), from [p0-diag/analysis](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis) and [scripts/p0-diag.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/p0-diag.sh). Timers sum ended attempts; open checker time is censored. The initial [P0 verdict](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p0-review-verdict.txt) was FIX-REQUIRED for missing Spot-fast/equivariant boundaries; the final [P0 repair review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p0fix-review.log) is PASS, with all **24 mutations caught**, **48 unit tests passed**, and diagnostics-off allocation probe reporting **zero allocations**. These are prior review results, not newly executed tests.

**Benefit/regression/disposition:** correct lifecycle attribution is demonstrated; no end-to-end speedup or peak-memory benefit is claimed. The saved review reports no remaining defects; available Python checks passed with missing Spot-dependent coverage disclosed. **Retained as an essential measurement repair under §8.4**, not admitted as a performance optimization.

## P1a — admitted-pending-combined-check

**Intended removed work:** skip repeated losing-certificate checks only when scheduling the next K, using a separate scheduling-hint result. Exact APIs and decisive winning verification remain mandatory; hints cannot justify a final opposite verdict.

**Measured:** P0's ended-attempt loss checks are substantial: e.g. F-G-contradiction-110 **5,555.593 ms / 6,962.040 ms exploration**, GF-G-contradiction7 **3,955.668 / 5,861.470 ms**; three targets were still in losing verification at cap. These are baseline mechanism measurements, not measured treatment savings. The five-pair, ten-target flag screen uses **the same `p1a-d14cf955` binary on both sides**, control default `verify-all`, treatment **`--loss-check-policy scheduling-hint`**. See [screens/p1a/screen-manifest.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p1a/screen-manifest.tsv) (SHA-256 `6af9cd6178d73ced911d0868bdd395634cc33c29b9fb76be81892aad0c73277c`), [screens/p1a/admission.md](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p1a/admission.md) (SHA-256 `72b5168c6223afbf817e5742f22a298e6542f9c103aeedec32bb8f763e2ce582`) and [admission.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p1a/admission.tsv); drivers [seq-screens1.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-screens1.sh) / [screen.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/screen.sh). The timing screen has no paired cumulative worker-cost attribution; no amount of saved verifier time is invented.

**End-to-end benefit:** F-G-contradiction-110 **0/5 → 5/5**, satisfying §8.2 coverage. GF-G-contradiction7 improves **3/5 → 5/5**; workstation **0/5 → 2/5** is not an independently confirmed coverage benefit. The evaluator reports ADMIT and **no validated regression under the stated tests and measurement resolution**. No qualifying common-solved runtime or measured scope-memory benefit is established.

**Correctness/disposition:** initial [review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p1a-review-verdict.txt) required an omitted-option regression test; [repair review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p1afix-review.log) is PASS, with all five mutations rejected, including a default-policy change. **The screen binary still contained P3a.** Its valid flag comparison does not establish retention on the P3a-free landing candidate. Status is **admitted-pending-combined-check**, as required by §8.4. Deployed mode in this sprint is the runtime flag only; **no preset or registry change**. Do not infer combined results by adding individual gains.

## P1b — not attempted

**Intended removed work:** keep immutable frozen-provider rows across K; lazy-provider retention already exists. K-dependent search/proof state would remain fresh.

**Measured:** all 13 historical multi-K cases repeat K starts, but ended-attempt row generation is at most **3.670 ms per target**; repeated-row counts are usually bounds. Evidence: [phases.md](phases.md), [P1b selection in targets.md](targets.md#p1b-selection-not-executed), and [p0-diag/analysis/SUMMARY.txt](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/SUMMARY.txt) (SHA-256 `a3dfb2f33e19f8e69f0a4c6a58874612f707f98b1b63bc23325cb8547902c255`).

**Benefit/regression/disposition:** even eliminating all that measured generation cannot reach the **50 ms** single-instance runtime floor in §8.2. Interrupted work and unmeasured construction costs are not a proven whole-run upper bound; no family-runtime or actual-memory benefit is established either. No treatment, end-to-end comparison or regression test of a P1b implementation exists. **Not attempted**; no redundant lazy cache was built.

## P2 — blocked

**Intended removed work:** avoid complete optimized Spot translation/preprocessing before useful sparse search by constructing one fixed-acceptance closure/cursor provider and publishing complete rows on demand.

**Measured:** [p2-provider-report.md](p2-provider-report.md) / [data](p2-provider-data.tsv) compare A/B/C over 22 frozen targets, preserving 23 B and 23 C worker records, cumulative counters, missing measurements and shared-binary caveats. **19/22 targets complete no first row** in both closure modes; branch counts commonly approach the **2,000,000 per-row limit**. Only CheckAlarm and OneCounter solve. P1 hints are off; P3a was inherited, so the original P3a-off isolation condition was not met. Drivers: [diag-p2mech.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-p2mech.sh), [diag-p2eager.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-p2eager.sh), [seq-p2explore.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-p2explore.sh).

**End-to-end/regression:** no confirmed useful provider benefit. One exploratory portfolio round gains 06/07 but loses GF-G-contradiction7; diagnostics attribute 06/07 to the closure unreal arm's early inconclusive exit, then forward-real winning at K=2, not a closure solve. No five-pair admission or scope-memory comparison is available. Core/integration reviews PASS with lasso, Spot equivalence, finite-K and mutation evidence as detailed in the report.

**Disposition:** **blocked by first-row explosion; not integrated**. Plan §5.9 explicitly requires stopping here rather than redesigning a partial-row oracle, and §8 requires actual deployed-mode benefit without validated losses. Preserve code/tests on `research/ds-p2-closure-buchi` at `fd6d5a16` (core `c1f60fff`, measured integration `64b961f0`). Solo-real-Spot results are diagnostic inputs to P4 only.

## P3a — rejected

**Intended removed work:** move distinct-slot LossSet survivors instead of deep-copying rank payloads, preserving order/proof alignment. P0 insert samples motivate a small screen but do not measure cumulative survivor-copy cost or time saved.

**Measured:** five pairs on six targets, `p0-4a04e882` versus `p3a-52d4c8e5`: [screens/p3a/screen-manifest.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p3a/screen-manifest.tsv) (SHA-256 `6d5be3e80928a77b5672d6155cb44555c65e70ef39b822ac8fa4a73f80ecb2ee`), [screens/p3a/admission.md](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p3a/admission.md) (SHA-256 `33ae59b3ff4f9cb476bea01067308e742a5cf617872d4c6f5dd4cd6675c8f332`), [admission.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p3a/admission.tsv). GF-G-contradiction7 is **5/5 → 0/5**, an unequivocal regression under §8.3. Workstation's **0/5 → 1/5** is no qualifying benefit. The evaluator reports REJECT, no-regression FAIL, improvement FAIL. The [code review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p3a-review.log) is PASS, with compaction/mutation and ASan/UBSan checks; code correctness does not imply performance admission.

**Longer-cap diagnostic:** [diag-gfg7](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-gfg7), produced by [scripts/diag-gfg7.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-gfg7.sh), uses **51 s**, never 17 s coverage. Two P0 walls are **17.219479/17.295967 s**; P3a walls are **17.575652/17.338676 s**. All four return UNREALIZABLE at **K=11**, with **5/5 started/completed attempts** including the pre-K attempt, and verified-win evidence. The same verdict/attempt progression around **17.22–17.58 s** supports a knife-edge near-cap timing shift, not a solver-behaviour change. It does not establish the cause of the timing shift or invalidate the failed 17 s screen.

**Disposition:** **rejected** under §8's no-regression and improvement gates. Code remains on `research/ds-p3a-move-compaction` (`52d4c8e5`); it is excluded from the landing candidate. No cumulative copy-time, actual-memory or end-to-end benefit is claimed.

## P3b — not attempted

**Intended removed work:** replace broad stable-node scans through tombstones with an ordered dense live-ID list.

**Measured:** [profile.md](profile.md) gives broadcast-upper self counts **76/3379 (2.25%), 20/2219 (0.90%), 48/2543 (1.89%), 1/312 (0.32%), 5/2150 (0.23%)**. Every sparse-exploration denominator meets n≥300, but none reaches the **10%** work-selection gate. Historical tombstone ratios are counts, not CPU shares. The alternative replay-benefit route is unmeasured.

**Benefit/regression/disposition:** no implementation, treatment comparison or demonstrated benefit; no treatment regression result. **Not attempted** because the measured gate did not fire. No live-ID structure or new replay was built. Passing a work-selection gate would still not replace §8 admission.

## P3c — not attempted

**Intended removed work:** reject LossSet candidates via contiguous scalar/SIMD metadata before the exact comparator, preserving first proof witness.

**Measured:** LossSet self shares **76/3379 (2.25%), 47/2219 (2.12%), 41/2543 (1.61%), 4/312 (1.28%), 102/2150 (4.74%)** are all below the **10%** CPU gate. The additional **material share of queries with ≥128 live generators is unevaluated**; peak counts do not measure that distribution. Source and intervals: [profile.md](profile.md).

**Benefit/regression/disposition:** no implementation, solver benefit or treatment regression result. **Not attempted**; the CPU gate did not fire and the full conjunction is not established. No SIMD backend/metadata sidecar or new trace campaign was justified; §8 remains required for any future proposal.

## P3d — not attempted

**Intended removed work:** intern duplicate immutable rank payloads in an attempt-local arena to reduce allocation/copying and retained bytes.

**Measured:** payload-allocation/copy shares **111/3379 (3.28%), 66/2219 (2.97%), 53/2543 (2.08%), 10/312 (3.21%), 116/2150 (5.40%)**, below **15%**. Even the generous all-allocation sensitivity reaches at most **11.86%**. The alternative **duplication ≥2 and ≥25% attributable retained native storage** is **unevaluated**; logical capacity estimates supply neither denominator nor actual peak memory. Source: [profile.md](profile.md).

**Benefit/regression/disposition:** no arena implemented, actual-memory benefit or treatment regression comparison. **Not attempted**: the sampled CPU gate did not fire; the unmeasured alternate route is not a measured failure. No storage census or speculative arena was added. §8 would reject a payload-only improvement without useful whole-solver benefit.

## P4 — PENDING — screen running

**Intended removed work/configuration:** replace `real:small:forward` with `real:small:spot-guarded-sparse` in the existing four-arm portfolio, preserving `real:small:backward` and both unreal arms. This tests useful coverage under the same arm count, not a virtual-best union.

**Evidence available:** [reuse.md's P4 decision inputs](reuse.md#p4--one-evidence-selected-four-arm-screen) select the ten frozen gain/displacement/sentinel targets in [targets.md](targets.md#p4list). P2's solo-real-Spot results are diagnostic only. [scripts/seq-p4.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-p4.sh) specifies `cand-89d67bcd`, with **`--loss-check-policy scheduling-hint` on both sides**, and five alternating pairs. That arm comparison does not by itself establish hint-versus-verify-all benefit on the P3a-free candidate.

**End-to-end benefit/regression/disposition:** **PENDING — screen running**. No partial P4 observations are promoted here; retention/rejection is not yet decided. Apply §8 including all displaced-arm/near-cap losses and the separate combined-candidate requirement. The historical five-arm and isolated-arm data are selection evidence, not a result for this exact concurrent portfolio.

## P5 — retained reporting/gate tooling; final campaign pending

**Intended removed work:** reuse saved observations and shared normalization/scoring to avoid rerunning solvers for tables; enforce §8 consistently. Per the sprint handoff, reporting work is associated with **PR #187**: `export-cactus`, `cactus-report` REAL/UNREAL/PAR-2 mean/hash fields, and `paired-admission.py`.

**Measured validation:** [reporting review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p5report-review.log) is **PASS**: 24 malformed/adversarial inputs rejected; duplicate/status/mean mutations caught; historical **1,524-row** reports regenerated with unchanged verdicts, wall times and scores. The reported available Python suite was **694 passed, two skipped**. Missing native bindings blocked full collection and missing matplotlib prevented review PNG/PDF regeneration; Markdown regression succeeded. These are historical report-validation figures, not a new sprint three-way campaign.

The [first admission review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/admission-review-verdict.txt) was **FIX-REQUIRED**: duplicate/swapped rounds, offset near-cap losses, fewer than five pairs and a nondiscriminating runtime-support test. The [fixed review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/admissionfix-review.log) is **PASS**: identities now reject; offset losses and short confirmations stay UNRESOLVED; the three-win case rejects; every listed mutation is caught. It reports **778 passed, two skipped**, excluding the two specified binding-dependent modules. Evaluator correctness checks recorded verdict consistency; independent certificate/unit checks remain external prerequisites.

**Benefit/regression/disposition:** **retained as reporting and admission correctness tooling under §8.4**, with no solver speedup claimed. Saved-data regression checks preserve established scores; no new final end-to-end comparison exists in this report. The final combined candidate and the required uniform-17 s baseline/candidate/ltlsynt CSVs, PAR-2 table and three-curve cactus remain pending under §9.6. PR merge state was not inspected during this read-only documentation task.
