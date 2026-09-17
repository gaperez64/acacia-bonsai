# Demand-driven construction and sparse-forward sprint

Sprint record for the [16 September 2026 revised plan](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/plan.md), closed 17 September 2026. **No package was admitted, so no solver code lands.** The required three-way report compares the baseline (which is also the closing candidate), and ltlsynt on the full SYNTCOMP26 set; the two candidates measured end to end appear as supplementary series. Every retained artifact is documentation, evidence or the reporting tools in [PR #187](https://github.com/gaperez64/acacia-bonsai/pull/187).

## A. Goal and run identity

The goal was to avoid unnecessary work before and during sparse search: demand-generate automaton rows, remove repeated losing-certificate checks and screen sparse-storage changes. The plan requires **correctness AND no validated regression AND a useful end-to-end benefit** (§8); gains on one input cannot purchase losses on another, and a measurement repair may skip the benefit requirement but not the no-regression one (§8.4).

| Item | Identity / scope |
| --- | --- |
| Source baseline | `6410fd3417f5c05f901ed6afc5094947103c157d`; the [P0 manifest](demand-sparse-20260916/manifest.json) records solver/config equivalence to the frozen executable |
| Baseline = closing candidate | `candidate-f7919b83`, preset `otf_sparse_formula`, shipped four arms, SHA-256 `0f4f48b6b751be91d9e0575105d4678d47dbccb8c998c01d71f9597abe22fb99` |
| Evaluated, not admitted | P0+P1a `cand-hints-4cbf975c` (preset `otf_sparse_formula_loss_hints`, `936b11b8…`); P0 `retained-e27dd528` (preset `otf_sparse_formula`, `14b71f87…`) |
| Headline corpus | SYNTCOMP26 `all.list`, **1,524 IDs**, SHA-256 `0226e5cd2cadc033544f0901bd900dd31dd7add813ef216558826468e0b7b5c7`; native source map `9fbc4007b18065296d592f14bd9ef0e9e95a4893a4d4477e0153013fa531f92f` |
| Headline regime | Uniform **17 s**, **8 GiB**, zero swap, no CPU quota, sequential invocations, one epoch (2026-09-17) |
| External reference | ltlsynt (spot) 2.15.1.dev, SHA-256 `ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900`; SyFCo v1.2.1.2 |
| Evidence | Bundle [demand-sparse-20260916/](demand-sparse-20260916/); raw runs [_bm-logs.20260916-demand-sparse/](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse) |

## B. What was built

**P0 repaired existing diagnostics:** worker lifecycle boundaries, per-attempt reset, cumulative ended work, fallback segments and observed worker returns, reusing the sink and consumers. Its records drove this sprint's work selection ([phases](demand-sparse-20260916/phases.md), [profile](demand-sparse-20260916/profile.md)). A follow-up (`e27dd528`) put every record action behind one sink check outside the solving loops. **Not retained:** the closing campaign found a regression (section C). Code on `research/ds-p0-lifecycle-records`.

**P1a implemented scheduling-only loss hints:** an increasing-K wrapper may advance K on a completed losing search without the losing checker, through a type-separated result that can never become a verified loss; wins remain independently checked. Deployed for closing through registry option `acacia_default_loss_check_policy` and preset `otf_sparse_formula_loss_hints`. **Not admitted.** Code, option and preset on `research/ds-p1a-loss-hints`.

**P2 built a closure/cursor provider core and worker integration on a research branch:** fixed acceptance, complete-or-unknown transactional rows, shared sparse search and independent certificate checks, with eager and lazy controls over the same provider. Core `c1f60fff` and measured integration `64b961f0` are preserved in `research/ds-p2-closure-buchi` at `fd6d5a16`. Saved reviews include 21,000 lasso-oracle checks, 100 Spot equivalence checks, 192 lazy/eager finite-K comparisons and mutation/fault-injection checks. The implementation is blocked by first-row explosion and is excluded from landing. Full evidence and caveats: [provider report](demand-sparse-20260916/p2-provider-report.md).

**P3a built move compaction on a research branch:** distinct-slot LossSet survivors move instead of deep-copying rank payloads, preserving order and proof alignment. The code review passed; its performance screen failed. `research/ds-p3a-move-compaction` at `52d4c8e5` preserves the negative result. P3b/c/d were not implemented because the measured CPU selection gates did not fire; alternate routes remain unmeasured.

**P5 extended existing reporting and admission tools, associated with [PR #187](https://github.com/gaperez64/acacia-bonsai/pull/187):** `export-cactus`, `cactus-report` REAL/UNREAL/PAR-2 mean/hash fields, and `paired-admission.py`. Saved-data regression validation preserved historical verdicts, timings and scores. The first admission review found defects; its fixed review passed, including mutations for offset losses and incomplete confirmations. Tooling validation is summarized in [decisions](demand-sparse-20260916/decisions.md#p5--retained-reportinggate-tooling-final-campaign-pending); PR merge state has not been verified here.

## C. Mechanism measurements and outcomes

Cumulative times cover **ended attempts only**; open work at a cap is censored. Thresholds are selection/admission requirements, never achieved results by implication.

| Package | Intended removed work | Actual cumulative measurements / limits | End-to-end benefit | Regression result | Disposition |
| --- | --- | --- | --- | --- | --- |
| P0 | Stale phase labels and counters in worker records (measurement repair, no removed work) | No-sink G2s cost **+1.27%** (P0) and **+1.03%** (with guard) cycles geomean; same-path rebuild control **+0.42%** vs frozen, rebuild vs P0 **−1.38%**; forward arm alone **+1.3% cycles, +0.02% instructions**, hot loop identical except alignment padding | None claimed | Closing: **1,173 vs 1,176** solved; confirmations GF-G **4/5→2/5**, g-unreal-115 **2/5→0/5**, workstation **2/5→1/5**; at 51 s g-unreal-115 slower every round (**17.96–18.12 s** vs 16.28–17.99 s) | **Not retained**; research branch |
| P1a | Repeated losing-certificate verification used only to schedule K | P0 ended loss-check / exploration: F-G-contradiction-110 **5,555.593 / 6,962.040 ms**; GF-G-contradiction7 **3,955.668 / 5,861.470 ms** | Package screen F-G **0/5→5/5**; on the closing build F-G **0/5→2/5**, g-unreal-310 **0/5→1/5**, GF-G **3/5→5/5** (control not 0/5): no confirmed §8.2 benefit | g-unreal-115 **3/5→0/5**; at 51 s **17.44–19.04 s** vs **16.31–16.69 s**: hinted sparse worker climbs to K=14 and competes with the deciding arm | **Rejected**; research branch |
| P1b | Rebuilding immutable frozen-provider rows across K | 13 historical multi-K cases; ended row generation at most **3.670 ms per target**. Interrupted work/construction is not a measured whole-run upper bound | None measured; measured row generation alone cannot reach the 50 ms instance-runtime requirement | No implementation or treatment comparison | **Not attempted**; existing lazy retention reused |
| P2 | Full optimized translation/preprocessing before useful search | B/C each: **19/22** targets complete no first row; 20 failing worker records have median **1,786,410** branches, range **381,556–2,000,000**. Collector: **1,024 rows, 10,766,202 cumulative branches**, then failure. OneCounter eager/lazy: **123/97 rows** | No confirmed useful provider benefit. Only CheckAlarm/OneCounter solve in B/C. Exploratory 06/07 portfolio gains come from forward-real after closure exits | One exploratory round loses GF-G; no five-pair admission. P3a inherited; hints off; whole-invocation memory comparison unavailable | **Blocked**, research only; no partial-row redesign |
| P3a | Deep-copying LossSet survivors during compaction | Inclusive insert samples selected the screen; cumulative survivor-copy cost and saved copy time are **unmeasured** | No qualifying benefit; workstation **0/5 → 1/5** is insufficient | GF-G **5/5 → 0/5** at 17 s. 51 s diagnostic returns the same verdict/K progression around **17.22–17.58 s** but does not reverse failure | **Rejected**, research branch retained |
| P3b | Broad scans through tombstones | Sampled broad-scan upper shares **0.23–2.25%**; denominators **312–3,379**, all ≥300; below the 10% selection gate. Cumulative scan cost / alternate replay benefit unmeasured | None measured | No implementation or treatment comparison | **Not attempted** |
| P3c | Exact LossSet comparisons via metadata filtering | Sampled LossSet shares **1.28–4.74%**, below the 10% selection gate; material query share with ≥128 live generators **unevaluated** | None measured | No implementation or treatment comparison | **Not attempted** |
| P3d | Duplicate payload allocation/copying and storage | Sampled payload shares **2.08–5.40%**; generous all-allocation sensitivity at most **11.86%**, below 15%. Duplication ≥2 / ≥25% attributable retained storage alternative **unevaluated** | No actual peak-memory benefit established | No implementation or treatment comparison | **Not attempted** |

Sources: [decisions](demand-sparse-20260916/decisions.md), [screen ledger](demand-sparse-20260916/screens.md), [provider report](demand-sparse-20260916/p2-provider-report.md), [confirmations](demand-sparse-20260916/closing/confirmations.tsv), [P0 cost attribution](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-perfdiff/ATTRIBUTION.md).

## D. P4 adjudication

The single P4 screen replaced `real:small:forward` with `real:small:spot-guarded-sparse`, keeping four arms, both sides with the hint policy. Five alternating pairs at 17 s gave **0/5 → 5/5** on `SPIPureNext`, `heim-double-x-real`, `ordered-visits-choice-real`, `robot_grid_pb_5_5_pe_` and `thermostat-F-real`, but workstation fell **1/5 → 0/5** (UNRESOLVED). At 51 s the control solved workstation in **15.8–17.8 s** through the forward worker and the treatment in **38.5–42.6 s** through the sparse real worker: a systematic slowdown. **Rejected** under the set rule.

## E. Retained, rejected, not attempted and blocked

- **Retained:** P5 reporting and admission tooling (PR #187) and this evidence record; no solver speedup claimed.
- **Not admitted after closing:** P1a (no confirmed benefit, g-unreal-115 slowdown) and P0 (confirmed near-cap success loss, g-unreal-115 slowdown, cycle increase).
- **Rejected:** P3a move compaction (GF-G-contradiction7 5/5 → 0/5) and P4 arm substitution (workstation 2.5× slower).
- **Blocked:** P2 closure/cursor provider by first-row explosion (19 of 22 affected targets never complete a first row in either closure mode); code and tests on the research branch.
- **Not attempted:** P1b (at most 3.67 ms of repeatable row generation per target), P3b/P3c/P3d (CPU selection gates did not fire; alternate gates unevaluated).

## F. Closing three-way comparison

Primary report ([closing/README.md](demand-sparse-20260916/closing/README.md), [table](demand-sparse-20260916/closing/three-way-par2.md), [PNG](demand-sparse-20260916/closing/three-way.png), [PDF](demand-sparse-20260916/closing/three-way.pdf)). Nothing was admitted, so the candidate series is the baseline's own rows, disclosed as such.

| Series | Solved / 1,524 | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | Timeouts | UNKNOWN | Memory failures | Errors | Raw dataset hash |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Acacia baseline | 1,176 | 533 | 643 | 12,624.620 | 8.284 | 345 | 1 | 2 | 0 | `922ac3533a56a4427d66f74e9d27c7fb03e42948cef63376fd11573864623c39` |
| Acacia candidate (= baseline) | 1,176 | 533 | 643 | 12,624.620 | 8.284 | 345 | 1 | 2 | 0 | `922ac3533a56a4427d66f74e9d27c7fb03e42948cef63376fd11573864623c39` |
| ltlsynt | 1,258 | 574 | 684 | 9,504.731 | 6.237 | 254 | 2 | 0 | 3 (+7 SYFCO-FAIL) | `f81beba80ab5639b01c05d921d000aa0022060adf8255d9b4a4b739f8d0e1cfb` |

Supplementary, evaluated but not admitted ([table](demand-sparse-20260916/closing/supplementary-evaluated-par2.md), [PNG](demand-sparse-20260916/closing/supplementary-evaluated.png)): P0+P1a **1,177** solved, PAR-2 **12,553.750 s**; P0 **1,173**, PAR-2 **12,672.294 s**. These are single primary observations; their five-round confirmations and 51 s adjudications ([confirmations.tsv](demand-sparse-20260916/closing/confirmations.tsv)) decide admission, not the PAR-2 differences. Every observed difference is in [gains-losses.tsv](demand-sparse-20260916/closing/gains-losses.tsv).

All three primary series were measured fresh in one epoch rather than reusing the 2026-09-15/16 rows, because near-cap outcomes had drifted ([reuse.md](demand-sparse-20260916/reuse.md)). Acacia times include its native TLSF frontend; the ltlsynt route excludes cached SyFCo conversion and Strict semantics has no ltlsynt counterpart. Every nonsolved observation is charged 34 s by the shared scorer. Gates on the evaluated builds: G0 and G1 passed, G4 matched the baseline (575 ok / 0 fail / 49 timeouts), G3 passed on both panels for both builds, G2s printed GATE FAIL for both (it requires a ≥5% speedup; P0+P1a −1.50% and P0 −1.03% cycles). G5 and posets were skipped because the TLSF frontend and posets were unchanged.

## G. Branches, PRs and evidence limits

- **This record:** `sprint/demand-sparse-record` (documentation and evidence only).
- **Reporting tools:** [PR #187](https://github.com/gaperez64/acacia-bonsai/pull/187), `sprint/ds-threeway-report`.
- **Research branches:** `research/ds-p0-lifecycle-records` (P0 + no-sink guard, `e27dd528`); `research/ds-p1a-loss-hints` (P0 + P1a + preset, `b321343a`); `research/ds-p2-closure-buchi` (`fd6d5a16`; core `c1f60fff`, integration `64b961f0`); `research/ds-p3a-move-compaction` (`52d4c8e5`).

Evidence limits: paired cumulative P1a savings and P3a copy costs are unmeasured; whole-invocation memory comparisons were mostly unavailable; P2's unfinished eager denominators are unknown; the alternate P3c/P3d gates are unevaluated; part of P0's g-unreal-115 slowdown is not explained by the measured forward-arm placement effect.
