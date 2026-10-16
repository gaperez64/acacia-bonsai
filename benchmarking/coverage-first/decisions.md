# Coverage-first sprint: decision record

Started 2026-10-06 from `d9d3fd43` (tlsf-tools `4049635`). Evidence IDs refer to
[evidence-index.tsv](../evidence-index.tsv). Expected verdicts and instance names are
used only to select and evaluate measurements, never in solver logic.

| Date | Decision | Evidence |
|---|---|---|
| 2026-10-06 | Measurement infrastructure first (P0): route attribution with typed stop causes, cgroup peaks collected before teardown, corrected gates. A decline means only a genuine applicability decline. | `cov20261006-p0exit` |
| 2026-10-06 | Timeouts get a fixed global 500 ms cleanup window after SIGTERM; answers inside it remain TIMEOUT. | `cov20261006-p0exit` (delivery 144/180 → 180/180) |
| 2026-10-07 | Keep R on (three confirmed REAL gains, one near-cap UNREAL loss). | `cov20261006-p1ablation` |
| 2026-10-07 | Do not disable equivariance; bound its pre-pass instead (zero marginal solves, one confirmed loss, PAR-2 above noise). | `cov20261006-p1ablation` |
| 2026-10-07 | P3 widens weakening eligibility to assume–guarantee objectives, keeping every assumption, with bounded attempts; the incumbent mode stays the default until admission. | `cov20261006-p1ablation`, P3 census |
| 2026-10-07 | P2b screens the native structure-budget guards with an off-by-default global scale before changing any default. | `cov20261006-p1ablation` |
| 2026-10-07 | Gates for the ablation switches: G0, G1, G4 pass; G5 and G2s not applicable (defaults identical, no frontend or membership change). | `cov20261006-p1ablation` (G1/G4 logs) |
| 2026-10-07 | Shipping has no known deadline (owner): admission campaigns run without `--route-records`, the fresh no-deadline incumbent is the baseline, and every new budget is absolute and tested with and without a deadline. The P1 matrix already ran in this mode. | owner decision; `cov20261006-p1ablation` |
| 2026-10-07 | Equivariance accepts absolute `s`/`ms` allowances without a wrapper deadline; fractions use one global 5 s reference when none is supplied, matching extended weakening. Keep the unbounded default and screen a paired `3s` treatment before admission. | owner decision; [allowance protocol](equivariance-budget.md) |
| 2026-10-07 | Do not scale the native structure guard: with it off no guard-stopped input solves (103/138 are `mp-class`). | `cov20261007-screens` |
| 2026-10-07 | Stop P4a: the dual of every `mp-class` frontier input is also rejected. | `cov20261007-screens` |
| 2026-10-07 | Deprioritize P5: U is reachable on at most one frontier input. | `cov20261007-screens` |
| 2026-10-08 | Keep weakening extensions opt-in only: no frontier proof even at 2 s/8 s allowances. | `cov20261007-screens` |
| 2026-10-08 | Keep declaration order and the incumbent R alignment: structural orders and typed roles give no robust gain. | `cov20261007-screens` |
| 2026-10-08 | Admit `3s` as the runtime default for `--equivariance-budget`; keep explicit `unbounded`, fractions and durations selectable. Keep this runtime allowance in the CLI state rather than adding a compile-time registry option. | `cov20261007-eqfull` (to be archived): fresh matched full-corpus 17 s screen, no deadline, one binary, rotated; 1,206 vs 1,205 solves, one gain, zero losses or verdict conflicts, identical memory outcomes, paired PAR-2 −23.3 s (12.1 s noise floor); [allowance protocol](equivariance-budget.md) |
| 2026-10-11 | Implement compact sparse action payloads after measuring 0.46–28.28% panel row density; retain list/splice order, lexicographic ties, endpoint order and RNG consumption. No additional action-ID/order redesign; driver admission still pending. | [Action payload measurements](action-payload-layout.md): six paired REAL-arm profiles, robot OOM → search, Morning2s table/peak reductions, frozen byte-identical generated traces; unit/full pytest/hardcoding pass. |
| 2026-10-16 | A: measure the memory-share-alone option; it gains chomp_pb_3_6_pe_ at 180 s in three repetitions (~100 s), but leave the default share unchanged. Rebuild screen.py summaries from the whole raw input list after its one-input calls. | `otd-A-obligation20261015` |
| 2026-10-16 | B: implication-simplification screen found no decision gain (one translation rescue, worker cannot decide it); 524 deferred boundaries left unmeasured end to end; retain the shipping translation policy. | `otd-B-implication20261014` |
| 2026-10-16 | S: negative downset rescreen; retain the shipping representation. | `otd-S-downsets20261014` |
| 2026-10-16 | C: the parity arm supersedes the experimental deterministic-acceptance conversion into the counting game. | `otd-C-parity-route20261015` |
| 2026-10-16 | Tiers 1–3: Spot's explicit game solver only; it beats symbolic GR(1), strpm-simd and antichain search on the 36-input comparison. | `otd-parity-engines20261015` |
| 2026-10-16 | Tier 4: strpm-simd with adaptive (k,t), then Spot fallback; trust production verdicts and strategies without a recheck, with independent checks retained in tests. | `otd-C-parity-route20261015`, `otd-parity-engines20261015`; [arm contract](parity-arm.md) |
| 2026-10-16 | Accept G2s's failed 6% cycle ceiling on three inputs (+12.6/+32.9/+8.2%) as the always-on parity worker's cost. | owner decision; `otd-gates-OTF20261016` |
| 2026-10-16 | Waive G5's seven SyFCo strong-next conversion failures (#265); no opposite verdicts or frontend errors. | owner decision; `otd-gates-OTF20261016` |
| 2026-10-16 | Campaign-only exception: archived master/ltlsynt comparisons are INDICATIVE (different revision/sessions); derive OTF 60 s by keeping measured-60 rows and otherwise recycling decided 17 s rows with their original times. Confirm every selection with three cooled OTF repetitions; after complete OTM r1, skip OTM r2/r3 only if archived master and OTM r1 are undecided, retaining full pairs for near-cap selections and all decisions. Record skips explicitly; confirmations do not replace primary rows. | owner decisions 2026-10-15/16; `otd-OTF-owner20261016`; [verbatim disclosure and rows](../RESULTS.md) |
| 2026-10-16 | Ship OTF (previous shipping portfolio plus both:parity:spot) as the first docker_default member; retain the previous default second. Confirmed 17 s: +110 shortcut and +1/−1 paired; 60 s: +96, zero losses; no conflicts. Keep A's memory-share option off the default. | owner decision; `otd-OTF-owner20261016`, `otd-gates-OTF20261016` |
