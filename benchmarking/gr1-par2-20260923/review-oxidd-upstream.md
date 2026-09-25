# Review brief — OxiDD upstream PR branch (GC-thread retirement)

Reviewer; do not edit or commit anything; write your report to
/home/gperez/GIT-repos/acacia-bonsai/build_scratch/oxidd-upstream-REVIEW.md.
Branch: build_scratch/oxidd-upstream, `gc-thread-retirement` at 2f6f4f5 on upstream be2f69b.
Evidence and draft: build_scratch/oxidd-upstream-REPORT.md and benchmarking/gr1-par2-20260923/
oxidd-upstream-drafts.md. A timed benchmark is running: cargo -j 1, serial, offline and locked,
never /tmp. You may run `cargo test --offline --locked -j 1 -p oxidd --lib manager_lifetime_tests
-- --test-threads=1` a few times, and nothing heavier.

This code goes to a third-party maintainer, so check it as the maintainer would.
1. Correctness of the concurrency protocol in manager.rs:
   - The external-reference counter: every handle construction path (Clone, from_raw/into_raw,
     FFI and Python binding paths in the workspace) increments it exactly once, and every drop
     decrements it exactly once. List the paths you checked.
   - The final drop joins the GC thread: the self-drop exception on the GC thread; the GC thread
     holding or cloning a ManagerRef during `with_manager_shared` (can a ManagerRef clone made
     inside GC become the "last" one and join itself?); lock ordering between gc_signal and
     store.state; panics inside GC (poisoning, and a join that returns Err).
   - Memory orderings on the atomic counter are sufficient.
2. The tests: deterministic enough for upstream CI (no timing flakiness beyond the 30 s bound);
   Linux gating correct; do the non-Linux paths assert anything meaningful?
3. The docs: accurate and in the crate's style. The commit message: accurate.
4. Behaviour changes that upstream users might notice, e.g. drop now blocks until a running GC
   finishes. Should the PR text call them out?
5. Anything that should be split out or left out of the PR.
End with VERDICT: ACCEPT / ACCEPT WITH FIXES (list) / REJECT.
