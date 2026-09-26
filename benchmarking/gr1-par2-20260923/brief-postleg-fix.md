# Brief — fixes to the post-leg pipeline (build_scratch/postleg/REVIEW.md, ACCEPT WITH FIXES)

Same workspaces and rules as brief-postleg-pipeline.md: NEVER stage or commit; timed legs are
running, so run no solver, build or benchmark, only dry runs; serial; python3 -s; never /tmp.

1. **`make_sample()` must never exceed N.** Cap the mandatory per-bucket picks by the remaining
   slots, or reject an N smaller than the number of mandatory picks with a clear error. Add a
   dry-run check for N=8 with four arms.
2. **Re-run policy (owner-side decision).**
   - Replace the automatic "all near-cap for anomalous legs" recommendation with a
     selection-relevant re-run set, computed only after all seven legs exist.
   - Take the union over the top-3 four-arm and top-3 five-arm subsets. For each arm in a subset,
     include:
     (a) a TIMEOUT/MEMOUT/ERROR of that arm on an instance no other arm in the subset solves
         within the cap;
     (b) a solve in [0.8·cap, cap];
     (c) a solve that is the subset's unique solve, or whose fastest-arm credit changes the
         subset's PAR-2 by more than the run-to-run noise measured from B's two epochs on the
         same instances. Document how that noise is computed.
   - Apply this only to legs whose throttle rate exceeds the clean-leg median by more than 25%.
     Define the clean legs as those with at least 80% sample coverage.
   - Legs with less than 80% sample coverage (arm-5) are labelled "thermal unknown". For those,
     the selection-relevant set is re-run regardless of their rate, since their conditions cannot
     be shown to be clean.
   - Print each set's size and estimated re-run time. Write the commands but do not run them.
3. **Cap-relative near-cap window.** Use [0.8·cap, cap] everywhere, stated in the README,
   including for the final 17 s run.
4. **TACAS23 audit labels.** Rename `crash_exit` to `unexpected_exit`, and add `signal_crash`:
   the process was killed by a signal (negative return code, or ≥128 where the runner encodes
   signals that way; check how run-subset.py records it). Report both counts.
Update build_scratch/postleg/REPORT.md with the dry runs. VERDICT at end.
