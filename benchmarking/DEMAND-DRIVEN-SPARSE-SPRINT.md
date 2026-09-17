# Demand-driven construction and sparse-forward sprint

Sprint record for the [16 September 2026 revised plan](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/plan.md), updated 17 September. **P4 is rejected; P1a retention and the closing three-way results are pending.** This report records completed package evidence and the candidate selected for closing. It does not claim experimental closure under plan §9.6.

## A. Goal and run identity

The goal is to avoid unnecessary work before and during sparse search: demand-generate automaton rows, remove repeated losing-certificate checks and screen sparse-storage changes. The plan requires **correctness AND no validated regression AND a useful end-to-end benefit** (§8); gains on one input cannot purchase losses on another. Essential measurement/reporting repairs can be retained for correctness without a solver-speed claim (§8.4). The closing artifacts and final checklist are specified in §9.6 and §10.

| Item | Identity / scope |
| --- | --- |
| Source baseline | `6410fd3417f5c05f901ed6afc5094947103c157d`; [P0 manifest](demand-sparse-20260916/manifest.json) records solver/config equivalence to the frozen pre-sprint executable |
| Measured baseline | `candidate-f7919b83`, preset `otf_sparse_formula`, SHA-256 `0f4f48b6b751be91d9e0575105d4678d47dbccb8c998c01d71f9597abe22fb99` |
| Closing candidate | Landing **P0 + P1a**, preset **`otf_sparse_formula_loss_hints`**, commit **`4cbf975c`**; `cand-hints-4cbf975c`, SHA-256 `936b11b8b2c711bbc9296719b2018bdf25ec5bb8778b5fb60bdc4865916d29ae` |
| Shipped arms retained | `real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward` |
| Candidate exclusions | **P2, P3a, P4**; no group/shipping change |
| Headline corpus | SYNTCOMP26 `all.list`, **1,524 IDs**, SHA-256 `0226e5cd2cadc033544f0901bd900dd31dd7add813ef216558826468e0b7b5c7`; native source map SHA-256 `9fbc4007b18065296d592f14bd9ef0e9e95a4893a4d4477e0153013fa531f92f` |
| Planned headline regime | Uniform **17 s**, **8 GiB**, zero swap, no CPU quota, sequential invocations; no diagnostic worker capture |
| External reference | Historical pin: ltlsynt 2.15.1.dev, SHA-256 `ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900`; closing launch records its actual executable/version/hash before the fresh external leg |
| Raw evidence | [_bm-logs.20260916-demand-sparse/](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse); [screen ledger](demand-sparse-20260916/screens.md) pins package configurations, targets, caps and rounds |

The [freeze manifest](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/bin/freeze-manifest.tsv) and [candidate options](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/bin/cand-hints-4cbf975c.options.txt) identify the closing build. The bundle's original `manifest.json` remains a historical P0 inventory; it is not a completed closing manifest. The final candidate label does not imply that P1a has passed the closing retention gate.

## B. What was built

**P0 repaired existing diagnostics:** worker lifecycle boundaries, per-attempt reset, cumulative ended work, fallback segments and observed worker returns. It reused the sink and consumers. The 46-instance diagnostic contains 184 worker records; five separate profiling-twin invocations informed conditional storage gates. The saved final repair review reports 24 mutations caught, 48 unit tests passed and zero allocations in the diagnostics-off allocation probe. These are prior review results, not tests rerun for this report. See [phases](demand-sparse-20260916/phases.md), [profile](demand-sparse-20260916/profile.md) and [decisions](demand-sparse-20260916/decisions.md#p0--retained).

**P1a implemented scheduling-only loss hints:** an inconclusive scheduling result can advance K without repeating a losing-certificate check. Exact APIs retain verification, decisive wins remain independently checked, and a hint cannot justify a final opposite verdict. Closing deploys this through `otf_sparse_formula_loss_hints` at `4cbf975c`, using registry option `acacia_default_loss_check_policy`; its general default stays `verify_all`, while the candidate preset selects `scheduling_hint`. The preset inherits the shipped four arms and changes no groups or shipping defaults. The original five-pair flag screen used `p1a-d14cf955`, which still included P3a. **PENDING closing — retention is decided by the interaction check and campaign on the P3a-free preset candidate.**

**P2 built a closure/cursor provider core and worker integration on a research branch:** fixed acceptance, complete-or-unknown transactional rows, shared sparse search and independent certificate checks, with eager and lazy controls over the same provider. Core `c1f60fff` and measured integration `64b961f0` are preserved in `research/ds-p2-closure-buchi` at `fd6d5a16`. Saved reviews include 21,000 lasso-oracle checks, 100 Spot equivalence checks, 192 lazy/eager finite-K comparisons and mutation/fault-injection checks. The implementation is blocked by first-row explosion and is excluded from landing. Full evidence and caveats: [provider report](demand-sparse-20260916/p2-provider-report.md).

**P3a built move compaction on a research branch:** distinct-slot LossSet survivors move instead of deep-copying rank payloads, preserving order and proof alignment. The code review passed; its performance screen failed. `research/ds-p3a-move-compaction` at `52d4c8e5` preserves the negative result. P3b/c/d were not implemented because the measured CPU selection gates did not fire; alternate routes remain unmeasured.

**P5 extended existing reporting and admission tools, associated with [PR #187](https://github.com/gaperez64/acacia-bonsai/pull/187):** `export-cactus`, `cactus-report` REAL/UNREAL/PAR-2 mean/hash fields, and `paired-admission.py`. Saved-data regression validation preserved historical verdicts, timings and scores. The first admission review found defects; its fixed review passed, including mutations for offset losses and incomplete confirmations. Tooling validation is summarized in [decisions](demand-sparse-20260916/decisions.md#p5--retained-reportinggate-tooling-final-campaign-pending); PR merge state has not been verified here.

## C. Mechanism measurements and outcomes

Cumulative times below cover **ended attempts only**; open work at a cap is censored. P0 counters motivate work but do not measure treatment savings. Sample shares use sparse-exploration event denominators, not cumulative elapsed time. All thresholds mentioned are selection/admission requirements, never achieved results by implication.

| Package | Intended removed work | Actual cumulative measurements / limits | End-to-end benefit | Regression result | Disposition |
| --- | --- | --- | --- | --- | --- |
| P1a | Repeated losing-certificate verification used only to schedule K | P0 ended loss-check / exploration: F-G-contradiction-110 **5,555.593 / 6,962.040 ms**; GF-G-contradiction7 **3,955.668 / 5,861.470 ms**. No paired cumulative treatment-cost attribution | Flag screen: F-G **0/5 → 5/5**; GF-G **3/5 → 5/5**. No qualifying common-solved runtime or actual-memory benefit established | No validated regression in the flag screen; that binary includes P3a | **Admitted screen; PENDING closing retention** on the preset candidate |
| P1b | Rebuilding immutable frozen-provider rows across K | 13 historical multi-K cases; ended row generation at most **3.670 ms per target**. Interrupted work/construction is not a measured whole-run upper bound | None measured; measured row generation alone cannot reach the 50 ms instance-runtime requirement | No implementation or treatment comparison | **Not attempted**; existing lazy retention reused |
| P2 | Full optimized translation/preprocessing before useful search | B/C each: **19/22** targets complete no first row; 20 failing worker records have median **1,786,410** branches, range **381,556–2,000,000**. Collector: **1,024 rows, 10,766,202 cumulative branches**, then failure. OneCounter eager/lazy: **123/97 rows** | No confirmed useful provider benefit. Only CheckAlarm/OneCounter solve in B/C. Exploratory 06/07 portfolio gains come from forward-real after closure exits | One exploratory round loses GF-G; no five-pair admission. P3a inherited; hints off; whole-invocation memory comparison unavailable | **Blocked**, research only; no partial-row redesign |
| P3a | Deep-copying LossSet survivors during compaction | Inclusive insert samples selected the screen; cumulative survivor-copy cost and saved copy time are **unmeasured** | No qualifying benefit; workstation **0/5 → 1/5** is insufficient | GF-G **5/5 → 0/5** at 17 s. 51 s diagnostic returns the same verdict/K progression around **17.22–17.58 s** but does not reverse failure | **Rejected**, research branch retained |
| P3b | Broad scans through tombstones | Sampled broad-scan upper shares **0.23–2.25%**; denominators **312–3,379**, all ≥300; below the 10% selection gate. Cumulative scan cost / alternate replay benefit unmeasured | None measured | No implementation or treatment comparison | **Not attempted** |
| P3c | Exact LossSet comparisons via metadata filtering | Sampled LossSet shares **1.28–4.74%**, below the 10% selection gate; material query share with ≥128 live generators **unevaluated** | None measured | No implementation or treatment comparison | **Not attempted** |
| P3d | Duplicate payload allocation/copying and storage | Sampled payload shares **2.08–5.40%**; generous all-allocation sensitivity at most **11.86%**, below 15%. Duplication ≥2 / ≥25% attributable retained storage alternative **unevaluated** | No actual peak-memory benefit established | No implementation or treatment comparison | **Not attempted** |

Sources: [phases](demand-sparse-20260916/phases.md), [profile and denominators](demand-sparse-20260916/profile.md), [provider report/data](demand-sparse-20260916/p2-provider-report.md), [screen ledger](demand-sparse-20260916/screens.md), [package decisions](demand-sparse-20260916/decisions.md). Smaller logical payloads, fewer rows or a quick UNKNOWN do not establish a useful solver gain. P2's eager failures before K have absent counters, not measured zero search cost.

## D. P4 adjudication

The single P4 screen replaced `real:small:forward` with `real:small:spot-guarded-sparse`, keeping four arms. Both sides ran `cand-89d67bcd` with `--loss-check-policy scheduling-hint`. Five alternating pairs at 17 s gave **0/5 → 5/5** on `SPIPureNext`, `heim-double-x-real`, `ordered-visits-choice-real`, `robot_grid_pb_5_5_pe_` and `thermostat-F-real`, but workstation fell **1/5 → 0/5**. The original evaluator correctly remains **UNRESOLVED**.

The §8.3 longer-cap diagnostic ran three alternating rounds per side at 51 s. Control solved workstation in **15.8–17.8 s**, via the real forward worker at **K=20**; treatment took **38.5–42.6 s**, via the real spot-guarded-sparse worker at **K=20**. All six winners have verified-win evidence. This is a systematic slowdown, not near-cap equivalence. **P4 is REJECTED: five gains cannot compensate for the lost solve under the set rule.** These 51 s answers never become 17 s coverage. Exact times, hashes and worker paths are in [decisions](demand-sparse-20260916/decisions.md#p4--rejected) and [screens](demand-sparse-20260916/screens.md).

## E. Retained, rejected, not attempted and blocked

- **Retained:** P0 measurement repair and P5 reporting/admission tooling, for demonstrated correctness benefits; no solver speedup claimed.
- **Candidate inclusion, retention pending:** P1a, selected in the loss-hints preset with the shipped four arms. The earlier flag-screen admission is not a completed combined-candidate result.
- **Rejected:** P3a move compaction and P4 arm substitution; both excluded from the closing candidate.
- **Not attempted:** P1b frozen-row retention, P3b live-ID scans, P3c metadata/SIMD filters and P3d payload arena. Selection evidence did not justify implementation; unmeasured alternate gates are disclosed above.
- **Blocked:** P2 by first-row explosion and absence of a confirmed useful deployed-mode benefit. Preserve core/integration code and tests on the research branch; stop at the plan §5.9 boundary.

## F. Closing three-way comparison

**PENDING — closing campaign running.** No final coverage, runtime, PAR-2, gain/loss or gate claim is made here. The required publication destination is [demand-sparse-20260916/closing/](demand-sparse-20260916/closing/), which is **not yet populated**. Raw running work is in [closing-run/](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/closing-run).

All three primary series are being remeasured in the same epoch rather than reusing 2026-09-15/16 timings. The older five-versus-four repetitions found workstation **4/5 with four arms**, while this sprint's same-arm-configuration P1a and P4 controls found **0/5** and **1/5**. GF-G-contradiction7's no-hint screen/diagnostic observations lie around **16.4–17.6 s**, close to the cap. The binaries/policies differ, so this is evidence against assuming cross-epoch timing compatibility, not a controlled attribution of drift. Sources and reuse boundary: [reuse.md](demand-sparse-20260916/reuse.md#executed-in-this-sprint), [previous sprint §D](COVERAGE-DIRECTED-FORWARD-SPRINT.md#d-family-level-frontiers).

The [candidate-freeze sequence](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-candidate.sh) records `4cbf975c` and its compiled policy/arms. The [closing driver](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/closing.sh) schedules:

1. Five alternating interaction pairs against frozen `candidate-f7919b83` on `targets/combined.list` (14 IDs), requiring ADMIT before proceeding.
2. G0, G1, candidate G4, G3 and G2s. **Reuse baseline G4** from the same `f7919b83` executable: **Ok 575, Fail 0, Timeout 49** in the [saved log](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260915-closing/g4-candidate.gate.log). **Skip G5 and posets** because TLSF frontend/conversion and posets are unchanged; these skips are not new passing results.
3. Fresh full-SYNTCOMP26 primary observations for candidate, baseline and ltlsynt, with the same 17 s/resource regime; then five alternating pairs on selected changed/suspicious instances, separate from primary rows.
4. Existing `export-cactus` normalization and `cactus-report` generation from saved observations. The retained sequence around the existing coverage driver and external adapter is the three-way workflow; no new runner/scorer is introduced.

**Table and numbers: PENDING — closing campaign running.** Every cell below is a placeholder, not a measured result.

| Series | Solved / 1,524 | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | Timeouts | UNKNOWN | Memory failures | Errors/crashes | Raw dataset hash |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Acacia baseline | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Acacia candidate | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| ltlsynt | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |

**Plot: PENDING — closing campaign running.** Required artifacts in the closing destination: `three-way-par2.md`, `three-way.png`, `three-way.pdf`, `acacia-baseline.csv`, `acacia-candidate.csv`, `ltlsynt.csv`, `gains-losses.tsv`, `confirmations.tsv` and a provenance/commands `README.md`. Their generation, ID/hash validation, score/endpoint cross-checks and publication remain outstanding under §9.6. A driver completion marker alone does not establish that every gate or export succeeded.

Preserve the established timing caveats: Acacia includes its native TLSF frontend; the external route excludes cached SyFCo conversion from timed synthesis, and Strict semantics has no ltlsynt counterpart. Conversion failures must remain visible. All nonsolved primary observations receive the shared reporter's 34 s PAR-2 charge; primary rows cannot use best-of-five times, solo-arm unions or 51 s diagnostic successes. Updated external provenance, final failure/conflict counts, confirmations and any comparable-semantics subset remain pending.

## G. Branches, PRs and evidence limits

- **Landing:** `sprint/demand-sparse-landing`, master plus P0 and P1a; landing code checkpoint `89d67bcd`, documentation `3a70d131`, loss-hints preset `4cbf975c`. The code measured for P4 is `89d67bcd`; closing measures the separately frozen preset build at `4cbf975c`.
- **P3a research:** `research/ds-p3a-move-compaction` at `52d4c8e5`; rejected and excluded.
- **P2 research:** `research/ds-p2-closure-buchi` at `fd6d5a16`; core `c1f60fff` (`sprint/ds-p2-closure-core` in the handoff), measured integration `64b961f0`; blocked and excluded.
- **Reporting:** [PR #187](https://github.com/gaperez64/acacia-bonsai/pull/187), associated with the existing reporting/admission changes by the sprint handoff. Its remote state and any other PR numbers/merge states were not verified.

This record was written from local source, manifests, saved runs and reviews only. No compilation, solver, test, profiling or benchmark command was executed for it. Remaining evidence limits are the closing results/artifact bundle, paired cumulative P1a savings and P3a copy-cost measurements, usable whole-invocation memory comparisons, P2's missing worker/unfinished eager denominators, and the unevaluated alternate P3 gates. The original [reuse inventory](demand-sparse-20260916/reuse.md) and [manifest](demand-sparse-20260916/manifest.json) preserve the earlier checkpoint; [decisions](demand-sparse-20260916/decisions.md) and [screens](demand-sparse-20260916/screens.md) give the current package record.
