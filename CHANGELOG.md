# Acacia-Bonsai changelog

## 2.6.0 (2026-10-18)

- **New default: the parity portfolio.** The first `docker_default` configuration
  adds `both:parity:spot` (deterministic Büchi games on tiers 1–3, a parity game
  solved by strpm-simd with Spot fallback on tier 4) and the obligation arm
  `both:obligation:spot`. At 17 s on the 1,524-input SYNTCOMP26 corpus it decides
  1,339 inputs against 1,226 for the previous default (different sessions; see
  RESULTS).
- Four shipping configurations, unchanged in order: the default, the no-parity
  fallback, a lean member and the synthesis member.
- OxiDD is always built; bounded-stack handling (#258), native dependency
  preparation and packaging (#259) and cumulative resume summaries (#262) fixed.
- `ACACIA_PHASE_RECORDS` now also records per-arm CPU, memory and phases.
- Research and benchmark tools retired; their source and notes are preserved in
  `refactor-closeout20261017`.

Earlier release notes and their original performance tables are preserved in
`refactor-closeout20261017` (original evidence includes `benchmarking-root-legacy`,
`plots-three-way-full-20260905` and `plots-spot-otf-threeway-20260909`).
See [RESULTS](benchmarking/RESULTS.md) for measured outcomes and limitations,
[decisions](benchmarking/decisions.md) for policy, and the
[evidence index](benchmarking/evidence-index.tsv) for published archives.
