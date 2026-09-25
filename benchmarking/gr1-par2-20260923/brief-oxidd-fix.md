# Brief OX — fix OxiDD's thread-local local-store state across manager lifetimes (tlsf-tools)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api (17ffec9). NEVER stage or commit by any
path (including inside external/oxidd). One job for every build (cargo -j 1, meson compile -j 1);
never /tmp. Read /home/gperez/GIT-repos/acacia-bonsai/build_scratch/review-native3/REPORT.md
(root-cause analysis: OxiDD's LOCAL_STORE_STATE in oxidd-manager-index/src/manager.rs is keyed by
store address; the checker creates a new manager per call on the same thread; manager unref only
signals the GC thread; same-thread repeated checks crash ~19/200 on the round_robin_arbiter
certificate, 0/200 with prepare_local_state disabled; its scratch reproducers are under
/home/gperez/GIT-repos/acacia-bonsai/build_scratch/review-native3/).
1. Reproduce in tlsf-tools alone: a C test that creates, uses and destroys OxiDD managers repeatedly
   on ONE thread (the checker call pattern, and a minimal manager-only pattern), with the dumped
   native artifacts from the reviewer where needed; make it fail reliably (loop count/seed) on the
   pinned OxiDD 9158645.
2. Fix it in OxiDD properly: make the thread-local local-store state generation-safe across manager
   destruction and address reuse (e.g. a per-manager generation/ID in the key, validated on use, and
   synchronous retirement/teardown so no stale state or asynchronous GC thread outlives the manager
   in a way that corrupts the next one). Minimal, well-commented Rust change with a Rust unit/
   integration test in the OxiDD crate that fails before and passes after.
3. Ship it without forking upstream: keep external/oxidd at 9158645 and add
   patches/oxidd-local-store-generation.patch applied reproducibly by the build before cargo runs
   (idempotent; fail loudly if it does not apply; recorded in build info). If the tlsf-tools build
   already has a patch mechanism use it.
4. Write patches/oxidd-local-store-generation.md: an upstream-ready issue/PR description (symptom,
   minimal reproducer, root cause with file:line in OxiDD 9158645, fix, test) that the owner can file
   with OxiDD. Do not contact anyone.
5. Regression: the same-thread C test runs in meson test (e.g. 1,000 manager lifetimes on one thread,
   and 300 repeated tlsf_gr1_check calls on the reviewer's artifact) and passes; ASan on the C test if
   feasible. Full meson test serially. VERDICT at end.
