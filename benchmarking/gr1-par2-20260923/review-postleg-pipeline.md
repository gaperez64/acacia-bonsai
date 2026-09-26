# Review brief — post-leg pipeline scripts (uncommitted, timing worktree campaign/)

Reviewer; do not edit tracked files; write build_scratch/postleg/REVIEW.md in the main repo.
Brief: benchmarking/gr1-par2-20260923/brief-postleg-pipeline.md. Report:
build_scratch/postleg/REPORT.md. TIMED LEGS ARE RUNNING: static review and dry runs on existing
files only. Run no solver, build or benchmark. Serial, python3 -s, never /tmp.

This code decides which portfolio ships and what the three-way says, so check it hardest:
1. **perarm-select.py.**
   - PAR-2 at a 60 s cap: an unsolved instance scores 120 s, and MEMOUT, ERROR, UNKNOWN and
     TIMEOUT all count as unsolved.
   - The virtual best takes the minimum over the subset's arms, and only verdicts count.
   - Ranking is by solved, then PAR-2. Every 4- and 5-subset of the seven arms is enumerated.
   - Instance keys join correctly across legs, with no silent drops if a leg misses rows.
   - A verdict conflict between arms is flagged, never averaged.
   - Recompute two subsets by hand from the TSVs and compare.
2. **Smoke sample and smoke-compare.py.** Is the stratification right: each chosen arm's credited
   instances, and all-timeout ones? Is the seed fixed? Is the tolerance max(2 s, 1.5× VB)? Is a
   verdict mismatch always a failure?
3. **Obfuscated runs and the join.**
   - The corpus manifest is verified before running.
   - The sealed mapping cannot be read before both Acacia obfuscated result files exist, and
     nothing reads it earlier.
   - The mapping direction is correct, the joined row counts are 1,524, and SYFCO-FAIL rows
     are handled.
   - The TACAS23 audit logic: a wrong verdict is flagged, and so is its coincidence with crash
     exit codes.
4. **Thermal annotation.**
   - Are events per second computed from cumulative counters, as deltas between consecutive
     samples within one leg?
   - arm-5 has only 73 samples, taken during heavy side work; the brief's re-run rule reads it as
     anomalous. Comment on whether the rule is sound. Suggest a narrower, selection-relevant
     re-run set: instances where the arm's result could change the virtual best of any
     top-ranked subset. A TIMEOUT counts only if no other arm in that subset solves it within
     60 s; a solve counts if it falls in [48, 60] s or its credit is decisive. Estimate its size
     for arm-5 now.
5. The README commands: bash correctness, hash checks, the 8G / no-swap scopes, and serial
   execution.
End with VERDICT: ACCEPT / ACCEPT WITH FIXES (list) / REJECT.
