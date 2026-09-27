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
