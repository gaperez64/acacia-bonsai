# Source-route eligibility census, per-capability switch

The serial `eligibility-census.py` rerun on 2026-09-24 used the same 1,524
SYNTCOMP26 IDs and source hashes as `eligibility-v2.tsv`, the production source
binder and route guard, and pinned tlsf-tools build
`/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b`. The binding budget was
1.0 s. No benchmark verdict or STATUS field was used.

| Decision | IDs | Tool calls per ID | Total binding time | Median | P90 | P99 | Max |
|---|---:|---|---:|---:|---:|---:|---:|
| Route-eligible | 72 | 18 × 28, 24 × 32, 30 × 12 | 0.970 s | 0.0110 s | 0.0190 s | 0.0492 s | 0.1220 s |
| Decline | 1,452 | 1 × 1,070, 5 × 317, 18 × 15, 24 × 7, 30 × 43 | 3.574 s | 0.0008 s | 0.0037 s | 0.0168 s | 0.3584 s |

The route-eligible set is exactly the v2 eligible set minus 19 members of the
four disabled capabilities: `collector_v1` (8), `abcg_arbiter` (3),
`simple_arbiter_with_hints` (4), and `amba_case_study_unreal` (4). This verifies
the expected **91 − 19 = 72** count. No other ID changed its eligible/decline
decision. The disabled switch also changes the reason for seven old seed-guard
declines, leaving 26 with `target_must_exceed_every_seed`.

Decline reasons are `unsupported_parameter_signature` (1,070),
`source_not_content_verified_for_capability` (317),
`capability_route_disabled` (39), and `target_must_exceed_every_seed` (26).
Of the 39 disabled-route declines, 19 were previously eligible, seven were
previously seed-guarded, and 13 were already nonmembers that share a disabled
capability's structural signature. These 13 conservatively fall back to B;
their binding evidence says `structural-prefilter`, not content-verified. The
early structural gate handled 32 of the 39 in five tlsf-tools calls each. The
other seven required full binding. No ID exhausted the eligibility budget.

Validation after the timed measurements reported `DONE`: the runnable
`tests/pytest` suite passed (944 tests, eight subtests; four skipped), and the
benchmarking regressions passed (68 tests, 41 subtests; three skipped). The
unfiltered Python suite cannot collect two API modules because the
`acacia_boomslang` extension is absent in this checkout; the runnable suite
used `PYTHONPATH=benchmarking` for its `benchlib` dependency. Ruff passes on
every changed Python file. Repository-wide ruff reports 228 existing
violations outside those files.
