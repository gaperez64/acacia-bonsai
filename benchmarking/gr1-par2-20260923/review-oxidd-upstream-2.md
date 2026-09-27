# Review brief (round 2) — OxiDD upstream PR branch

Reviewer; do not edit or commit; write /home/gperez/GIT-repos/acacia-bonsai/build_scratch/
oxidd-upstream-REVIEW-2.md. Branch build_scratch/oxidd-upstream `gc-thread-retirement` at d174f65
(base be2f69b). The round-1 review is build_scratch/oxidd-upstream-REVIEW.md; the fixes and
evidence are in build_scratch/oxidd-upstream-REPORT.md. STATIC REVIEW ONLY: another side task is
building, so run no cargo command. The logs in build_scratch/oxidd-upstream-*.log are the evidence.

This goes to a third-party maintainer. The fix grew a revival protocol, so check it as that
maintainer would:
1. **The retirement/revival protocol in manager.rs.** Walk every interleaving you can find of:
   - the final drop on thread A;
   - a GC callback on the worker reviving the count with `From<&Manager>` or `from_edge`;
   - the revived handle's drop, both transient (on the worker) and later (on another thread);
   - a second concurrent final drop;
   - a worker panic and its exit guard.

   For each, state the outcome. Look for a lost retirement, a double join, a join while holding a
   lock the worker needs, a dropper that waits forever, and a worker that exits while a live handle
   remains. Check that holding the join-handle mutex through `join` cannot deadlock with the
   worker or with another dropper.
2. **Did the round-1 findings get resolved?** Tests use their own worker identity; there are no
   short timing thresholds; the held-collection test is deterministic; the double panic is
   avoided.
3. **Is the change proportionate for upstream?** It adds a dev-dependency (`oxidd-rules-bdd` in
   `oxidd-manager-index`'s Cargo.toml, plus a Cargo.lock edge) and about 340 lines. Would a
   maintainer accept the dev-dependency, or could the tests use a lighter rule set already
   available in the crate? Is anything unnecessary?
4. **PR text and #37 comment drafts in the report.** Every claim must be supported by the logs.
   The user-visible changes must be stated: drop can block during a running collection; callback
   revival semantics; panic behaviour.
End with VERDICT: ACCEPT / ACCEPT WITH FIXES (list) / REJECT.
