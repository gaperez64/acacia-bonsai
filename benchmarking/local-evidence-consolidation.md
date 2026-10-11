# Local evidence consolidation — 11 October 2026

The original local artifact directories were compared by SHA-256 and byte count
against all 135 published evidence archives. Unmatched evidence is prepared for
release `evidence-2026-10-11-consolidation`. These new archive IDs are **pending
publication and committed evidence-index rows**. The driver's upload manifest and
deletion plan are outside Git in `build_scratch/consolidate-20261011/` at the
original repository root. No local evidence was removed.

The `archive` column in [baselines.tsv](baselines.tsv) identifies each frozen
executable. Its `path` records the original location, relative to a restore root,
and does not require retaining that original checkout. New binary archives also
retain available configuration, introspection, provenance, and yyjson libraries.
After fetching, restore executable permissions and configure the restored runtime
library search path before using an archived executable.

The consolidation status `closed` describes an immutable retention snapshot. It
does not change any stopped or partial historical campaign into a completed
measurement. Original measured rows and their numbers remain untouched.

Archive `consolidation20261011-audit` preserves `FILE-AUDIT.tsv` (one row per
local file or link), `REFERENCES.tsv` (tracked file, line and reference purpose),
`SYMLINKS.tsv`, publication holds, and the index provenance corrections.
Coverage is by content, so an existing archive member may have a different path.
Duplicate content within new campaign supplements is stored once; long USTAR
paths use content-addressed members. The audit retains every original path and
its actual archive member. Symlink contents are materialized when needed and
exact link targets remain in `SYMLINKS.tsv`, including two dangling build links.

To reconstruct a historical directory after publication, fetch the audit into
`restore/consolidation-audit`, then use its `restore-local.py --list-archives`
with `--audit`, `--directory`, `--fetched`, and `--dest` arguments to list required
IDs. Fetch each listed ID with `scripts/acacia-evidence.py fetch --campaign ID
--dest restore/fetched/ID`. Run the same helper without `--list-archives` to
verify the fetched content and rebuild the original paths below a new destination.
The helper materializes symlinks and omits classified build intermediates. A
held file prevents a complete reconstruction; keep its original directory.

The publication guard withheld two optimization review logs and Stage C's
`final-state.patch` because they contain obfuscation-mapping markers. Their exact
paths are in `PUBLICATION-HOLDS.tsv`. Both containing directories remain unsafe
to delete even after the prepared assets are published. No guard was bypassed.

Four existing index rows had provenance inconsistent with their verified archive
metadata: `cov20261006-p0exit`, `cov20261006-p1ablation`,
`binary-COVBASE-26bda40e`, and `cov20261007-screens`. The proposed index corrections
match the embedded `CAMPAIGN.json`; archive URLs, digests, sizes, supports text,
and measured outcomes stay unchanged. The audit preserves the old fields.
The packaging source revision identifies the consolidation checkout. Historical
compilation revisions and host/dependency pins remain in the original metadata;
no historical binary is described as a fresh build of master.

Scope has 28 top-level log directories, 30 top-level build directories, and six
selected `build_scratch` children (64 deletion roots). The stated count of 31
build directories includes the `build_scratch` parent; that parent is retained
and has an additional protected row in the deletion plan.
`build_scratch/cov` is excluded. Its tracked references remain in
[the equivariance protocol](coverage-first/equivariance-budget.md) (the frozen
worktree binary) and [timeout teardown](coverage-first/timeout-teardown.md)
(the driver rerun script). They are recorded separately in `REFERENCES.tsv`.

New local supplements are grouped below; an empty entry means every content is
already in the published archives. The per-file audit identifies those published
IDs and members, including all separately archived frozen executables.

| Original directory | New campaign supplements / binary archives |
|---|---|
| `_bm-logs` | `benchmarking-root-legacy-localrest` |
| `_bm-logs-ltlsynt-component-bypass-2025` | `ltlsynt-component-bypass-2025-localrest` |
| `_bm-logs-ltlsynt-component-bypass-20260804` | `ltlsynt-component-bypass-20260804-localrest` |
| `_bm-logs-ltlsynt-gap-campaign-20260803-224955` | `ltlsynt-gap-campaign-20260803-224955-localrest` |
| `_bm-logs-ltlsynt-gap-campaign-20260804-tier3-contiguous` | `ltlsynt-gap-campaign-20260804-tier3-contiguous-localrest` |
| `_bm-logs-ltlsynt-phase1-any-small-aborted-20260803` | `ltlsynt-phase1-any-small-aborted-20260803-localrest` |
| `_bm-logs-ltlsynt-phase1-small-any-20260803` | `ltlsynt-phase1-small-any-20260803-localrest` |
| `_bm-logs-top4-on-2024_20s` | `top4-on-2024_20s-localrest` |
| `_bm-logs.20260912-s0-phases` | `gr1-par2-20260923-s0-localrest` |
| `_bm-logs.20260915-closing` | Already published |
| `_bm-logs.20260916-demand-sparse` | Already published |
| `_bm-logs.cov-20261006` | `cov20261007-screens-localrest` |
| `_bm-logs.final-v1-current-1d48a15f-20260825` | `binary-LOCAL-_bm-logs.final-v1-current-1d48a15f-20260825-build-current-best_decomp_rank_bucketed_mona-8e677cdd`, `plots-final-v1-current-20260825-localrest` |
| `_bm-logs.gap-plan-20260804` | `gap-plan-20260804-localrest` |
| `_bm-logs.gr1-par2-e2e-P14` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P2a` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P2b` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P2c` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P2d-policy` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P2d-region` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P5-policy` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-e2e-P5-region` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-full-aborted-20260924` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-p0-replay-L0` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-s0-run1` | `gr1-par2-20260923-localrest` |
| `_bm-logs.gr1-par2-s0-run2-L0` | `gr1-par2-20260923-localrest` |
| `_bm-logs.syntcomp25-basis-20260828` | `syntcomp25-basis-20260828-localrest` |
| `_bm-logs.three-way-20260828` | `plots-three-way-20260829-localrest` |
| `build` | `binary-LOCAL-build-7a35774a`, `historical-build-localrest` |
| `build_base` | `binary-LOCAL-build_base-798be79f`, `historical-build-base-localrest` |
| `build_best_decomp_mona` | `binary-LOCAL-build_best_decomp_mona-4ba9292a`, `historical-build-best-decomp-mona-localrest` |
| `build_best_decomp_mona_any` | `binary-LOCAL-build_best_decomp_mona_any-3f077d97`, `historical-build-best-decomp-mona-any-localrest` |
| `build_best_decomp_mona_diag` | `binary-LOCAL-build_best_decomp_mona_diag-dca49682`, `historical-build-best-decomp-mona-diag-localrest` |
| `build_best_decomp_rank_bucketed_mona_eq_min_blocks_2` | `historical-build-best-decomp-rank-bucketed-mona-eq-min-blocks-2-localrest` |
| `build_c5_b355604a` | `historical-build-c5-b355604a-localrest` |
| `build_c5s216_1b9e4b5f` | `historical-build-c5s216-1b9e4b5f-localrest` |
| `build_final_a3c38777` | `historical-build-final-a3c38777-localrest` |
| `build_final_f74ad9d1` | `historical-build-final-f74ad9d1-localrest` |
| `build_gap_action_dedup` | `historical-build-gap-action-dedup-localrest` |
| `build_gr1par2-adapter-112eb7fe` | `historical-build-gr1par2-adapter-112eb7fe-localrest` |
| `build_ltlsynt` | `binary-LOCAL-build_ltlsynt-e6a44fbc`, `historical-build-ltlsynt-localrest` |
| `build_o5_4ab02725` | `consolidation20261011-audit`, `historical-build-o5-4ab02725-localrest` |
| `build_oracle_tlsf` | `historical-build-oracle-tlsf-localrest` |
| `build_otf_diag` | `binary-LOCAL-build_otf_diag-5d993359`, `historical-build-otf-diag-localrest` |
| `build_otf_sparse_formula` | `binary-LOCAL-build_otf_sparse_formula-5451c91e`, `historical-build-otf-sparse-formula-localrest` |
| `build_p4n4_9f5ea45f` | `historical-build-p4n4-9f5ea45f-localrest` |
| `build_p4n6_54032834` | `historical-build-p4n6-54032834-localrest` |
| `build_perarm_m1` | `historical-build-perarm-m1-localrest` |
| `build_perarm_m2` | `historical-build-perarm-m2-localrest` |
| `build_res` | `binary-LOCAL-build_res-157c69a2`, `historical-build-res-localrest` |
| `build_research` | `binary-LOCAL-build_research-c23001b4`, `historical-build-research-localrest` |
| `build_s2_off` | `binary-LOCAL-build_s2_off-4e6bbee3`, `historical-build-s2-off-localrest` |
| `build_s4_50384cf6` | `historical-build-s4-50384cf6-localrest` |
| `build_scratch/buddy-adapter` | `gr1-par2-20260923-buddy-adapter-localrest` |
| `build_scratch/coverage-20261006` | `binary-LEGI200-48d5d3e9`, `binary-LEGI201-5350762f`, `binary-LEGI208-ac1ff537`, `binary-LEGI209-344715b7`, `binary-LEGI209M-8c79a8b6`, `binary-LEGI213-f79ebbfa`, `cov20261006-frozen-extra-localrest` |
| `build_scratch/legacy` | `legacy20261008-instructions-localrest` |
| `build_scratch/optimize-20260927` | `opt20260927-briefs-localrest`, `opt20260927-closeout-localrest`, `opt20260927-p0b-c5-localrest`, `opt20260927-p1b1-localrest`, `opt20260927-p1b3-localrest`, `opt20260927-p4eqv-localrest`, `opt20260927-p4g-localrest`, `opt20260927-p4r2ab-localrest`, `opt20260927-pea-localrest`, `opt20260927-tt49-localrest` |
| `build_scratch/p2b` | `spot-otf-20260909-p2b-scratch-localrest` |
| `build_scratch/stageC` | `gr1-par2-20260923-stagec-scratch-localrest` |
| `build_sprint_diag` | `binary-LOCAL-build_sprint_diag-5381b7d9`, `historical-build-sprint-diag-localrest` |
| `build_sr_candidate` | `binary-LOCAL-build_sr_candidate-436e1a78`, `historical-build-sr-candidate-localrest` |
| `build_sr_selector` | `binary-LOCAL-build_sr_selector-14778225`, `historical-build-sr-selector-localrest` |
| `build_w1_B` | `historical-build-w1-B-localrest` |
| `build_w1_S` | `binary-LOCAL-build_w1_S-f80ff3f6`, `historical-build-w1-S-localrest` |

The deletion plan reports coverage both by entry count and content bytes, packed
archive IDs, regenerable counts, verification results, and publication/commit
conditions. `SAFE_TO_DELETE=no` is mandatory for withheld, changed, unreadable,
unpublished, or unverified evidence. The driver must recheck the original tree
for new or changed files immediately before deletion.
