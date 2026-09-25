# Brief — prepare the upstream OxiDD PR for GC-thread retirement

Goal: a branch ready to become a PR against OxiDD/oxidd `main` (currently
be2f69bd704a4b9baf993fe54ff92c7ca17bb177), carrying the fix now shipped as
/home/gperez/GIT-repos/tlsf-tools/patches/oxidd-gc-thread-retirement.patch (write-up:
patches/oxidd-gc-thread-retirement.md). Draft texts:
/home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/oxidd-upstream-drafts.md.

Rules: NEVER commit in tlsf-tools or Acacia. In the new clone below you MAY create commits on the
branch (it is a scratch clone). No network: the cargo registry already holds upstream's exact
Cargo.lock crates, so use `--offline --locked`. A TIMED BENCHMARK is running: cargo -j 1, serial
tests, never /tmp.

1. `git clone /home/gperez/GIT-repos/tlsf-tools/external/oxidd
   /home/gperez/GIT-repos/acacia-bonsai/build_scratch/oxidd-upstream`, then check out a new branch
   `gc-thread-retirement` at be2f69b. Set remote `upstream` to https://github.com/OxiDD/oxidd.git
   (do not fetch). Apply the tlsf-tools patch.
2. Make it upstream quality:
   - Doc comments on `ManagerRef` / the manager's `Drop`: when the last external handle is dropped,
     the GC thread has exited before `drop` returns, and dropping on the GC thread itself does not
     join.
   - Gate the Linux-only `/proc/self/task` assertions with `#[cfg(target_os = "linux")]`. If a
     portable assertion is cheap (e.g. a test-only counter of live GC threads behind `cfg(test)`),
     add it so the tests also mean something on other platforms. Do not add a public API only for
     tests.
   - Add a test in which the final external drop races with a garbage collection in progress (e.g.
     trigger GC on one thread while dropping the last handle on another, repeated), and assert
     that the thread retires and nothing deadlocks. Bound it by a timeout.
   - Match the crate's style: `cargo fmt`, and `cargo clippy --offline --locked -j 1
     --all-targets` without new warnings in the touched crates.
3. Issue #37 upstream ("Possible Memory Leak in BDDManager"): its reporter creates 100,000 BDD
   managers in a loop without using them, and the Python process eventually dies. Write a Rust test
   or example with the same shape (create and immediately drop many BDD managers, e.g. 10,000 with a
   small capacity). Count live GC threads and RSS every 1,000 iterations, on be2f69b without the fix
   and with it. Report the numbers. If unfixed main accumulates threads or memory and the fix keeps
   both flat, that supports "Fixes #37". If not, say what #37 might be instead.
4. Run the whole workspace test suite serially: `cargo test --offline --locked -j 1 --workspace
   -- --test-threads=1`, or the subset upstream CI runs (see .github/workflows). Report failures
   that also occur on unpatched be2f69b separately.
5. Write build_scratch/oxidd-upstream-REPORT.md containing:
   - the final diff stat;
   - the commit message (imperative, describing what changed and why);
   - the #37 evidence;
   - test results;
   - suggested edits to the PR draft in oxidd-upstream-drafts.md, including whether the PR should
     say "Fixes #37".
   End with a VERDICT.
