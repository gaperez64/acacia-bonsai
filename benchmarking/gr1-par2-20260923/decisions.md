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
