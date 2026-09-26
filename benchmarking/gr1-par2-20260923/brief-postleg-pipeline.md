# Brief — script the post-leg pipeline (selection → smoke → obfuscated three-way → report)

Work in the timing worktree /home/gperez/GIT-repos/acacia-gr1-par2-timing (benchmarking/
gr1-par2-20260923/campaign), whose legs are running, AND the main repo
/home/gperez/GIT-repos/acacia-bonsai for anything outside campaign/. NEVER stage or commit.
TIMED LEGS ARE RUNNING: run no solver, no build, no benchmark. Only fast dry runs of your scripts
on existing result files. One process at a time, never /tmp, Python with -s.

Plan (owner-approved, see decisions.md 2026-09-25/26):
- Seven per-arm legs at 60 s on the plain corpus: arm-1..4 (the default arms), arm-5
  `real:gr1:oxidd`, arm-6 `unreal:gr1:oxidd`, arm-7 `real:param-lift:oxidd`. arm-7 lands in
  perarm-m1/legs/arm-7 and was run with the frozen build_perarm_m2 binary.
- Selection: the best 4- or 5-arm portfolio by maximum solved, with PAR-2 as the tie-break,
  computed as a virtual best over the legs. The owner picks.
- **No separate plain-corpus portfolio run.** Instead there is a stratified plain-corpus smoke run
  (about 50 instances, 60 s), then the chosen portfolio at 60 s and 17 s on the OBFUSCATED corpus.
- Final binary: build_final_<sha> (frozen; hash in build_scratch/final/progress.txt when done).
- External baselines on PLAIN names: formulas are identical under renaming, as verified in
  obfuscation-invariance-summary.md.
  - ltlsynt: derived 60 s (derived-60s/) and archived 17 s (witness-lifting opening/17s).
  - TACAS23 (Acacia 1.x): fresh 60 s via the existing run-subset.py acacia1x commands in
    campaign/README.md, and the archived 17 s CSV.
  - TACAS23 predates e84b968c and reports false REALIZABLE on crashes, so it needs a wrong-answer
    audit.

Deliverables (scripts plus README sections, each dry-run on current data where possible):
1. **perarm-select.py.** Include arm-7, tolerate a missing leg with a clear message, and rank
   every 4- and 5-arm subset of the seven arms by (solved, PAR-2). Output the top 10 per size, each
   arm's unique solves, and the per-arm credit in the virtual best. Add `--smoke-sample N --arms
   <chosen list>`: a stratified sample of about N plain instances. It covers the instances the
   virtual best credits to each chosen arm (fastest or unique), some that every arm times out on,
   and some UNKNOWN-heavy ones. The seed is fixed and the sample is written to a file.
2. **smoke-compare.py.** Compare a portfolio run on the sample with the virtual-best prediction:
   verdict equality, no verdict where the virtual best has none, and time within max(2 s, 1.5×
   the virtual-best time). List the mismatches.
3. **README section "Final: chosen portfolio on the obfuscated corpus".**
   - The exact commands: the smoke run; then the 60 s and 17 s runs with
     run-syntcomp26-coverage.py, `--flags "--arms <comma list>"`, the obfuscated list, map and
     corpus, 8G and no swap, collect-rusage; export-cactus.
   - Locate the obfuscated corpus (build_scratch/stageA/obfuscation-corpus/ or wherever
     prepare-obfuscated-corpus.py wrote it). Pin the corpus with a manifest of per-file SHA-256
     and have the commands verify it first.
   - The sealed name mapping (sha 70f93395…) is opened ONLY by the join step, after the Acacia
     obfuscated runs finish. Write the join script so it refuses to run before both obfuscated
     result files exist.
4. **Join and audit.** Map ltlsynt (60/17) and TACAS23 (60 fresh / 17 archived) onto obfuscated
   IDs through the sealed mapping. The TACAS23 wrong-answer audit flags verdicts that contradict
   any other tool's verdict or the instance's expected status, and reports whether those rows
   coincide with crash exit codes. The three-way table and report use cactus-report.py and
   export-cactus conventions: solved, PAR-2, and per-tool unique solves at 60 s and 17 s.
5. **Thermal annotation.** From build_scratch/thermal/samples.tsv (main repo), compute per leg and
   per final run: median temperature, throttle events per second, and minimum available memory.
   List the near-cap instances (solved in [54, 60] s, or TIMEOUT) to re-run for any leg whose
   throttle rate exceeds the campaign median by more than 25%. Write the re-run commands; do not
   run them.
Report in build_scratch/postleg/REPORT.md with each script's dry-run output. VERDICT at end.
