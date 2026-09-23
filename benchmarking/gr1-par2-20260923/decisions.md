# GR(1) lifting → PAR-2 sprint: decisions log

Plan: [`plan.md`](plan.md) (the revised review of 2026-09-23, copied verbatim). This file records
every judgment call and measurement in order. Numbers are carried forward verbatim.

## 2026-09-23 — start

- Branch `sprint/gr1-par2-20260923` stacked on PR #191 (`651a0741`), which is stacked on PR #190.
- Open PRs refreshed once (`gh pr list`, all pages): Acacia #190 `sprint/witness-lifting-20260918`
  @ `9f490d7e` (base master), #191 `sprint/param-lift-20260922` @ `651a0741` (base #190). tlsf-tools:
  none open. Acacia `origin/master` @ `50384cf6`.
- tlsf-tools `origin/main` has moved past the plan's pin: `6b2507f` (merge of #36, "Support bad-state
  properties in GR(1) games") on top of `2f123f9` (#35). Sibling checkout fast-forwarded; the Acacia
  submodule is bumped from `f2130939` directly to `6b2507f`. #36 is inside the dependency repair, so
  L0 is defined at `6b2507f`, not `2f123f9`.
- The pre-existing `subprojects/tlsf-tools/build-oxidd/` binaries (dated 2026-09-23 14:11, built from
  the uncommitted `4793add` submodule state) are kept untouched as the historical M6 tool state.

## 2026-09-23 — P−1 worktree retirement: audited, not executed

- Audit and dry-run plan in `/home/gperez/acacia-worktree-retirement-20260923/` (outside every
  candidate): 11 keep, 8 admitted for removal (~881 MiB), 6 defer (four only for an unclassified
  `.pytest_cache`; two `/tmp` entries whose paths are already gone — stale metadata only).
- Only one admitted tree needs an archive ref (`acacia-wt-candidate`, detached `fe249294`); the other
  seven sit on branches that stay.
- Execution was refused by the session's permission policy; nothing was removed and no ref created.
  Held for the owner. Nothing downstream depends on it (no new worktrees are being created).
- `build-L0` (tlsf-tools at `6b2507f`, same Meson options as `build-oxidd`) is frozen read-only.

## 2026-09-23 — P0: the repaired pin reproduces every M6 closure

- Replay against `build-L0` (tlsf-tools `6b2507f`) at Acacia `b970cbf1`, reproducer request mode,
  120 s budget, one `systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0` per row, serial,
  quiet machine: **26/26 decisive M6 rows verified (22 REAL + 4 UNREAL), 4/4 controls verified**.
  Driver wall total over the 26 rows: **357.044 s** (M6 reported 396.227 s on the decisive rows;
  different host epoch and driver, so this is a reproduction, not a speed claim).
- Per-row table: `p0-replay/replay-L0.tsv`; evidence JSON per row under `p0-replay/evidence/`.
  Run directories (games, certificates, policies; 203 MB) are frozen read-only in the gitignored
  `_bm-logs.gr1-par2-p0-replay-L0/`; they are the frozen target artifacts for checker attribution.
- The historical M6 rows are not overwritten; they remain measurements of the uncommitted
  `4793add` submodule state.
- Near-cap rows for later recovery targets (L0 wall): `arbiter_with_cancel` n=9 55.2 s,
  `load_balancer` n=9 55.9 s, `arbiter_on_inpchange` n=6 51.5 s, `amba_decomposed_lock` n=15
  40.3 s, `round_robin_arbiter_unreal2` n=7 33.7 s.

## 2026-09-23 — P1 landed on tlsf-tools `gr1-par2-checker`

- Commits `7136067` (selected roots via `oxidd_build_roots`), `079e4aa` (per-mode constant
  specialisation before compiling the policy), `0660e72` (per-mode substitution and successor-image
  reuse), `0435385` (review nits: skip unused environment `system_winning`; cache-free rebuild
  oracle). Codex review: ACCEPT-WITH-NITS on steps 1 and 3, ACCEPT on step 2; nits fixed. The
  reviewer's differential against the L0 checker found no status disagreement on any fixture or
  mutation.
- Frozen treatment build `tlsf-tools/build-P1-0435385/` (read-only): same Meson command line as
  `build-L0` (`buildtype=release oxidd=enabled research_tools=true`), only `tlsfcertcheck` and
  `tlsfsolve` compiled, one job. sha256 tlsfcertcheck `b43dcec2…63c3`, tlsfsolve `f30cc2e6…a8`.
  Version string reads `0435385-dirty` because of an unrelated untracked note in the sibling
  checkout; `simd=scalar` (no ISA tier enabled in this profile — S0 inventory item).
- tlsf-tools suite: 284/285 in the sibling clone; the one failure is environmental
  (`gr1_monitor_game` resolves Acacia's `m0-census.py` through a relative path that only exists in
  the submodule layout).

## 2026-09-24 — P1 checker attribution on frozen target artifacts

- Harness `p1-attribution/run.py`; 33 deduplicated target triples from the frozen P0 replay and S0
  run 1; L0 (`build-L0`) vs P1 (`build-P1-0435385`) `tlsfcertcheck`, exact generalizer argv, cold,
  one cgroup scope each (8 GiB, no swap), 300 s timeout, 2 rounds ABBA, serial, quiet machine.
  Raw rows `p1-attribution/l0-vs-p1-20260924.tsv`, summary `.json`.
- **No status disagreement where both decide.** Three L0 capacity UNKNOWNs now VERIFY:
  `load_balancer_unreal2` n=7 (28.69 s UNKNOWN → 1.36 s; peak 3.26 → 1.38 GiB),
  `arbiter_with_cancel` n=10 (10.68 s UNKNOWN → 51.68 s), `arbiter_on_inpchange` n=7
  (13.12 s UNKNOWN → 124.29 s, check alone exceeds the 120 s invocation budget).
- Medians where the checker dominated: cancel n=9 50.20 → 6.77 s; cancel n=8 12.52 → 2.62 s;
  inpchange n=6 36.11 → 12.35 s; lbu2 n=6 3.49 → 0.44 s (peak 1.80 → 1.28 GiB).
- Regressions: `round_robin_arbiter_unreal2` n=7 17.99 → 19.97 s (+11%), `amba_decomposed_lock` n=15
  7.75 → 8.84 s (+14%), `prioritized_arbiter` n=12 0.46 → 0.77 s (+70%). Two rounds only; LTO
  placement noise is ~1-4%, so these look real. Hypothesis: per-mode compilation rebuilds
  counter-independent cones once per mode. Sent to a P1.4 follow-up rather than accepted.
- S0 run 1 (instrument before review fixes) breakdown: target check dominates cancel, inpchange,
  lbu2, rru2 (P1); `substitute_variables` dominates load_balancer (61.6% at n=9) with projection/
  relabel second (P2 §5.1, S1); Skolemization dominates arbiter_with_buffer; monitor construction
  dominates collector_v1 (P4, conditional). Run 1 is frozen at `_bm-logs.gr1-par2-s0-run1/` with its
  known flaws (mode count; censored bounds) and is not committed as evidence.
