# SYNTCOMP26 source-binding census

Run serially on 2026-09-24 with `eligibility-census.py`, the production
`acacia_lift.runner._resolve_source_request` in exact mode (which calls the
production source binder and the default-seed route guard), and
`/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b`. All 1,524 IDs in
`all.list` were joined to `tlsf-sources.tsv`; no STATUS or verdict field was read.
The pinned submodule is c956847 (same tree as 9212e2b). The table records each
source SHA-256, decision, binding metadata, attempted lowering-tool calls, and
elapsed binding time. `eligible.list` preserves the corpus order.

During prep, separate working-tree edits relocated the 14 pinned capability
templates into `scripts/acacia_lift/data/templates/`. Every relocated template
has the same SHA-256 and the registry's other capability fields match HEAD;
the source binder and route guard are unchanged. The census decisions therefore
apply to the committed package at HEAD 3ee43ec8 as well. Those separate edits
are not part of this campaign prep.

| Decision | IDs | Tool calls | Binding elapsed, total | Median | P90 | P99 | Max |
|---|---:|---|---:|---:|---:|---:|---:|
| eligible | 91 | 92 each | 5.693 s | 0.0486 s | 0.0596 s | 0.3345 s | 0.9515 s |
| decline | 1,433 | 1 × 1,070; 92 × 360; 45 × 3 | 71.828 s | 0.0008 s | 0.0505 s | 0.8748 s | 7.0792 s |

Eligible route kinds: 63 `real-proposal`, 28 `exact-game-both-sides`.
Proof methods: 72 `policy`, 19 `region`.

| Capability | IDs |
|---|---:|
| `amba_decomposed_lock` | 13 |
| `prioritized_arbiter` | 7 |
| `prioritized_arbiter_unreal2` | 11 |
| `arbiter` | 6 |
| `arbiter_with_buffer` | 6 |
| `arbiter_with_cancel` | 6 |
| `load_balancer` | 7 |
| `collector_v1` | 8 |
| `arbiter_on_inpchange` | 3 |
| `load_balancer_unreal2` | 7 |
| `simple_arbiter_with_hints` | 4 |
| `abcg_arbiter` | 3 |
| `round_robin_arbiter_unreal2` | 6 |
| `amba_case_study_unreal` | 4 |

Decline reasons: `unsupported_parameter_signature` 1,070,
`source_not_content_verified_for_capability` 327,
`target_must_exceed_every_seed` 33, `lowering_tool_failed` 3.
The last three are `ltl2dba_theta_pb_300_pe_`,
`ltl2dba_theta_pb_500_pe_`, and `shift_pb_500_pe_`; their tool calls hit the
production five-second timeout after 45 attempted calls. These are declines,
not eligible IDs. The 33 route-guard declines are source-verified capability
members at sizes no larger than their default seeds; a binding-only census
would have reported 124, but the actual wrapper cannot attempt lifting on
those 33. They are excluded from `eligible.list` and still appear in the
full-corpus N leg, falling back to B. The 327 complete unmatched comparisons
explain why the decline cost has a longer tail than the one-call majority.
