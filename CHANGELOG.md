# Acacia-Bonsai changelog

## Unreleased

## 2.8.0 (2026-11-06)

- The TLSF → Acacia pipeline is now fuzzed (#369). tlsf-tools v1.9 generates small TLSF specifications from arbitrary bytes and checks its parser, printer, allocation-failure handling and DPA builder on them; Acacia replays the generated inputs and checks that its native conversion agrees with SyFCo, that the tlsf-tools DPA agrees with Spot's automaton, and that no two default arms give opposite verdicts. Seeds run in the unit suite and a weekly CI job grows and replays the corpora.
- tlsf-tools v1.9 fixes what the fuzzer found: printed big operators that lost required parentheses, set deduplication that dropped a strong next under finite semantics, and crashes or partial rewrites when memory runs out. Acacia's verdicts on infinite-word specifications were not affected.
- Faster DPA construction in tlsf-tools: rows visit only reachable formula nodes and the intern tables use tagged probing. On all 1,524 SYNTCOMP26 inputs the default portfolio decides 1,365/1,385/1,390 at 17/60/120 s, three more than before at 17 s with none lost.
- **Known issue (#375).** Spot's obligation synthesis answers REALIZABLE for some unrealizable obligation specifications, such as `G i -> G(i <-> X X X false)`. The `both:obligation:spot` arm uses it, so Acacia can print a wrong REALIZABLE when that arm finishes first. Reported upstream as spot#666; the arm stays in the default portfolio until Spot ships a fix.
- Linux phase recording requests a 1 MiB pipe to reduce startup drops, silently keeping the default capacity if refused.
- Remove unsuccessful DPA route experiments and lazy parity arms; keep eager parity and telemetry fixes.
- The default portfolio adds `both:parity:oxidd` alongside `both:parity:spot`,
  bringing it to eight arms. On all 1,524 SYNTCOMP26 inputs it decides 1,351 at
  17 s and 1,379 at 60 s, against 1,341 and 1,364 for archived master's seven
  arms, with no verdict disagreement. Arm reduction is tracked in #359.
- The parity worker now runs past its equal memory share until the solver's memory scope is nearly full and decides more inputs (#341, evidence `sharegate-20261028`). Use `--parity-memory-share equal` for the old behaviour.

## 2.7.2 (2026-10-27)

- **Completeness fix: MONA precomputers keep empty responses.** Both `mona` and
  `semantic_mona` dropped legal output choices that enable no transitions, which
  could make backward and forward solving return UNKNOWN for realizable
  specifications such as `F (c <-> i) | G F c` (#335, #336). The shipped Docker
  configurations use `semantic_mona` and were affected. The bug only removed
  choices from the player whose win is checked, so it could not produce a wrong
  REALIZABLE or UNREALIZABLE answer.
- Shared equivariant solving falls back to the ordinary solver when a K raise
  would exceed the rank type's range, avoiding overflow with large K schedules
  (#337). The default schedule was not affected.
- `proofs/` holds a Lean proof that the guarded symmetry reduction is sound
  (#331, #334). Both fixes above were found while writing it.

## 2.7.1 (2026-10-26)

- **Soundness fix: equivariance could answer REALIZABLE wrongly.** The symmetry
  detector accepted permutations that swap an input with an output, so a
  specification could be reduced to one with a different answer. Symmetries
  now keep inputs and outputs apart (#330). The default configuration was
  affected; 2.7.0 is confirmed, and earlier releases with equivariance on are
  likely affected too. `--equivariance off` avoids it on older versions.
  No disagreement with ltlsynt was seen on the benchmark corpora.

## 2.7.0 (2026-10-23)

- **Faster MONA preparation.** The semantic MONA precomputer reads transition
  endpoints straight from BuDDy's minterm assignments (#320) and, with at most 12
  active APs, builds actions from explicit guard letters (#321); output is
  byte-identical. Paired with the code before these changes at 17 s on the
  1,524-input SYNTCOMP26 corpus, five more inputs are decided (1,334 → 1,339) and
  none are lost (evidence `perf-20261022`).
- **BuDDy grows its node table by up to 4M nodes** instead of Spot's 512K
  (`-Dacacia_bdd_max_increase`, 0 keeps Spot's step; #319, #324). Four more
  inputs are decided in the same paired setting (1,338 → 1,342), none lost; peak
  memory rises on 205 inputs, at most 5.4 GiB (evidence `bddgrowth-20261023`).
- Smaller forward-search and symmetry-search costs (#317, #318).
- OxiDD moves to upstream main: worker stacks fit `RLIMIT_AS`, and a failed
  manager construction is a clean native failure instead of a panic.
  tlsf-tools and posets move to their main branches.
- The bundled yyjson links statically (#291); child-signal records are written
  atomically (#316).

## 2.6.1 (2026-10-18)

- Tier 4 of the parity arm solves its ACD parity game directly with Spot; the
  strpm-simd port is removed (#282, evidence `tier4-spot282-20261018`).
- Python wheels build again (Bison 3.8.2 on manylinux, no `std::print`), so 2.6
  reaches PyPI.

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
