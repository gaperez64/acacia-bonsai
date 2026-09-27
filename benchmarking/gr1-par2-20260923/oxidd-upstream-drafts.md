# OxiDD upstream submission (posted 2026-09-25: PR OxiDD/oxidd#49, comment on #37)

Owner decision (2026-09-25): keep the local GC-thread retirement fix
(`tlsf-tools/patches/oxidd-gc-thread-retirement.patch`) for now. Once it checks out, submit the fix
upstream, and show both texts to the owner before posting.

- **No new issue.** OxiDD #37 ("Possible Memory Leak in BDDManager", open since 2025-08-20, no
  replies) reports the same pattern. We comment there and link the PR.
- **Branch.** `build_scratch/oxidd-upstream`, `gc-thread-retirement` at `d174f65`, on upstream `main`
  `be2f69b`. It is one commit changing 4 files (+338/−16), most of it tests.
- **Reviews.** Round 1 was ACCEPT WITH FIXES and led to a protocol redesign. Round 2 was ACCEPT
  WITH FIXES with text-only fixes, all applied below. See `build_scratch/oxidd-upstream-REVIEW*.md`.
- **Validation on the tree identical to `d174f65`:**
  - the five lifetime tests passed 20 serial runs and one run under default parallelism;
  - the workspace passed 114 tests;
  - Clippy and `cargo doc --no-deps` were clean;
  - `cargo fmt --all -- --check` exited 0 on stable, but the repository's nightly
    `wrap_comments` cannot be checked there, so added comment lines are kept within 80 columns.

## PR

**Title:** Retire the index manager's GC worker when the last external handle is dropped

Fixes #37 (the unused-manager pattern it reports).

**Problem.** Dropping the last `ManagerRef` of an index manager does not reliably stop the
manager's GC worker, and the worker keeps the store alive. We found two mechanisms by reading the
code in `crates/oxidd-manager-index/src/manager.rs`:
- **Lost wakeup.** The worker checks for `Quit` only after waiting on the condition variable. A
  `Quit` notified before the worker's first wait, or while it is collecting, is lost.
- **Racy last-reference check.** The `Arc::strong_count(..) == 2` check can miss two concurrent
  final drops.

The probe below measures the effect, not which mechanism caused it.

**Change.**
- External manager handles are counted separately from the worker's own store reference. This
  includes the manager handle inside every function handle.
- The worker checks for a pending `Quit` before waiting and again after waking.
- The final external drop requests retirement and joins the worker. On the worker thread itself,
  it only requests retirement.
- A GC callback may create a new handle through `From<&Manager>` or `Function::from_edge` after the
  count has reached zero. That revival is serialised with the final decrement: the earlier dropper
  returns, the worker keeps running, and the revived handle's own final drop retires it later.
- If the worker panicked, the joining drop propagates the panic, except when that thread is already
  unwinding. A panic inside `Manager::gc` still aborts through the existing guard.

**Behaviour changes for users.**
- Dropping the last manager or function handle now waits for a running collection to finish, then
  for the worker to exit. This applies to Rust drop, C `*_unref` and Python finalisation.
- A GC callback that waits for the thread performing that final drop now deadlocks.
- If the worker panics outside `Manager::gc`, live handles keep working without background GC until
  their final drop reports the panic.

**Tests.** The new tests are in `oxidd-manager-index` and observe their own manager's worker; they
do not read `/proc`:
- 100 repeated lifetimes;
- 100 concurrent final drops;
- a final drop while a collection is held inside a callback;
- callback revival followed by another collection and retirement;
- a transient handle dropped on the worker.

They use `oxidd-rules-bdd` as a new dev-dependency of `oxidd-manager-index`. That adds one edge to
`Cargo.lock` and no new package versions. All five passed 20 serial runs and one run under
libtest's default parallelism. The workspace suite passed (114 tests), and so did Clippy,
`cargo doc --no-deps` and `cargo fmt --check` on stable. The added comments are wrapped by hand at
80 columns, since `wrap_comments` needs nightly.

**Probe for #37.** The probe creates and immediately drops unused BDD managers
(`new_manager(128, 16, 1)`, with `OXIDD_STACK_SIZE=262144`):

| Managers dropped | `main` (be2f69b): GC workers / RSS | This PR: GC workers / RSS |
|---:|---|---|
| 1,000 | 1,000 / 51,548 KiB | 0 / 3,180 KiB |
| 9,000 | 8,997 / 435,500 KiB | 0 / 3,272 KiB |
| 10,000 | spawning a worker fails with `EAGAIN` after about 9,200 managers | 0 / 3,308 KiB |

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01Df5ZPKWrA5rnm7denjL6Fz

## Comment on #37 (posted after the PR, with its number filled in)

We hit the same growth when creating and dropping many managers, and traced it to the index
manager's GC worker, which could outlive the manager and keep its store alive.
- **Lost wakeup:** the worker checked for `Quit` only after waiting, so a `Quit` sent before its
  first wait, or during a collection, was lost.
- **Racy last-reference check:** the `strong_count` check could miss concurrent final drops.

In a probe that creates and drops 10,000 unused BDD managers, `main` reached 9,197 GC workers and
about 435 MiB RSS before spawning failed with `EAGAIN` near 9,200 managers. With #<PR> the probe
finished with no workers left at any sample and about 3.3 MiB RSS. The probe is in Rust rather
than Python, so if your Python case still grows with #<PR>, there may be a second cause.

## Posting steps (after owner approval)

1. `gh repo fork OxiDD/oxidd --clone=false` (to `gaperez64/oxidd`).
2. Push `gc-thread-retirement` from `build_scratch/oxidd-upstream` to the fork.
3. `gh pr create --repo OxiDD/oxidd --base main --head gaperez64:gc-thread-retirement`, using the
   PR text above.
4. Post the #37 comment with the PR number filled in.
5. Add both links to `tlsf-tools/patches/oxidd-gc-thread-retirement.md`. Once upstream releases
   the fix, drop the patch and move the pin.
