# Brief — fix the OxiDD upstream branch (review: build_scratch/oxidd-upstream-REVIEW.md)

Branch build_scratch/oxidd-upstream `gc-thread-retirement` at 2f6f4f5 (base be2f69b). You may amend
or add commits on this scratch branch; squash to ONE commit at the end with an accurate message.
Offline, `--locked`, cargo -j 1, serial tests, never /tmp. A timed benchmark is running. rustfmt and
Clippy are now installed (stable 1.98.1).

Fix all review findings:
1. **Revival of the count after zero.** Handles can be created after the count reaches zero:
   `From<&Manager>` or `Function::from_edge` called from a GC callback (`pre_gc`/`post_gc`) during a
   collection. That brings `external_refs` from 0 back to 1 while the worker still exits. Choose
   the smallest design that is sound. Options include:
   - (a) make revival impossible or harmless. For example, the worker honours `Quit` only if
     `external_refs` is still 0 after the collection. If the count was revived, the final dropper
     must not block forever: it can stop waiting once it observes the revival, with the revived
     handle's final drop then retiring the worker. Prove there is no deadlock and no lost retirement.
   - (b) narrow the contract. Document on the event-subscriber callbacks and `ManagerRef` that
     callbacks must not create handles that outlive the callback, and enforce it with a
     `debug_assert!` on a 0→1 increment.

   Explain the choice in the report. Add a regression test with a GC callback that creates a
   handle while the final drop is in progress, exercising whichever contract you choose.
2. **Tests identify this manager's own worker, not any `oxidd mi gc` thread.** Prefer moving the
   lifetime tests into `oxidd-manager-index`, where the join handle or thread id is reachable through
   `cfg(test)`-only code. Do not add public API. Use time-based polling with generous bounds instead
   of `yield_now` loops and 5 s thresholds, and make the tests correct under libtest's default
   parallelism. Keep `/proc` use only if it is still needed, gated to Linux.
3. **Make "drop during a collection" deterministic.** Use a barrier inside a GC callback that holds
   the collection while another thread performs the final drop. Then assert that the drop completes
   after the collection is released and that the worker has exited.
4. **Worker panic.** `ManagerRef::drop` currently calls `.expect()` on `join()`. Avoid a double
   panic: do not panic when `std::thread::panicking()`; otherwise pick and document a behaviour.
   Say which you chose.
5. **Lints.** Satisfy `cargo fmt --all -- --check`, which currently shows 2 diffs in our lines, and
   `cargo clippy --offline --locked -j 1 --all-targets`. Clippy currently gives `collapsible_if` at
   manager.rs:2099. Also run `cargo doc --no-deps` (upstream's lint-rust recipe). Upstream formats
   with nightly `wrap_comments = true`, which stable cannot check, so keep every comment line you
   add or change at 80 columns or fewer.
6. **Docs.** State that function handles also own manager references. State that dropping the
   last manager or function handle can block until an in-progress collection finishes; this
   applies to Rust drop, C `*_unref` and Python finalisation. Describe the self-drop exception and
   the panic behaviour.
7. Rerun the #37 probe (build_scratch/oxidd-upstream-probe.rs) on the final commit, the focused
   tests (repeat them 20 times), and the full serial workspace suite.

Update build_scratch/oxidd-upstream-REPORT.md: the new commit, design choices, test results, probe
numbers, and a corrected PR text with every claim verified. Include the user-visible behaviour
changes, and use "Fixes #37". Also include a comment for issue #37 that explains the mechanism
briefly and links the PR. VERDICT at end.
