# Brief — make tlsf-tools' OxiDD patch exactly the code submitted upstream (OxiDD PR #49)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api at b98cd74 (clean except the
submodule's applied patch and the owner's untracked git-derived-versioning.md). NEVER stage or
commit. A timed benchmark is running: one job everywhere (cargo -j 1, meson -j 1), serial tests,
never /tmp, no network. The cargo registry already holds the crates for be2f69b's exact lockfile.

The upstream PR is OxiDD/oxidd#49 (https://github.com/OxiDD/oxidd/pull/49), branch
gaperez64/oxidd:gc-thread-retirement, commit d174f65 on be2f69b. It is available locally at
/home/gperez/GIT-repos/acacia-bonsai/build_scratch/oxidd-upstream. It fixes OxiDD #37. It differs
from our current patch: it handles revival of the count by GC callbacks, moves the tests into
oxidd-manager-index, adds an `oxidd-rules-bdd` dev-dependency, and adds one Cargo.lock edge.

1. Regenerate patches/oxidd-gc-thread-retirement.patch as exactly
   `git -C <oxidd-upstream> diff be2f69b d174f65`. Keep the file name.
2. In external/oxidd:
   - reverse the old patch;
   - apply the new one;
   - rebuild with scripts/build_oxidd.sh (offline, locked, -j 1);
   - make `--verify` pass;
   - run the Rust `manager_lifetime_tests`.
3. Rerun tlsf-tools' C regressions: `oxidd_manager_lifetime` in manager and checker modes, 10
   repetitions each. Then run the full serial native meson suite (oxidd + native_gr1).
4. Rewrite patches/oxidd-gc-thread-retirement.md to describe the new patch accurately. Link OxiDD
   PR #49 and issue #37, and state that the patch is identical to the PR's commit d174f65. Say
   when to drop it: when an OxiDD release contains the fix, move the submodule to that release
   and delete the patch, its build-time application and its build-info record.
5. Check whether the CI cache key and consumer jobs need any change; they hash patches/, so a
   new key is expected.
Write build-cleanup/H-oxidd-sync.patch and a short build-cleanup/H-REPORT.md. VERDICT at end.
