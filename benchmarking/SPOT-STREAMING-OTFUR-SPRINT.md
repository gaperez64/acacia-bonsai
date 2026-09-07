# Spot OTF sprint completion

Status: implementation and validation in progress. No new performance or
coverage claim has been established by this completion run.

## Scope and baseline

The September 7 completion request extends `otf.md`'s default-off scope: test
all useful new mechanisms and polarities against the current best configurations,
select a candidate default portfolio from measured evidence, then compare its
actual race with the mainline shipping configurations on all 1,524 SYNTCOMP26
logical entries. This supersedes the original restriction on changing defaults;
it does not waive correctness or admission gates.

The remote default branch is `master`. Its tip was verified over HTTPS and
frozen at `1c028f13490862bfdbed5256d0e2404ee4d74355`. The baseline worktree is
`/tmp/acacia-otf-main-1c028f13`. All four members of its `docker_default` group
have been rebuilt with the release compiler profile, O3, LTO, native TLSF,
the same installed Spot/BuDDy, and their own unchanged options:

- `best_four_arm_contradiction`
- `best_decomp_rank_bucketed_semantic_mona`
- `best_four_arm_bboxtree`
- `best_decomp_mona_any`

Full options and binary hashes are in
`_bm-logs.spot-otf-20260907/main-baselines.json`. The Posets, tlsf-tools, and
benchmark pins agree between mainline and the sprint. The corpus is the existing
materialized `tlsf-corpus`; no benchmark input or dependency is changed.

P7 was recovered without consuming stash
`b37ba53f042b11cd7a991f1fc33e079f75f7fc7a` (`p7-verify`). Its old 43-test result
was a debug build without native TLSF and is historical evidence only.

## Completion checklist

- [x] Restore P7 and freeze/rebuild current mainline shipping baselines.
- [x] Finish provider selection, native worker capture, and sparse frozen control.
- [x] Validate real/formula-unreal eager/lazy transformations and all certificate paths.
- [ ] Run unit/config/version, native TLSF, labelled, frozen, and sanitizer checks.
- [ ] Finish C0/C1 demand, C3/C3s, and real-worker/formula-unreal C4/C5 experiments.
- [ ] Compare candidate arms on discovery and family-held-out cohorts.
- [ ] Select and validate candidate portfolios within four children.
- [ ] Run the final full-corpus comparison against all current shipping configurations.
- [ ] Publish per-instance results, losses, conflicts, PAR-2, CPU/memory, and decisions.

## Protocol

Timing uses one sequential invocation per systemd scope, a 17-second cap,
8 GiB memory, and zero swap. Full closing runs use a single `--caps 17` pass;
staged discovery results do not substitute for that measurement. Gains, losses,
major claimed speedups, and runs above 13.6 seconds receive three alternating
paired repetitions. The 60-second cap is diagnostic only.

No timing campaigns overlap builds or other campaigns. Existing status
exceptions and exact family provenance remain authoritative. An incomplete or
censored graph has no utilization denominator. Fixed-K losses never become
top-level unrealizability answers without the existing worker reduction.

Each hypothesis will receive LAND DEFAULT-OFF ARM / KEEP RESEARCH TOOLING /
STOP / NOT ADMITTED, followed by a separate recommendation about the measured
default configuration. All results below remain pending until executed.

## Validation and frozen cohorts

The debug unit/version run passed all 45 tests. ASan/UBSan with leak detection
passed all eight relevant suites outside the tracing sandbox (LeakSanitizer
cannot operate under ptrace). Follow-up tests cover forced sparse budgets and
verifier failures. The dense and sparse engines agree on 5,000 generated mixed
rank games; corrupt certificates are rejected. Native Mealy, Moore, and strict
TLSF fixtures compare independently launched eager/lazy workers after the real
and formula-unreal transformations. The Python suite without optional extension
modules passed 518 tests, with one skip; additional campaign tests validate
resource accounting and reject resumes with changed binaries or treatments.

`spot-otf-cohorts/manifest.json` freezes discovery and held-out inputs before
new timing results. Discovery has 36 instances from 24 families: the existing
24 seeds plus their committed solved neighbors. The existing frontier selector
found 15 held-out targets from 13 families after excluding every family in all
82 earlier Spot letter measurements. Missing annotations are handled by the
existing status/exception protocol; they are not assumed to be realizable.
