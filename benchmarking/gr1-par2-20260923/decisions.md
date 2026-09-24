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

## 2026-09-24 — Integration design decided (from `integration-survey.md`)

- **Wrapper, not harness logic, not C++.** N is `scripts/acacia-lift-portfolio.py`, an
  Acacia-compatible executable the existing coverage runner invokes as its single scoped argv
  (`-T source`). It does source-bound eligibility, runs the lifting route under a sub-budget in
  killable process groups (no nested cgroup), and on anything but a verified decisive answer
  `exec`s the unchanged frozen B (`build_w1_B`, sha256 `398a420b…e4a`, `otf_sparse_formula`) with
  the remaining absolute deadline. Whole-tree wall/`MemoryPeak` accounting is reused unchanged;
  a route sidecar JSON records per-stage attribution. The only campaign change is passing the
  outer absolute deadline and a sidecar path.
- **No cheap pre-probe stage in the first N** (plan §9.2: integrate lifting with unchanged B
  fallback first). A sparse/selector combination is out of scope until this is measured.
- **Eligibility becomes fail-closed and cheap:** `tlsfinfo --parameters` first; bucket capabilities
  by exact parameter-name tuple; non-parametric input declines after one tool call (today 8; an
  unrelated `n`-parametric input can cost up to 92). Final equality check unchanged.
- **Preregistered sub-budget variants (two, not a sweep):** lift gets `cap/3` (N-a: 40 s at 120,
  5.67 s at 17) or `2·cap/3` (N-b: 80 s / 11.33 s); B gets the rest of the absolute deadline.
  Selection on all SYNTCOMP26 instances of the capability families (including those B already
  solves) at both caps; the full corpus is then run for the chosen variant only. No virtual score
  is ever synthesized from L0 wins plus B rows.
- **Order of remaining work:** generalizer items (P2b; P2c orchestration/retry; P3 lifetimes and
  the open warts: censored bound inside native calls, adapter sidecar binding by content hash,
  ~0.15 s per-invocation overhead) → package extraction to `scripts/acacia_lift/` with
  `data/capabilities-v1.json` replacing the runtime reads of `m4-alignment.tsv`,
  `m4-invariant-separability.tsv`, `m4-move-separability.tsv` → wrapper and campaign hook →
  campaigns.
- **Campaign reuse:** B's 120 s and 17 s full-corpus legs (witness-lifting opening, two epochs each,
  same host regime) are reusable as B; ltlsynt's 120/17 legs are reusable only after re-hashing
  `/usr/local/sbin/ltlsynt` against the committed pin; TACAS23 has only a 17 s leg, so a fresh 120 s
  leg is required; N needs both caps.

## 2026-09-24 — P2b end to end: neutral, kept; where the time is now

- `ef2b6393` (owner index, support-restricted projection, checked layout; adapter rebuilt from the
  changed source, sha256 `0043e97b…`) with `build-P14-dbe8c2b`: every target within ~±3% of P2a
  (`s0/raw-P2b-e2e/`, `s0/s0-summary-P2b-e2e.md`, runs in `_bm-logs.gr1-par2-e2e-P2b/`). This is
  the outcome §5A S1 anticipates: once P2a removed the compose/import work the metadata scan was
  not a cost. Kept for the layout's soundness (explicit backend limit, no fixed coordinates).
  **No SIMD or word-mask variant added:** measured supports are 4–39 variables and sorted sparse
  lists were 2.4× faster than masks in the micro-measure; S2/S3 preconditions are not met.
- Remaining dominant phases (P2b): `bdd_projection_relabel` ~60% on load_balancer n=8/9 and amba
  n=15 — BuDDy operation cost, not Python; Skolemization 85% on buffer n=9 and ~32-37% on lb/amba;
  target check 80% on cancel n=10 and ≥89% on inpchange n=7; rru2 n=7 splits target solve/check.
- Next: P2d — split projection/relabel into exist/relabel/cube and use a permutation replace
  (sound only for pure renamings) where the relabel is one. P5 (relational region check instead of
  global Skolemization) meets its §8 trigger on arbiter_with_buffer; decide after P2d.

## 2026-09-24 — P2c end to end: whole-tree memory drops; the per-invocation overhead is gone

- `b123e090` with `build-P14-dbe8c2b` and the unchanged adapter (`0043e97b…`); evidence
  `s0/raw-P2c-e2e/`, `s0/s0-summary-P2c-e2e.md`, runs in `_bm-logs.gr1-par2-e2e-P2c/`.
- **Whole-invocation cgroup peak** drops wherever the generalizer ran, because the supervisor no
  longer holds a BuDDy manager while the child and checker run: load_balancer n=9 1.06 → 0.74 GiB,
  n=8 1.02 → 0.70 GiB; cancel n=8 1.18 → 0.95 GiB; arbiter n=6 936 → 688 MiB; buffer n=8 1.05 →
  0.72 GiB. This is the process-tree measurement §6.4 requires, not a per-process estimate.
- Time: inpchange n=6 23.76 → 17.27 s; cancel n=8 6.31 → 5.10 s; prioritized n=7 1.02 → 0.81 s
  (the P2a watch item is resolved: faster than L0's 1.04 s); others within noise.
- Codex review of P2c found two release-critical invariant violations (source binding outside the
  absolute deadline; child bundle not bound to the parent request), both fixed and re-reviewed
  before commit. The implementer had also bypassed the git shim with `/usr/bin/git commit`; that
  commit was soft-reset and the work reviewed first.
- tlsf-tools: the P5 review found a pre-existing checker soundness bug on `main` (uninitialised
  latch resets read as 1, so a losing game could verify under `--method certificate`). Fixed for
  every method in `4af7bf4`; games from `gr1_monitor_game.py` always have constant resets, so no
  sprint result is affected. Region method `9212e2b`; frozen build `build-P5-9212e2b`
  (tlsfcertcheck `65fd3dd9…`).

## 2026-09-24 — Policy vs region checking end to end; the proof method becomes a capability attribute

- `ba98fb39` + `build-P5-9212e2b` (tlsfcertcheck `65fd3dd9…`, includes the reset fix) + adapter
  `0043e97b…`, 18 targets, `--real-check policy` then `region`, serial, same protocol
  (`s0/raw-P5-{policy,region}-e2e/`, summaries alongside; runs in `_bm-logs.gr1-par2-e2e-P5-*`).
  Policy on the P5 build reproduces P2c within noise (the reset fix costs nothing).
- Region wins where Skolemization dominated: buffer n=9 116.30 → 18.59 s (peak 1.81 → 1.23 GiB),
  buffer n=8 15.21 → 5.37 s, amba n=15 36.61 → 23.62 s. Region loses where the policy check is
  cheap and the joint existential is not: cancel n=8 5.12 → 21.23 s, cancel n=9/n=10 REALIZABLE →
  UNKNOWN, inpchange n=6 17.47 s → UNKNOWN. load_balancer neutral in time, more memory under
  region. UNREAL rows unaffected by construction.
- **Decision (learned development choice, frozen now):** the REAL proof method is an attribute of
  the source-verified capability in the capability data: `region` for `arbiter_with_buffer` and
  `amba_decomposed_lock`, `policy` for every other capability, including all not measured here.
  It is keyed to the verified capability, never to a filename or to a known answer. It was chosen
  on these development observations; the confirmation and closing campaigns are fresh runs and the
  final report discloses this choice as learned. No per-instance or per-n thresholds.

## 2026-09-24 — Correction: source binding uses tlsf-tools, not SyFCo

- The P0b commit message (`b970cbf1`), `integration-survey.md` §4 and earlier notes here describe
  the source-binding check as "SyFCo's expanded basic TLSF and lowered LTL". That is wrong. The
  binding code calls only tlsf-tools utilities from the configured build: `tlsfinfo --parameters`
  and `tlsfinfo` metadata queries, `tlsf2tlsf` / `tlsf2tlsf --basic`, and `tlsf2ltl --format ltl`
  (`benchmarking/param-lift-20260922/request.py`). No SyFCo binary is on the lifting route or in the
  wrapper. SyFCo appears only in the benchmark harness's external legs (TACAS23 and ltlsynt read
  SyFCo-converted `.ltl/.part` pairs). Earlier text is left as written; this entry supersedes it.

## 2026-09-24 — P2d end to end; the per-capability method choice stands

- `17b2b209` (pair replace for injective relabels over actual support; fused and-exist in
  Skolemization) + `build-P5-9212e2b` + adapter `d5ccf8f9…`, policy and region, same protocol
  (`s0/raw-P2d-{policy,region}-e2e/`, summaries; runs in `_bm-logs.gr1-par2-e2e-P2d-*`).
- Policy route vs P5 policy: load_balancer n=8 9.27 → 4.58 s, n=9 23.26 → 10.20 s; amba n=15
  36.61 → 14.47 s; buffer n=8 15.21 → 9.80 s, n=9 116.30 → 97.10 s. Checker-bound rows unchanged.
- Region route: buffer n=8 2.95 s, n=9 9.12 s; amba n=15 12.85 s; still worse than policy on
  load_balancer (7.26 / 17.79 s) and still loses cancel n=9/10 and inpchange n=6.
- The frozen choice is unchanged by the new code: region for arbiter_with_buffer and
  amba_decomposed_lock, policy elsewhere.
- Lifting-route-only times now under 17 s (one observation each, route only, no wrapper or
  fallback): cancel n=8/9, load_balancer n=8/9, amba n=15, buffer n=8/9, lbu2 n=6/7, rru2 n=5/6,
  and inpchange n=6 at 16.45 s (too close to call). Over 17 s: cancel n=10 (24.6 s), rru2 n=7
  (30.9 s). Still open: inpchange n=7 (check alone ~126 s) and collector_v1 (monitor
  construction; P4, not pursued in this sprint — see below).
- **Not pursued, with reasons:** S2 (mode-parallel constant propagation) — after P1.4 no target is
  dominated by repeated per-mode AIG interpretation; S3 (AVX2 popcount) — no path counts long
  masks (S1 chose sorted sparse lists; supports are 4–39 variables); P4 (monitor encoding) —
  conditional second workstream per plan §7, the only affected target in the cohort is
  collector_v1, and it would not change the first PAR-2 campaign.

## 2026-09-24 — Owner decision: the long cap is 60 s, not 120 s

- The owner moved the primary long cap from the plan's 120 s to **60 s** (closer to the TACAS23
  paper's regime and about half the campaign time); **17 s stays** for continuity. This supersedes
  plan §11.2's 120 s wherever it appears below, and the preregistered lift budgets become
  cap/3 = 20 s and 2·cap/3 = 40 s at 60 s (5.67 s / 11.33 s at 17 s).
- **N and TACAS23** are run fresh at 60 s (N is cap-aware through its lift budget; TACAS23 has no
  long-cap leg).
- **B and ltlsynt at 60 s** are derived from their archived 120 s uniform-cap legs
  (witness-lifting opening, epochs 1-2 for B) by censoring: a row counts as solved at 60 s iff it
  was decisive within 60 s in the 120 s run; everything else scores 2 × 60 s. Owner-approved
  (option (a)). This is exact for these tools because neither receives the cap or adapts to it —
  the harness only kills them at the deadline — but it is a derivation, not a 60 s observation, and
  every report that uses these series says so.
- The 120 s evidence already committed (M6, P0 replay, S0/e2e runs) stays as recorded; those runs
  used the 120 s budget and are development observations.

## 2026-09-24 — Source eligibility receives a structural gate and fixed time budget

- The 14 pinned capability signatures now include semantics, target, and declared input/output
  signal shapes, verified against the vendored templates when the registry loads. For a literal
  source `n`, `tlsfinfo` metadata can rule out all candidates before normalization or formula
  lowering. The existing basic TLSF and lowered LTL equality checks remain the admission rule.
- The wrapper passes `min(--eligibility-budget-seconds, 0.05 × cap)` to the source binder; its
  default is 1.0 s, so the 17 s and 60 s caps give 0.85 s and 1.0 s, respectively. A timeout
  kills the active tool process group and declines with `eligibility_budget_exhausted`.
- The serial SYNTCOMP26 rerun preserved the 91 eligible IDs and 33 guard declines. All 1,433
  declines were below 0.85 s in this run; P99 was 0.0172 s and the maximum 0.4055 s. See
  `campaign/eligibility-v2-summary.md` and `campaign/eligibility-v2.tsv`.
- Codex review ACCEPT-WITH-NITS; the reviewer also recomputed the censored 60 s B/ltlsynt scores
  independently and they match. Open nit (reporting only): when the outer deadline is tighter than
  the eligibility budget, the decline reason reads `absolute_deadline_exhausted` and the route
  record stores the configured rather than the effective budget. No admission is affected.
