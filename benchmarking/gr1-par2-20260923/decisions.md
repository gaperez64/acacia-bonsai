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

## 2026-09-24 — P1.4 removes the many-mode regressions; S0 run 2

- tlsf-tools `dbe8c2b`: counter-independent policy cones compiled once across modes, bounded cache
  for counter-dependent-heavy policies. Codex review ACCEPT-WITH-NITS (two test-coverage nits,
  batched for the tlsf-tools PR). Frozen build `tlsf-tools/build-P14-dbe8c2b/` (same command line
  as L0; tlsfcertcheck sha256 `2b2464a2…37a6`, tlsfsolve `512a0521…5502`; the solver changes too
  because it shares `oxidd_build_roots`).
- Formal attribution L0 vs P14, same protocol as P1 (`p1-attribution/l0-vs-p14-20260924.*`): **no
  status disagreement; every target L0 decides is equal or faster under P14.** rru2 n=7 18.18 →
  15.83 s; amba n=15 7.66 → 7.06 s; prioritized n=12 0.46 → 0.32 s; cancel n=9 50.23 → 6.14 s;
  cancel n=10 UNKNOWN → VERIFIED 20.62 s (P1: 51.68 s); lbu2 n=7 UNKNOWN → VERIFIED 1.35 s;
  inpchange n=7 UNKNOWN → VERIFIED 125.66 s (check alone still exceeds the 120 s budget).
- S0 run 2 (L0 tools, instrument at `3ac18759`, timing worktree
  `/home/gperez/GIT-repos/acacia-gr1-par2-timing`, the single retained measurement workspace)
  repeats run 1 within ~1-2% per target. Committed under `s0/raw-L0-run2/` with
  `s0/s0-summary-L0-run2.md`; run directories frozen in `_bm-logs.gr1-par2-s0-run2-L0/`.
  Known wart: a stage censored inside one long native call (buffer n=9 Skolemization) reports
  `≥ 0.000 s` because its persisted elapsed only updates between calls; valid but uninformative.

## 2026-09-24 — First end-to-end measurement: P1.4 tools under the L0 generalizer

- Same 18 targets, same protocol as S0 run 2, tools swapped to `build-P14-dbe8c2b`
  (`s0/raw-P14-e2e/`, `s0/s0-summary-P14-e2e.md`; runs frozen in `_bm-logs.gr1-par2-e2e-P14/`).
  One observation per target; S0 runs 1 and 2 bound L0 repeatability at ~1-2%.
- **Two new 120 s closures:** `arbiter_with_cancel` n=10 UNKNOWN → REALIZABLE 26.48 s (peak 3.58 →
  1.94 GiB); `load_balancer_unreal2` n=7 UNKNOWN → UNREALIZABLE 11.24 s (peak 6.60 → 1.58 GiB).
- **Near-17 s objectives met:** cancel n=8 17.20 → 6.66 s; cancel n=9 55.60 → 11.10 s. Also
  inpchange n=6 52.15 → 24.19 s; lbu2 n=6 5.21 → 2.14 s.
- Unchanged where the generalizer dominates (load_balancer, amba_lock, buffer) — P2's targets —
  and collector_v1 (monitor construction, P4). inpchange n=7 remains UNKNOWN: its check alone
  needs ~126 s.

## 2026-09-24 — P2a end to end (P1.4 tools + native compose + compiled-AIG contexts)

- Generalizer `112eb7fe` with the frozen adapter (sha256 `980aa651…5e5f`, built deterministically:
  identical hash from both trees; frozen in `build_gr1par2-adapter-112eb7fe/` of the timing tree)
  and `build-P14-dbe8c2b`. Evidence `s0/raw-P2a-e2e/`, `s0/s0-summary-P2a-e2e.md`; runs frozen in
  `_bm-logs.gr1-par2-e2e-P2a/`. One observation per target.
- Versus P14 alone: load_balancer n=8 18.21 → 10.42 s (under 17 s), n=9 56.51 → 23.49 s;
  amba_lock n=15 41.66 → 35.90 s; buffer n=9 UNKNOWN → REALIZABLE at 117.73 s — too close to the
  cap to count as robust without repeats. Checker-dominated rows unchanged, as expected.
- Watch item: prioritized_arbiter n=7 1.02 → 1.17 s. A ~0.15 s fixed per-invocation cost (adapter
  load/validation?) would be charged on every routed input in the full corpus; investigate.
- The adapter sidecar binds the absolute source path, so an adapter built in one checkout fails
  validation in another even with identical bytes. Should bind content hashes only (cleanup item).
