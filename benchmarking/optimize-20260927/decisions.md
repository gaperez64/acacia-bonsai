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
