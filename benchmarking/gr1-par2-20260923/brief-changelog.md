# Brief CL — CHANGELOG.md from the TACAS23 tag to now (docs only)

NEVER stage or commit by any path. No builds, no solver runs (timed benchmarks are running: keep
to reading files and git history). Write ONLY: CHANGELOG.md at the repo root and, for very big
changes, short topic pages under doc/ (new directory, e.g. doc/changelog/<topic>.md).
Owner request: a changelog with a timeline of medium/big changes from Acacia v1 (tag TACAS23 =
5ffd8f99, 2022-11-11) until now; at the TOP, a short summary of which milestones actually improved
performance. Accurate and concise; lean main file; link a doc/ page only for very big changes.
Sources (use them, cite nothing you cannot find in them):
- git history: `git log --first-parent origin/master` and `git log 5ffd8f99..HEAD` (the current
  branch sprint/gr1-par2-20260923 is stacked on PRs #190/#191 and is open as draft PR #192), merges,
  tags (`git tag --contains 5ffd8f99`, `git tag -n`), `git show <tag>` dates;
- PR history with bodies (no network for you): build_scratch/changelog/prs.json (105 PRs) and
  release list build_scratch/changelog/releases.txt;
- sprint/experiment write-ups: benchmarking/*.md, benchmarking/*/decisions.md, benchmarking/*/closing/,
  benchmarking/plots/*/README.md and PROVENANCE, benchmarking/README.md, README.md, CLAUDE.md.
Rules:
- Timeline: grouped by release/tag (or by month where no tag), newest first; each entry one line:
  what changed and, where the repo has a measurement, its measured effect with a pointer (PR number
  or file path). Medium/big changes only (new algorithms, solver backends, portfolio/arm changes,
  frontends such as native TLSF, synthesis output, major refactors, build/CI infrastructure that
  changed how results are produced, benchmark protocol changes). Skip small fixes unless they
  changed results (e.g. soundness fixes: include them, they matter — e.g. the pre-e84b968c false
  REALIZABLE on signal-killed workers).
- Performance summary at the top: ONLY milestones with a committed measurement showing an
  improvement (solved count and/or PAR-2 on a stated benchmark set, cap and memory), with the numbers
  and their source file. Say explicitly when a comparison is against a flattering or unsound
  baseline (the 1.x false-REALIZABLE issue), when an improvement was on a selected subset rather than
  the full corpus, and when later work superseded or reverted it. Do not claim a speedup the repo does
  not measure. Include the current sprint only as far as it is measured and committed; label the
  withdrawn family-registry route as withdrawn, and in-progress work (native gr1/param-lift arms) as
  in progress, not as a performance result.
- Concise: the main CHANGELOG.md should be readable in a few minutes; move detail into doc/ pages for
  the few very big changes (e.g. the antichain/k-safety core rewrite, the portfolio/arms architecture,
  native TLSF frontend, the GR(1) lifting work) — only if they are genuinely big.
- Verify every date, tag, PR number and number you write against the sources; if unsure, leave it out.
At the end, list any claim you could not verify and omitted. VERDICT at end.
