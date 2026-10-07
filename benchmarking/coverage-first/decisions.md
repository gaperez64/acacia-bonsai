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
| 2026-10-07 | Do not scale the native structure guard: with it off no guard-stopped input solves (103/138 are `mp-class`). | `cov20261007-screens` |
| 2026-10-07 | Stop P4a: the dual of every `mp-class` frontier input is also rejected. | `cov20261007-screens` |
| 2026-10-07 | Deprioritize P5: U is reachable on at most one frontier input. | `cov20261007-screens` |
| 2026-10-08 | Keep weakening extensions opt-in only: no frontier proof even at 2 s/8 s allowances. | `cov20261007-screens` |
| 2026-10-08 | Keep declaration order and the incumbent R alignment: structural orders and typed roles give no robust gain. | `cov20261007-screens` |
