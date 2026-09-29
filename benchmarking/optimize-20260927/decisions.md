# Sprint decisions — optimization first, lean repository, then UNREAL lifting (2026-09-27)

This is the single active record for the sprint whose plan is `plan.md`. It supersedes the
follow-up handoffs in `../gr1-par2-20260923/`. Agent briefs and review rounds are kept outside Git
(`build_scratch/optimize-20260927/`); only decisions and their evidence pointers are recorded here.

## 2026-09-27 — Kickoff facts

- **Branch.** `sprint/optimize-20260927`, stacked on PR #192
  (`sprint/gr1-par2-20260923` at `26cbb45f`), which stays open and unmerged. The stack is
  #190 → #191 → #192.
- **tlsf-tools.** At `a294419` (main, #41 merged). OxiDD is `be2f69b` plus the patch submitted
  upstream as OxiDD#49, which is still open.
- **E5 (evaluation invocation, not a shipped default).**
  - Arms, in this order: `real:small:backward`, `real:small:forward`,
    `unreal:formula:spot-guarded-sparse`, `unreal:automaton:forward`, `real:gr1:oxidd`.
  - Frozen binary `build_final_f74ad9d1/src/acacia-bonsai`, SHA-256 `65530fb4…c462`.
  - No completed full-corpus E5 result exists. The 1,238 figure is a virtual best of isolated
    60 s legs.
- **S4.** The shipping `docker_default` group, unchanged.
- **Open issues in scope:** #194 (optimise native arms), #195 (UNREAL lifting), #196 (sparse
  backward).

## 2026-09-27 — P0a audit, and the sealed mapping was never sealed

- **Audit results** (full report outside Git, `build_scratch/optimize-20260927/p0a-audit/`):
  - **Tracked at the stack head:** 100.2 MB, of which `benchmarking/` is 84.6 MB and
    `gr1-par2-20260923` 59.3 MB.
  - **Stack history (master..#192):** 3,069 new objects, 83.5 MB raw, but only about 7.0 MB as a
    standalone pack. Checkout clutter and continued growth are the problem, not clone size.
  - **Candidate evidence bundles:** 58 tracked bundles, 81.5 MB of member bytes, 10.9 MB as
    deterministic .tar.gz.
  - **Local-only cited material:** the 1.26 GB `_bm-logs.20260916-demand-sparse` splits into
    traces, rows/logs and binaries, about 87 MB compressed. There are 12 of 58 local binaries
    whose SHA-256 is cited in committed records.
- **Protocol breach, found by the audit.** The obfuscation "sealed" mapping
  (`gr1-par2-20260923/campaign/generic-selection/sealed-mapping.jsonl`) was committed on
  2026-09-24 in `eca0be93` and pushed with the #192 branch. It has therefore been public, and
  readable by the coding agents, throughout.
  - There is no evidence of misuse: the join script refused early reads, and the hardcoding guard
    scans solver sources for names and corpus paths.
  - But the old obfuscated corpus can no longer serve as a holdout.
- **Handling.**
  1. All ten `generic-selection` files are excluded from published bundles and removed from the
     tree on this branch. They remain in #192's history until the owner retires that branch; no
     rewrite without approval.
  2. For P4, a fresh obfuscated corpus is generated with an owner-held seed. Its mapping never
     enters the repository or any agent-readable directory, and the owner supplies it only at the
     join.

## 2026-09-27 — Evidence published, read back and restore-tested (P0a)

- **33 deterministic archives, 122 MB compressed:**
  - one per campaign directory; each `plots/<dir>`; the loose root files and historical
    narratives (`benchmarking-root-legacy`, partial); the stopped per-arm campaign
    (`gr1-par2-20260923-perarm-m1`, status `stopped`); the `s0` diagnostics; the earlier N
    selection;
  - five local-only cited supplements (the demand-sparse traces, rows/logs and binaries; the
    15 Sep closing log; the ltlsynt route directory);
  - seven frozen binaries (E5, pre-E5, M1, M2, G1, B, TACAS23 v1).
- **Coverage.** All 2,306 tracked `benchmarking/` files are either in a bundle (2,209) or have a
  stated keep-in-Git reason (97). Nothing on the do-not-publish list is in any archive. Five
  tools/briefs tripped the mapping-content guard and stay in Git as tools. The 232 symlinks in
  the ltlsynt route directory point at third-party corpus bytes and were not copied.
- **Backup.** An independent copy, verified, is in `~/acacia-evidence-backup/2026-09-27/`
  outside the repository.
- **Publication.** Release `evidence-2026-09-27`: a prerelease, not latest (v2.4.3 stays
  latest).
  - Its tag points at an orphan README-only commit `40d292e6` with no workflows and no ancestors.
    Three workflows (docker-deploy, docker-boomslang, wheels→PyPI) fire on
    `release: published`, prereleases included, and GitHub runs them from the tagged commit's
    workflow files. A tag at master would have published bogus images and a PyPI version.
  - Nothing was triggered.
- **Read-back.**
  - All 33 assets were fetched from their public URLs with `acacia-evidence.py fetch`, which
    checks the outer digest, the provenance against the index, and every member before and
    after a safe extraction: 33/33 verified.
  - The seven restored binaries hash to their pins.
  - `restore-test.sh` on the downloaded bytes: 12/12 PASS.
    - The seven per-arm `export-cactus` outputs are byte-identical.
    - perarm-select reproduces {1,2,3,4,5} 1,238 / 36,260.1 s, {1,2,3,4} 1,217 / 38,787.2 s
      and {1,2,3,5} 1,225 / 38,005.8 s.
    - `plots/three-way-full-20260905` ltlsynt gives 1,257 / 9,515.826 s.
    - `witness-lifting-20260918` opening gives 1,178 / 12,543.332 s.
- **Index.** `benchmarking/evidence-index.tsv` holds 33 write-once rows. The next step untracks
  the archived bulk from HEAD.

## 2026-09-27 — Bulk evidence off HEAD; living docs; worktrees retired (P0a done, part of P0b)

- **HEAD shrinks from 100.3 MB to 17.5 MB tracked.** 2,139 files were removed: 2,129 are in
  indexed, restore-tested archives and 10 are the obfuscation linkage files, deliberately not
  archived. Ten reusable campaign tools moved to `benchmarking/tools/` and take fetched evidence
  through explicit arguments.
  - One-off entrypoints are removed or made to require explicit inputs.
  - Generated outputs go to ignored directories.
  - Source comments cite archive IDs.
- **What this does not do.** It reduces HEAD only. The removed bytes remain reachable in the
  history of #190–#192; the clean-landing proposal and the owner's later branch retirement deal
  with that.
- **Living docs.** One current report, `benchmarking/RESULTS.md`: the latest released three-way,
  the E5 virtual best, the per-arm legs, and rejected ideas pointing at archive IDs.
  `CLAUDE.md` and `benchmarking/README.md` now define durable evidence as a verified external
  archive plus an index row.
- **Growth guard** on 26cbb45f..HEAD: PASS, after fixing a false positive that classed the tool
  `thermal-annotate.py` as thermal data. The owner chose CI only, with no git hook.
- **Oracle regression found and fixed.** 12 failures in `test_acacia_lift_online.py` came from the
  oracle defaulting to a stale sibling tlsf-tools build (`f213093`) whose `tlsf2tlsf` lacks
  frontend provenance.
  - `scripts/build-oracle-toolchain.sh` now builds the oracle's CLIs from Acacia's own
    submodule, pinned and OxiDD-patch-verified, into ignored `build_oracle_tlsf/`.
  - The oracle refuses a stale toolchain.
  - Results: 17/17; pytest 976 passed and 4 skipped (the Python 3.14 binding modules are an
    environment limit); unit 50/50.
- **Worktrees: 16 → 9.**
  - `acacia-wt-candidate2`: its unique commit `f7919b83` was bundled and verified, and its pinned
    build moved (hash re-checked) to `~/GIT-repos/acacia-worktree-retirement-20260927/`; then it
    was removed.
  - Five clean worktrees with uncited caches were removed without force: `c-selector`,
    `ds-report`, `legacyfix`, `master-c4e0eb80`, `pairs-error-policy`.
  - The two stale `/tmp` registrations were pruned.
  - Eight are kept: dirty, unmerged, pinned binaries, or the timing tree.
  - No branch was deleted.

## 2026-09-27 — Spot upgrade deferred to the P2b acceptance-limit probe

- **Owner's question.** The owner asked whether rebuilding Spot (newer version, more acceptance
  sets) would fix the local Python binding gap.
- **Answer: only in part.** The system Python moved to 3.14. Spot's Python module exists only as
  the 3.13 build from the owner's fork, and Acacia's own binding was never built for 3.14. A new
  Spot fixes the first but not the second. CI already runs those modules; the oracle uses the
  3.13 bindings by explicit path.
- **Owner decision: carry on with the plan.** The upgrade belongs to P2b §6.5.
- **Constraint found for P2b.**
  - Every frozen binary (E5, M1, M2, the others) and the installed `ltlsynt` link `libspot.so.0`
    and `libbddx.so.0` dynamically from `/usr/local/lib`.
  - That install is the owner's fork `spot-goodset`: upstream 2.15.1.dev (2026-06-20) plus
    goodset-splitter commits, configured with `--enable-devel --enable-max-accsets=64`.
  - Replacing it would silently change every baseline, because a different MAX_ACCSETS changes
    the acceptance-mark layout (an ABI break).
  - So the probe uses a separate prefix (`~/opt/spot-2.16-acc<N>`, no sudo) and is measured on
    its own: C5 rebuilt against it versus C5.
  - The Spot 2.16 tarball is kept at `~/opt/src/spot-2.16.tar.gz`, SHA-256 `688463cb…ab869`.
- **Incident.** A build started just before the owner's rejection arrived. It aborted when its
  source tree was removed. Nothing was installed, and `/usr/local` was untouched.

## 2026-09-27 — P0b source cleanup accepted; C5 frozen

- **Refactor.**
  - Native arm orchestration moves out of headers into `native_gr1_arm.cc`,
    `native_param_lift_arm.cc`, `native_proof_binding.cc` and `native_support.{hh,cc}`, with
    shared RAII result owners and diagnostics.
  - Test hooks move to `native_test_hooks.cc`, linked only into the debug native test
    executable.
  - Portfolio arm parsing, child launch and dispatch are separate functions, with the lifecycle
    unchanged.
- **Review (`p0b-src/REVIEW.md`): ACCEPT WITH FIXES.**
  - It walked the old and new lifecycle side by side: fork and arm order, process groups, the
    absolute deadline, SIGKILL, non-blocking reaping, late-answer rejection and signalled
    children are all preserved.
  - Proof binding and hook isolation are unchanged; release `nm` and `strings` confirm the hooks
    are absent.
  - Fixes applied: the four tracked `build_scratch/stageC` files, including two review notes found
    in no archive, were published as the new asset `gr1-par2-20260923-stagec-scratch` (index row,
    read back byte-identical) before removal, and the Spot decision is kept out of the source
    commit.
- **Measurements, C5 versus pre-cleanup and E5.**
  - Paired 60 s screen on 70 instances (the stratified 50 plus 20 random, fixed seed), serial,
    each run in an 8 GiB no-swap scope: all 70 verdict triples identical; median C5/pre 1.0001
    and C5/E5 1.0000. The two cases over 5% repeat at 0.98 and 1.03.
  - `.text` −6,944 bytes.
  - Clean build +8.1% (188.7 → 204.0 s), from 14 compile units instead of 8.
  - Unit tests 56/56 native and 52/52 non-native; pytest 977 passed.
- **C5** is frozen from committed source `1b9e4b5f` (E5 build arguments, native arms on) and
  registered in `benchmarking/baselines.tsv`.

## 2026-09-28 — P1a: phase records, development panel, baseline attribution

- **Instrumentation.**
  - Opt-in per-phase records (`ACACIA_PHASE_RECORDS`) in the native arms.
  - tlsf-tools stage statistics through new entry points (tlsf-tools#43, `a5897d5`, which the
    submodule now points at). The existing option structs are unchanged, and a guard-page test
    compiled against the old headers proves it.
  - Records flow through a non-blocking pipe to one writer process forked before the race, so a
    stalled or failing destination cannot delay or change a verdict. With records off, nothing is
    created and every path is identical.
- **Review took four rounds.** It rejected:
  - an ABI break (fields appended to public structs);
  - a behaviour change with records off (early frees, now split out as a separate candidate);
  - synchronous record I/O that could block or kill a worker;
  - CPU medians computed with missing values as zeros;
  - a weak ABI test;
  - regular-file stalls;
  - a profile measured on a pre-final binary.

  All were fixed, including a wait-loop regression introduced and caught during the fixes.
  Final binary `01faa6da`: records-off matches C5 on 10/10 verdicts and exit codes (median 1.006);
  all 171 profile rows were rerun on it, verdicts and timeouts are unchanged, and the largest
  category median moved by 0.22 s.
- **Panels.** `dev-panel.tsv` (57) and `heldout-panel.tsv` (20, disjoint) come from the archived
  legs by outcome, time and resource rules. The memory-heavy stratum is selected by archived
  process RSS, which is disclosed. No names.
- **Attribution** (60 s, standalone and E5):
  - Direct REAL successes (8): independent check 37.8 s against solve 2.3 s. The checker, not
    the solver, dominates.
  - Heavy unsuccessful cases: direct monitor construction 219.5 core-s; lift target reduction
    173.7 core-s. Peaks reach 4.9 GiB per child, and one E5 case hit the 8 GiB scope.
  - REAL lift successes (19): policy export 80.8 s (one case 47.0 s), internal check 18.5 s,
    outer check 18.1 s (7.1 s on the unique success), re-reduction 0.36 s.
  - Opposite-polarity export (median 0.29 ms) and parent frontend conversion (median 0.61 ms)
    are negligible.
- **Ranked P1b packages:**
  1. generic work and memory budgets for monitor and target-game construction (§5.3/5.5);
  2. no duplicate final lift check, via a trusted source-bound target context (§5.4);
  3. profile and streamline the direct checker (§5.2/5.5);
  4. lift policy export and candidate size (§5.3/5.5);
  5. copies and owner lifetimes, i.e. the early frees (§5.2).

  Opposite-polarity export and moving the frontend are deprioritised.

## 2026-09-28 — P1b package 1: construction budgets (admitted)

- **What it adds.** Budgeted tlsf-tools entry points (tlsf-tools#44 at `7afc490`, stacked on
  #43) with global limits:
  - structural precheck: 4,096 formula nodes, 2,048 APs, 1,024 conjuncts, 1,024 nodes per
    conjunct, depth 16, and an estimate of 2^24 states;
  - Spot aborter: 10,000 monitor states and 150,000 edges;
  - soft RSS: half the invocation limit divided by the active arm count, i.e. 4 GiB standalone
    and 819.2 MiB per arm in E5, sampled in every lifting phase.

  A decline is an UNKNOWN with a stage and work counts, never a verdict. Through the old API,
  `std::bad_alloc` now reports LIMIT instead of ERROR; this is documented in the PR.
- **Tuning disclosure.** The limits were chosen on the development panel so that all its
  successes survive: outcome-informed tuning in aggregate, with no names or instances. The
  held-out panel triggers no budget decline.
- **Results.**
  - Zero lost solves: dev direct 24/24, dev lift 19/19, held-out 4/4 and 0/0, E5 subset 44/44.
  - Failed-construction CPU (paired observed rows): direct 341.8 → 271.1 core-s (−21%), lift
    238.4 → 162.8 core-s (−32%), mostly from three early structural declines.
  - E5 portfolio wall time: unchanged within noise.
- **Review (`p1b1/REVIEW.md`): ACCEPT WITH FIXES.** The lift RSS budget now covers every phase;
  aborter stops report budget stages; forced tests exist for both.
- **Limitation, and the P2 lead.** E5's one MEMOUT (8 GiB, `robot_grid_pb_8_8_pe_`) is
  unchanged: the GR(1) arm declines there in 17.7 ms at about 28 MiB. Standalone evidence
  points at `real:small:forward` during solving as the main consumer; `real:small:backward`
  also reaches the cap alone.

## 2026-09-28 — P1b package 3: faster independent checker (admitted)

- **Change** (tlsf-tools#46, `439c974`, stacked on main `112058b`). For certificate checks whose
  policy circuit has 4,096–262,144 AND gates, the independent checker orders its BDD variables
  inputs-first (environment and system inputs, then interleaved current and next state).
  - It is a pure permutation, chosen from the method and the policy size only, with every
    obligation computed on the same functions.
  - If the reordered pass fails, the checker retries on a fresh manager in the old order within
    the same deadline.
  - Opt-in phase timers are added.
- **Soundness evidence.**
  - Old and new checkers agree on exit code, verdict and every diagnostic line: 59
    artifacts/mutations (30 in the reordered window), and 1,229 oracle replays, which are all
    outside the window.
  - The review added 11 in-window mutations, including padded environment certificates; all
    were handled identically.
  - The review confirmed every obligation is unchanged.
- **Speed.**
  - Scratch candidate, summed check time over three paired repeats: 8 direct successes
    115.2 → 9.6 s (12.1×); 11 E5 GR(1) solves 74.5 → 5.5 s (13.5×).
  - Live tree against frozen C5, whole-invocation wall time: 6.6× (direct successes) and 5.7×
    (E5 GR(1) solves). All 24 C5 dev solves are kept, and all 57 outcomes match the candidate.
- **Validation.** Unit 56/56 and 52/52; release native 4/4; pytest 978 passed after committing
  the new pin (20 failures before were the oracle's provenance guard refusing a pin mismatch,
  as designed); tlsf-tools CI 11/11 on #46.
- **Found on tlsf-tools main, not caused by #46.** `native_reduction_differential` fails locally
  on the fixture `undeclared_at_atom` from tlsf-tools #42: the Python reference throws a Spot
  parse exception on `ack@1` instead of rejecting it. CI skips the test, lacking Spot Python.
  A separate fix is in progress on `fix-differential-at-atom`.
- **Provenance slips during integration, both caught.** The codex measured a scratch copy of the
  tree, not the live submodule; revalidated live. A stale `checker-speed` ref briefly made the
  first push a no-op; the commit was pushed by hash as a fast-forward.

## 2026-09-28 — P1b packages 4–6: lift export rejected, fixed lift budgets admitted

- **Packages 4 and 5 (P1b-4/5): rejected.**
  - Package 4 cut `amba_decomposed_lock_pb_14_pe_`'s policy export from 47.2 to 2.7 s, but it
    also changed proof selection on `_15` (region 2.75 s → policy 22.2 s) and raised the total.
  - Package 5, early owner release: no measurable gain.
  - Tracked in #199.
- **Regression found in that run.** The #46 checker order cost lifting `_13` and `_14`:
  1.3 s / 3.5 s before, timeouts after. Both are solved by the default arms, so no portfolio loses
  them, and the unique `_15` was kept. Accepted per owner option (a), with evidence on
  tlsf-tools#48. Lesson: P1b-3's validation measured the direct arm only; a shared-checker
  change must be validated on every arm that uses the checker.
- **Deferred:** package 2, the duplicate final lift check → #198. P1 status posted on #194.
- **Owner finding: lifting depended on the competition cap.** It sized its policy proof at 75%
  of the remaining time and had a discovery share. Owner decision: fixed per-run budgets.
  - **P1b-6 step A** (tlsf-tools#50, `bb21392`): 24 seed probes; 2 M BDD operations each for
    discovery and policy export; 12 s for policy export plus the internal proof. The deadline is
    a hard stop only.
  - Values are outcome-informed on the dev panel and disclosed. The held-out panel has no
    lifting success, so it cannot rule out overfitting.
  - The 17 s/60 s cap-independence check passes on 51 dev and 17 held-out rows, comparing
    outcome, decision, decline stage and every work count.
  - Dev lifting 17 → 19 successes, none lost, `_15` kept; `_13` and `_14` return via the bounded
    region fallback; wall time −122 s.
  - `benchmarking/tools/check-cap-independence.py` is kept for validating P4's derived 17 s
    series.
  - **Step B, the import memo only:** rejected, with a 0.2 s gain once the budgets are fixed.
- **Also fixed:** a tlsf-tools test read Acacia parent files (`m0-census.py` and corpus files).
  It now uses vendored fixtures.
- **Owner: tlsf-tools API changes freely.** It is in A/B testing with Acacia as its only
  consumer. The accumulated `_with_*`/`_v2` variants and ABI-guard tests are to be collapsed
  into one API per operation (tlsf-tools#49), scheduled after P3 so the lifting API settles once.

## 2026-09-29 — P2a: action-table borrow admitted, picker scratch rejected, E5 memory lead → #200

- **Package 1, the action-table copy: admitted.**
  - The backward solver now borrows `actioner.actions ()` instead of copying the nested table;
    the forward solver already did.
  - Both actioners own the table for the whole solve. The picker only splices inner lists,
    and the actioner never rereads its table after construction.
  - Peak RSS saving: 81.9 MB on `11.ltl` and 71 MB on each `Morning` solve, with 803,077
    fewer `malloc` calls. Wall time is inside noise.
  - Verdicts and full solver traces (06/07/09/11.ltl) are identical. There are no opposing
    conclusive verdicts across 15 panel legs; the three cap transitions were noise in two
    reversed-order repeats.
  - The trace identity covers E5's backward configuration, not the other `docker_default`
    presets. Their actioner contract was audited instead.
- **Package 2, critical-picker scratch: rejected.** It saved 51 allocations with no change in
  peak memory. The +8.06 s over five legs (0.12%) is single-pass noise, not a proven
  regression. No issue.
- **E5's only MEMOUT, `robot_grid_pb_8_8_pe_`, is unchanged by both packages.**
  - RSS rises from 136 MB to 8 GiB within 2.5 s during the MONA IO decoded transition-set
    expansion (`src/ios_precomputers/mona.hh`), before action construction.
  - Filed as #200: bound or stream the decoded sets. It is separate from #196.
- **Validation:** debug unit suites 56/56 and 52/52. Synthesis was bounded at 900 s: 117
  passed, 25 timed out per test, none failed.
  - The coder's 17 pytest failures were environmental: a stale `build_oracle_tlsf` at
    `84276bf` and a scratch launcher.
  - The oracle was rebuilt from the pin with `scripts/build-oracle-toolchain.sh`; full pytest
    then passed with 1,017.
- **Found for P2b §6.5: the installed Spot is a developer build.** It is compiled at `-g -O`
  (`--enable-devel`, automatic for `.dev` versions), so the acceptance-limit probe must
  separate three factors: version, optimization level and acceptance-set limit.
- **Track B started:** P3 U0 on its own worktree (`trackb/unreal-lift`), coded by a second
  agent. Builds are gated on a timed-run marker; codex reviews its work and the driver commits.

## 2026-09-29 — P2b-1: sparse backward vectors stopped at the census (#196)

- **The go rule was fixed in advance:**
  - the comparison-work-weighted non-bottom fraction is at most 0.25; and
  - a modeled sorted-pair read count is at most 0.5× dense.
- **Measured:**
  - E5's backward arm on the P2a panel, with a diagnostics-only build and 16 eligible rows;
  - a non-bottom fraction of 0.67: every eligible row is above 0.25, the lowest at 0.36;
  - modeled sparse reads at 2.56× dense, with mixed-comparison overhead set optimistically
    to zero.
- **STOP, confirmed by a codex review.** Both conditions fail by a wide margin; backward
  generators start at the safe ceiling and stay dense.
- **Evidence:** the census counters are reverted and kept as patches in the sprint evidence
  (`p2b1/census-counters/`). The results are posted on #196, which is left open for the
  owner.
- **SIMD gate (one `perf` workload):**
  - the indexed predecessor loop is about 21% of cycles, which does not fit lane-wise SIMD;
  - dominance is about 15% and is already compiled to 16-lane `vpcmpleb`;
  - the scalar rank sum in insertion is about 14%, and ranks are recomputed where they are
    already known.

  This leads to P2b-3: rank reuse first, SIMD only if residual rank work stays hot.
  - Gate: at least a 5% aggregate backward-arm CPU reduction, repeatable in reversed order,
    with exact verdict identity and no E5 regression.
- **P2b-2 (Spot limit probe) is running in parallel.**
  - It uses a clean worktree at `36e9b4fb` under `build_scratch/` and Spot 2.16 release
    builds in `~/opt` with limits of 64 and 256.
  - Four specs hit the 64-set limit in both UNREAL legacy arms: `robot_grid_pb_8_8_pe_` and
    `prioritized_arbiter_unreal2_pb_{30,60,100}_pe_`.

## 2026-09-29 — P2b-3: rank reuse stopped at its profile gate; no SIMD package

- **Gate:** rank computation must be at least about 10% of the backward arm's cycles,
  aggregated over the rows, before any code is written.
- **Profile:** `perf record`, `cycles:u`, of E5's backward arm on five eligible P2a-panel rows.
  - The rule took every eligible 1–5 s row that had comparison work, which gave four rows.
    A fifth was then added, disclosed, as a sensitivity extension: the shortest eligible
    conclusive row above 5 s.
  - Rank share per row: 0%, 8.2%, 1.2%, 15.4% and 6.0%.
  - Cycle-weighted: 5.77%, or 5.0% without the added fifth row.
- **STOP** for both steps: rank reuse, and the SIMD reduction behind it. The earlier 14% came
  from a single workload.
- **Outcome for P2 §6.3 and §6.4:** no sparse and no SIMD change is admitted. Dominance is
  already vectorized, and the predecessor loop is irregular.

## 2026-09-29 — P2b-2: Spot acceptance-limit probe stopped (#201); measurement Spot ≠ shipped Spot

- **The limit.** Four specs fail both UNREAL legacy arms within 0.1 s on the 64-set limit.
  - It is raised inside Spot's LTL→TGBA translation: one acceptance set per distinct promise.
  - Needed: 65 promises for `robot_grid_pb_8_8_pe_`; 467, 1,832 and 5,052 for the three
    `prioritized_arbiter_unreal2` sizes.
  - With Spot 2.16 release builds in separate prefixes (`~/opt/spot-2.16-acc{64,256}`), acc256
    turns `robot_grid` into a TIMEOUT, and the arbiters still exceed the limit.
  - A diagnostic acc5056 build cannot even translate the size-30 arbiter formula in 60 s.
  - A global increase would widen every acceptance mark (79× at 5,056).
  - **STOP**, a measured negative result.
  - Two exact alternatives are filed in #201: a sound UNREAL guarantee-subset prefilter, and
    bounded-block degeneralization.
- **Correctness.** Acacia at `36e9b4fb` builds and passes 56/56 against Spot 2.16 at both
  limits. The 600-row 10 s verdict screen has no conclusive conflict with `one.bin`.
- **Finding for P4: local measurement links a different Spot than the one shipped.**
  - The Docker image (`scripts/compile.sh`) builds Spot 2.15.1 from the release tarball, with
    release optimization and `--enable-max-accsets=64`.
  - Every local binary and the local `ltlsynt` link `/usr/local`: the owner's
    `spot-goodset` fork (2.15.1.dev), built as a developer build (`-g -O`, assertions on).
  - Local numbers therefore understate Spot-heavy work for both Acacia and `ltlsynt`, in a
    way the competition setting does not.
  - Separating the factors would take a 2×2 of version × build profile at 64 sets, with
    2.16 release as one cell. That is left to the owner's P4 baseline decision, together
    with the `ltlsynt` 2.16 question; `~/opt/spot-2.16-acc64/bin/ltlsynt` reports 2.16.
- **P2 is closed.**
  - Admitted: the action-table borrow.
  - Stopped: picker scratch, sparse backward vectors, rank reuse/SIMD, and the Spot limit
    increase.
  - Filed: #200 (MONA decode memory) and #201 (acceptance limit).
  - Next: the O5 checkpoint.

## 2026-09-29 — Spot 2.16 replaces the 2.15.1.dev fork (owner decision)

- **Owner:** "Use spot 2.16 from now on. Uninstall the dev version 2.15".
- **Installed.** The owner ran the root step. `/usr/local` now holds Spot 2.16, a release
  build: `-O3 -ffast-math -DNDEBUG`, 64 acceptance sets as in the shipped image (#201 showed
  a higher limit gains nothing), and Python 3.14 bindings.
  - The `spot-goodset` fork (2.15.1.dev, a `-g -O` developer build with assertions and 3.13
    bindings) is uninstalled.
  - Build tree: `~/opt/build/spot-2.16-usrlocal`.
- **Frozen baselines.** Spot 2.16 keeps the soname `libspot.so.0`, and every frozen binary
  resolves it through `RUNPATH=/usr/local/lib`. Run naively, E5, C5, G1, M1, M2 and pre-E5
  would silently load 2.16 through an ABI mismatch.
  - The fork's exact runtime is kept, byte-identical and write-protected, in
    `~/opt/spot-2.15.1.dev-goodset-runtime`: `libspot`, `libbddx`, `libspotgen`,
    `libspotltsmin`, plus `ltlsynt`, `ltl2tgba` and `autfilt`, with a README and SHA256SUMS.
  - Frozen binaries run with `LD_LIBRARY_PATH` pointing there. They are not modified, so their
    pinned hashes stand.
- **Repository.** The CI `setup-spot` default, the wheels workflow and scripts, both
  Dockerfiles and `scripts/compile.sh` move from 2.15.1 to 2.16. The CI cache key includes the
  version.
- **Oracle.** The Python lifting oracle defaulted to `/usr/bin/python3.13` and its
  `/usr/local` site. The defaults now follow the running interpreter.
  - BuDDy's variable ceiling is pinned per `libbddx` image. The 2.16 image is added with the
    same 2,097,150 ceiling, because `buddy/src/kernel.{h,c}` are unchanged between the fork and
    2.16.
- **Validation on 2.16.**
  - A fresh debug-native Acacia build passes its 56 unit tests.
  - Full pytest: 1,010 passed and **7 failed**, all in the Python lifting oracle's obfuscation
    and online tests. They return `fallback-pending` or `budget_exhausted` where they expect
    `lifting`. P2a hit the same six; under the fork environment they passed.
  - Diagnosis is delegated. The oracle is a differential reference, not on the solver path.
  - **Resolved.** The oracle called `tlsfcertcheck` without `--cache-cap`, so the checker
    sized its cache to the 2^26 node cap. On a tiny certificate that took 2.89 s and 1.3 GB at
    startup, against under 0.01 s with a 2^20 cache and the same verdict.
    - Pinned to 2^20; node cap and budgets unchanged. Full pytest: 1,017 passed.
    - Acacia's native arms already set `cache_cap` explicitly, so the solver path is
      unaffected.
    - Why the allocation became slow now is not established. The checker does not link
      Spot, and the system time points at page zeroing on a long-running host.
- **Leftovers that need root:** 14 headers from Spot's removed TA module (dated 2025-12, older
  than the fork install), empty `python3.13` directories under `/usr/local/lib64`, and a
  root-owned Spot 3.13 binding install in `~/.local/lib/python3.13/site-packages`.
- **Consequence for comparisons.** Pre-switch baselines mix code changes with the Spot change,
  so O5 adds C5s216 (C5's source rebuilt against 2.16) as a same-Spot reference.

## 2026-09-29 — P4 design decisions (owner)

- **No fresh obfuscated corpus.** The owner: "No need for a fresh obfuscation".
  - P4 runs on the ordinary corpora.
  - The earlier obfuscated corpus is not reused as a blind test, because its mapping was
    published.
  - Genericity stays enforced by the existing guards and renaming tests, not by a blind
    corpus.
- **The 60 s series recycles the 17 s run.** The owner: "do recycle data from 17s for 60s".
  - **Recycled into the 60 s series,** with their 17 s-run times:
    - every 17 s row that ended conclusively;
    - every 17 s row that ended for a cap-independent reason: a deterministic ERROR (for
      example the Spot acceptance limit, #201), a MEMOUT under the same 8 GiB scope, or a
      decline whose recorded stage is not the deadline.
  - **Rerun at 60 s:** only the rows that TIMEOUT or end on the deadline.
  - **Validation:**
    - Rerun a disclosed, seeded sample of recycled rows at 60 s, together with the
      cap-independence check (`benchmarking/tools/check-cap-independence.py`).
    - Any disagreement on outcome, decision or decline stage stops the recycling for the
      affected arm, which is then rerun in full at 60 s.
  - **Labelling:** 60 s tables are labelled *derived*, citing both source runs.
- **`ltlsynt` 2.16** from `/usr/local` is the P4 baseline, following the Spot 2.16 decision. The
  archived fork-era `ltlsynt` runs stay as history.
