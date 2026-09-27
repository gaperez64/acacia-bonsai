# Turn GR(1) certificate-lifting coverage into end-to-end PAR-2 gains

**Revised review and coding-agent handoff — 23 September 2026**

This revision supersedes the earlier file of the same date. It adds a mandatory worktree-retirement preflight, an explicitly bounded SIMD work package, and corrections to evidence preservation and SIMD attribution. The core objective and independent proof obligations are unchanged.

## Initial prompt — run this before implementation or benchmarking

> Start by pruning obsolete **linked Git worktrees**, not branches or history. In `gaperez64/acacia-bonsai`, retain the main checkout, the checkout on `master`, and all current open-PR head/base branches. At the reviewed snapshot this includes PR #190's `sprint/witness-lifting-20260918` and PR #191's `sprint/param-lift-20260922`; #191 depends on #190, so retaining only the newest head is wrong. Refresh the PR identities once and save them before classifying any path. In the companion `gaperez64/tlsf-tools`, retain its default branch and any open-PR head/base branches, plus the dependency checkout used by a protected Acacia tree. Protect the current agent workspace and any active job, locked tree, or frozen experiment. Do not touch unrelated repositories.
>
> Inventory using `git worktree list --porcelain -z`, not directory-name guesses. Record full paths, common Git directories, refs, HEADs, dirty/untracked/ignored state, submodules, and the protection/removal reason. Preserve every candidate's commits with durable refs before removing its checkout, including detached HEADs. **Do not delete local or remote branches, tags, stashes, or unique commits.** Old experimental ideas must remain recoverable through Git after their working directories are removed.
>
> Before a removal, audit ignored builds and `_bm-logs*` as well as visible files. Keep or safely relocate every referenced/frozen binary, dependency, certificate, source mapping, campaign, and needed runtime file outside the tree being removed; verify hashes and runnable dependencies. The existing artifact-pruning script is a source of candidates, not blanket permission to delete. Its local-checkout search does not establish that another protected branch or campaign does not need an artifact. Do not rebuild a native frozen baseline as a replacement for preserving it.
>
> Save a dry-run retirement manifest outside all candidate trees. Then remove only individually audited, clean, inactive, unprotected trees with `git worktree remove -- <absolute-path>`. Recheck each immediately before removal. **No `--force`, `reset --hard`, `git clean -fdx`, recursive deletion of checkouts, automatic stashing, or branch deletion.** Dirty, uncertain, locked, in-use, initialized-submodule-refused, and evidence-bearing trees without verified relocation stay in place with an explicit deferred reason; they do not block safe removals elsewhere. `git worktree prune` only cleans stale administrative entries, not live directories: inspect its dry run and leave uncertain/offline paths alone.
>
> Produce `worktrees-before.json`, `worktree-retirement-plan.json`, `worktrees-after.json`, an artifact relocation/hash manifest, and a short kept/removed/deferred summary. Do not call a proposed deletion completed. Continue this handoff on the protected PR stack once preflight finishes. This prompt authorizes the safe, scoped worktree retirement described here; it does not authorize deleting benchmark evidence or other work.

## P−1. Worktree-retirement execution contract

**Purpose:** reduce stale workspaces before builds while preserving the exact baseline and research evidence needed for this sprint. This is a local operational step, not a code optimization or a new solver experiment. No worktrees were removed when this handoff was written.

### Resolve the allowlist, once

The live review still found Acacia #190 and #191 open with the heads in the pin table below, and no open tlsf-tools PRs. Refresh at execution time rather than assuming this remains true. Resolve the default branch from the remote metadata; do not assume that tlsf-tools calls it `master`. Include all pages when enumerating open PRs. A failed or partial PR/ref discovery is **not** an empty allowlist: defer cleanup for that repository and continue non-destructive inspection.

Protect by exact repository/ref identity, not a basename, substring match, or an ancestry rule that accidentally treats every descendant of master as protected. Also protect detached worktrees at protected PR/default SHAs and the current task's workspace even when it contains newer unpushed commits. Match a forked PR by its head repository as well as ref name. Fetch required refs without deleting/pruning existing refs or overwriting local branches. Keep the explicit protected set in the retirement manifest.

Use a persistent audit/archive location outside every retirement candidate and outside any directory eligible for `prune-artifacts.sh`. Deduplicate repositories by their resolved common Git directory; an Acacia submodule checkout is not a free-standing disposable worktree merely because it is detached. An independent clone is not a linked worktree: report it but do not delete it under this prompt.

### Preserve first; never infer that ignored means disposable

For each candidate inspect tracked modifications, staged changes, untracked files, ignored files, merge/rebase/bisect state, and nested submodule state. Use NUL-delimited Git output and argument-array subprocess calls for paths with spaces, newlines, or leading dashes. Never parse `git worktree list`'s display columns with `awk` to form deletion commands.

Check active processes and agents, including executables, working directories, open outputs and benchmark scopes that refer to the path. A clean index does not make a running benchmark's tree safe to remove. Do not stop an unrelated job to make its tree removable. If activity cannot be established safely, defer that tree.

Keep all branch refs. For a detached candidate or other commit reachable only through the worktree/reflog, create a collision-checked local archival ref such as `refs/archive/worktree-retirement/<run-id>/<tree-id>`, verify it resolves to the intended object, and include its SHA in the manifest. Preserve independently owned submodule commits in their own object databases too. A ref to the superproject does not preserve an otherwise-unreachable submodule object. Do not push or garbage-collect as part of this step.

Cross-check artifact references in **all protected checkouts and relevant historical evidence**, not just the tree being retired. Protect B, L0 and TACAS23 executables, their shared-library/interpreter/runtime dependencies, selected seed certificates, M6 game/certificate/policy bundles, schemas, templates, source maps, and campaign rows. Preserve content hashes, relative runtime layout, executable permissions and provenance; test relocated executables and data lookup before the old location disappears. An executable with an RPATH or wrapper pointing into the old tree may not be relocatable in isolation. In that case retain the checkout or an independently verified complete runtime bundle. Do not create dangling symlinks back into a retired path.

Run the existing `scripts/prune-artifacts.sh` in **report-only** mode to reuse its directory/name/hash protection heuristics. At the reviewed pin it scans local tracked files/top-level notes and one conventional binary path, not every nested executable or cross-worktree reference. Moreover, its `--manifest` text says “removed” even in report-only mode. Treat those entries as **candidates**, record your own actual disposition, and do not pass `--delete` wholesale. Unknown ignored content remains protected. Positively identified reproducible scratch can be removed only under an individually audited artifact list; this is not permission for `git clean -fdx`.

### Execute and verify

Use this command shape from a protected administrative checkout after its plan entry passes every check:

```sh
# WT has been resolved and audited; AUDIT is outside every removal candidate.
git -C "$ADMIN" worktree list --porcelain -z > "$AUDIT/worktrees-before.porcelain0"
git -C "$ADMIN" worktree remove -- "$WT"
git -C "$ADMIN" worktree list --porcelain -z > "$AUDIT/worktrees-after.porcelain0"
# Diagnostic only; this does not remove a live worktree directory.
git -C "$ADMIN" worktree prune --dry-run --verbose > "$AUDIT/stale-worktree-metadata.txt"
```

These illustrate one already-admitted retirement, not an unreviewed loop. Never run with an empty or inferred `WT`. Recheck allowlist, HEAD, cleanliness, activity and artifacts immediately before the removal. If Git refuses because submodules are initialized, **defer rather than forcing**; automatic submodule deinitialization/deletion is out of scope. Likewise, do not unlock a tree to make it removable.

Only execute an ordinary metadata prune if its entire dry-run set is independently confirmed stale; otherwise leave it. No `--expire now` shortcut. Afterward verify that protected checkouts still resolve, archival refs resolve, artifact hashes match, and no planned-removed directory remains mislabeled as removed. A failed removal is `deferred` or `failed`, not reclaimed disk space.

**Completion:** a machine-readable entry per worktree with `keep/remove/defer`, before/after HEAD/ref, reason, archive refs, artifact manifests, command outcome and measured reclaimed bytes. Uncommitted content is never silently discarded. Do not create a new worktree for each ablation after just pruning old ones: use protected PR workspaces and separate build directories, or a single explicitly retained integration workspace when necessary. Never rebase, reset, merge or move either PR merely to satisfy the allowlist.

## 0. Outcome sought and scope

Preserve the newly demonstrated target-verified REAL/UNREAL coverage, reduce cold invocation time and whole-invocation peak memory, integrate a real source-bound route into Acacia, and finish with the existing SYNTCOMP26 three-way evaluation of **ltlsynt / Acacia TACAS23 / new Acacia**. A comparison against the old current Acacia remains an internal regression/control report, not a substitute for that headline comparison.

This is a source review and implementation plan, not a new benchmark result. All numerical observations below come from saved repository measurements. Proposed speedups, memory reductions, and coverage extensions are unmeasured.

Do not begin another general SMT synthesis project, port Spot to OxiDD, create another benchmark runner, or rewrite the complete LTL translator. The strongest immediate opportunities are avoidable work in certificate construction/checking and the lifetime of BDD managers.

### Pinned review inputs

| Role | Revision |
|---|---|
| Acacia PR #191, `sprint/param-lift-20260922` | `651a07417ca9889d28abc3edfe9f745d260c4cf5` |
| PR #191 base, PR #190 `sprint/witness-lifting-20260918` | `9f490d7ee39983cac8e9c9c613607251c8a7ba53` |
| Acacia master observed during review | `50384cf69a733199fa17c9c571da1761d8451991` |
| tlsf-tools main including merged PR #35 | `2f123f933e679266592fd90898315d25f2881232` |
| tlsf-tools commit still pinned by PR #191 | `f2130939d6b85eadd48f95c785441e74cb5bd5de` |
| Acacia v1 **TACAS23** tag | `5ffd8f994d2348759b721f64d9fa3c9b5b8a8607` |

PR #191 is stacked on PR #190. Do not apply its diff to master as though the parent changes were present. At implementation start, inspect the current tips once, save a delta from these pins, and skip tasks already completed. Read both repositories' current contributor/build instructions before edits. Freeze the actual executables, options, dependencies, host regime, and input hashes that produce each experiment.

**First solver task, after P−1:** tlsf-tools #35 is now merged. Update the Acacia dependency pin to the reviewed merged version and validate/replay the four direct UNREAL successes plus the existing small REAL/UNREAL certificate regression tests. The old REAL and UNREAL campaign legs were not produced by the same committed dependency state. Do not overwrite the historical measurements or silently label them as measurements of the repaired pin.

## 1. Correct interpretation of the evidence

### 1.1 What already works

`benchmarking/param-lift-20260922/m6-report.md` reports 26 new answers inside the 120-second budget: **22 lifted REAL certificates and four directly solved/certified UNREAL instances**. The four UNREAL successes are not environment-witness lifting. Six further previously checked REAL instances exceeded the cold 120-second budget and must remain outside its solved count.

On the deliberately selected 55-instance residual cohort, the saved totals already give:

| Series | Solved / 55 | PAR-2 total at 120 s |
|---|---:|---:|
| M6 checked route | 26 | 7,356.227 s |
| ltlsynt | 22 | 8,014.996 s |
| B or S on this cohort | 0 | 13,200.000 s |

The M6/ltlsynt difference is 658.769 s (about 8.22%) **on that selected cohort**. It is not evidence of a full-corpus integrated-solver win. The source cohorts contain only instances already missed by B/S, and the route has not paid its routing/fallback cost on every other input in these tables.

The report's comparison of a 120-second residual-cohort **PAR-2 mean** with an old 17-second panel **total-score spread** is invalid. Correct the interpretation, not the raw observations. Do not transfer the 12.1-second or 21.1-second historical spreads to a new cap, sample, normalization, host epoch, or metric. Use matched repeated observations to characterize current variability.

The reported stage totals do not turn every residual wall-clock difference into Python overhead: `driver_overhead_s` includes interrupted stage work that never published an ended-stage timer. For example, a target check interrupted after 100 seconds is not 100 seconds of framework overhead.

### 1.2 Prioritized diagnostic targets

These are saved single campaign observations, not promises of attainable improvements.

| Target | Saved dominant cost / failure | First intervention |
|---|---|---|
| `arbiter_with_cancel`, n=9 | target check 49.559 s of 56.027 s cold | checker policy specialization, selected-root compilation |
| `arbiter_with_cancel`, n=10 | OxiDD capacity while compiling policy; check 80.800 s | avoid constructing the global policy mux as a BDD |
| `load_balancer_unreal2`, n=7 | target solve 9.531 s; check 107.103 s, capacity compiling certificate | root selection, last-use release, checker cache sizing |
| `load_balancer`, n=9 | generalization 45.906 s, instantiation 15.458 s, check 0.861 s | native simultaneous composition; repeated-template work |
| `amba_decomposed_lock`, n=15 | generalization 26.238 s, instantiation 19.498 s | same kernel and policy construction work |
| `arbiter_with_buffer`, n=8 | instantiation 12.627 s of 18.370 s cold | shared export traversals; policy construction |
| `arbiter_with_buffer`, n=9/10 | timeout during instantiation | avoid global policy/move intermediates |
| `arbiter_with_cancel`, n=8 | 17.646 s cold | a concrete near-17-second recovery target |
| `load_balancer`, n=8 | 21.592 s cold | composition/export target for a 17-second recovery |
| `collector_v1`, n=11–15 | monitor construction timeout or 8 GiB kill | monitor construction/representation, not rank-cache tuning |

Keep fast controls from `arbiter` and `prioritized_arbiter`, all four successful direct UNREAL rows, and the historical selector-loss sentinels. Do not restrict evaluation to the largest target in each family.

### 1.3 Baselines and comparison ladder

Maintain three distinct controls:

- **B:** frozen deployed current Acacia configuration, with rejected selector/loss-hint variants off unless their deployment status has changed and is evidenced.
- **L0:** reproducible PR #191 lifting/direct-certificate route after only the dependency repair. This establishes the optimization baseline and its retained coverage.
- **N:** the actual proposed integrated Acacia configuration, including routing and all cold proof costs.

Use L0 versus a treatment for mechanism attribution. Use B versus N for deployment regression and full-corpus gains. Never infer deployment performance by adding L0's standalone wins to B's table.

## 2. Safety, measurement, and implementation invariants

1. Every new decisive answer is tied to the **actual requested specification**, target parameters, interface, semantics, game, certificate, and proof method. A same-basename file is not input identity.
2. Keep the current exact/strengthened-reduction trust distinction. A route sound only for REAL must never emit UNREAL on the strengthened game. Environment witnesses retain their causal move order.
3. `CERT_FAILED` means an insufficient proof, not a refuted policy or an opposite game verdict. Timeout, invalid mapping, unknown BDD result, resource exhaustion, and cancellation never become an answer.
4. Preserve all-zero goal-counter semantics separately from explicit one-hot goal 0. The existing checker deliberately checks both because an arbitrary policy may implement them differently.
5. Preserve the sampled/current/next-state timing of fairness and justice. Do not simplify a predicate by moving it one step without a proved transformation.
6. BDD handles, memo keys, substitutions, and variable identities are manager-local. No raw node pointer/ID transfer between BuDDy and OxiDD or between processes. Every persistent cache key includes its manager lifetime and semantic variable mapping.
7. Keep raw data immutable and keep rejected variants visible. Changing report formatting never reruns a solver.
8. A lower timeout score cannot hide extra memory failures; PAR-2 charges both 2T, so inspect failure categories and cgroup peaks separately.
9. Preserve G0–G5 as applicable, plus target-certificate differential/mutation tests. The existing paired-admission command writes reports even when the admission result is REJECT/UNRESOLVED; inspect its decision, not just process exit.
10. Avoid a Cartesian sweep. Each package has one mechanism hypothesis, one reference, and a small number of justified variants. Only surviving combinations receive broad deployment campaigns.
11. SIMD may operate on validated packed metadata and independent Boolean lanes, **not on numeric BDD handles as though they were truth vectors**. Every vectorized path preserves semantic-variable mappings, lane masks, ownership and scalar fallback.
12. Retiring a worktree is not pruning its branch or evidence. P−1 manifests and preserved runtime bundles are prerequisites for deleting a checkout, not optional cleanup after it.

## 3. Package P0 — reproducibility and source binding

**Mandatory before attributing new gains; keep separate from performance edits.**

### Existing code to fix/reuse

- Acacia `benchmarking/param-lift-20260922/param-lift-campaign.py`: `_resolve_request`, `Request`, duplicated `FAMILIES`, subprocess/deadline code.
- Acacia `benchmarking/param-lift-20260922/generalize_gr1.py`: source registry, `build_game`, `run`, `run_unreal_direct`.
- tlsf-tools `gr1_monitor_game.py`, exact reduction metadata and `tlsfcertcheck`.

The research campaign currently resolves a supplied TLSF by basename against `m0-instances.tsv` and rebuilds a repository template at that n. This is acceptable as a fixed-dataset reproducer, **not as a general Acacia frontend**. The designated verdict and baseline status columns also belong to the experiment, not to runtime selection.

### Required changes

- Keep the existing dataset reproducer available for historical commands.
- Add a production request object with actual input bytes/hash, exact source/template evidence, complete parameter assignment, lowered semantics/target, and I/O mapping. The new runtime path must not read solved/unsolved or expected-verdict columns.
- Resolve family capabilities by structural/semantic matching of the source, or by a content-verified template instantiation. A filename match alone must decline. For materialized benchmark files, verify that the reconstruction really denotes the supplied instance under the existing lowering rules; do not silently trust origin strings.
- A capability can select a REAL-proposal route, an exact game route that discovers either side, or a sound one-sided route. It is not a table of answers.
- Tool paths and Python binding locations become explicit build/CLI configuration, not `/usr/bin/python3.13` or one host's site-packages directory. A probe validates the required API/version without running benchmark work.
- Save a single machine-readable manifest for dependency repair: old/new gitlinks, binaries, build profiles, solver/checker versions, capability schema, and replay results.

### Tests

Same filename with a modified guarantee, changed parameter, changed target semantics, changed I/O ownership, substituted template, duplicate/missing AP, and stale manifest all decline or run the correct original-spec route. No mutated file can inherit a cached verdict. A verified certificate for n=6 must not license n=7. Restore actual corpus materialization for any previously missing G1 inputs; do not call a partially executed gate a pass.

## 4. Package P1 — make the existing checker avoid unnecessary BDD construction

**Highest-priority tlsf-tools optimization.** Keep the current proof obligations and certificate format initially; change only how their Boolean functions are built and reused.

### 4.1 Reuse the existing selected-root constructor

Current `src/main_tlsfcertcheck.c::compile_aig` (reviewed around lines 960–997) allocates a map for the entire AIG and retains every gate's BDD until all gates have been built. `setup_bdds` repeats this for game, policy, and certificate. By contrast, `include/tlsf/oxidd_common.h` already exports:

```c
bool oxidd_build_roots(OxiddRun *run, const Aig *game, Bdd *map,
                       uint32_t maxvar, const uint32_t *lits, Bdd *roots,
                       size_t count);
```

Its contract consumes map references after their final consumer and keeps roots separately. **Adapt/reuse this helper, its resource policy, and its tests; do not implement another gate scheduler.** The reviewed `src/oxidd_common.c::oxidd_build_roots` already traces selected-root uses in reverse topological order and releases last uses. Its counter updates can address the same input twice and its gate evaluation follows dependencies: do not mark these loops independent with SIMD pragmas. First validate exact input ownership and failure behavior at the execution pin.

- Build a validated root list for each checker method.
- Certificate-policy checking requires the winning region, specification-goal copies, and rank predicates (plus the corresponding dual/fairness roots for environment certificates).
- The present fixed-policy proof path requires `move_*` names for interface shape but does not use their functions as proof premises. Do not build their exclusive cones merely because they are exported. Keep structural validation of the complete supplied AIG and certificate interface; skipping irrelevant Boolean evaluation must not accept malformed encodings.
- Shared cones needed by selected roots are still built exactly. Duplicate roots and complemented aliases must have correct reference counts.
- Root lists must be derived from the chosen method. A future relation-checking method will use move/progress functions and must not inherit the fixed-policy root mask accidentally.
- Reuse the shared checked OxiDD operation wrappers where this does not couple proof logic to the solver algorithm. Validate against independent tiny exhaustive games to control common-mode library bugs.

**Tests:** old and new root functions are equivalent over the same manager/mapping on small arbitrary AIGs; duplicate roots, shared gates, negative edges, constants, dead gates, invalid references, and allocation failure at each publication point. A malformed unused cone still fails structural validation. Large valid unused move cones must not be evaluated by fixed-policy checking.

### 4.2 Specialize the goal counter before compiling the policy

Current `setup_bdds` compiles the **entire policy mux** to BDDs. Only later, in `check_certificate_mode` around lines 2030 onwards, does it loop through the counter modes and call `specialize` on those BDDs. The UNREAL certificate method has the same broad pattern for environment/fairness modes.

For certificate-only checking, replace this with:

1. Validate the original policy interface and AIG once.
2. For each supported mode, build the policy input map with its counter bits already set to Boolean constants.
3. Compile only the required control and counter-next output roots under that map, with shared multi-root memoization inside the mode.
4. Perform the existing safety, invariant, counter-update, and rank-progress conditions for that mode.
5. Release mode-local roots/substitutions before the next mode unless a bounded measured cache retains them.

The mode sequence includes **all zero** and **every one-hot** mode. Do not identify all-zero and one-hot zero because the generated policy usually agrees; arbitrary supplied policies are checked by this tool.

`--method closed-loop` and the closed-loop leg of `--method both` still need their own complete policy semantics. Do not accidentally run a one-mode policy as a full closed loop. They can retain the existing construction as a control initially.

**Tests:** intentionally change only the all-zero policy behavior, counter update, one fairness mode, or one rank layer. The optimized checker must reject exactly as the old checker does. Test large selectors whose constant-cofactored output cones are small and ensure the build path actually uses constants before BDD creation.

### 4.3 Reuse substitutions and constant successor images within one mode

`successor()` currently constructs, populates, and frees a complete state substitution on every call. The proof loop repeatedly computes the same goal successor, fairness successors, and lower-rank successors.

- Construct one immutable state-update substitution per specialized mode; keep all replacement roots alive until its final use.
- Compute the goal-successor/intersected-invariant expression once per mode.
- Compute each fairness successor once per mode on first use.
- Compute a lower-rank image once per level, not once per fairness entry.
- Share a bounded multi-root substitution application when supported by the pinned API; otherwise reuse the same substitution object and retained root-level results.
- Do not cache all intermediate rank images indefinitely. Prefer scope-local caches and streaming eviction. Measure peak memory as well as time.

No cache crosses a different policy, variable order, game, substitution mapping, or manager lifetime. Build only substitutions that the selected proof mode needs. Multi-root calls and `bdd_veccompose` are **batched/algorithmic BDD operations, not claims of CPU SIMD**; SIMD attribution is specified in P2S below.

### P1 reporting and admission

Use both cold checks on frozen target artifacts and full cold lifting invocations. The former attributes checker savings; only the latter establishes end-to-end benefit. Report AIG gates visited, requested roots, stored/live-node samples, peak cgroup bytes, setup/proof time, and final proof status. Counts are diagnostic, not substitute success criteria.

Primary targets: cancel n=8/9/10, inpchange n=6/7, load_balancer_unreal2 n=6/7, round_robin_unreal2 n=5/6/7. Keep fast REAL controls. No full corpus run is needed for every P1 subpatch.

## 5. Package P2 — remove repeated work in the generalizer

### 5.1 Native simultaneous composition, not two sequential Python passes

`generalize_gr1.py::Bdds.substitute_variables` currently renames each variable to a temporary with `bdd_compose`, then replaces every temporary with another `bdd_compose`: **2m calls for an m-variable substitution**, repeated inside target rank/move construction. The workaround exists because the installed Python binding does not expose a usable `bddPair` object. The generic path already uses an interleaved state/temporary layout; do not count introducing interleaving again as new work.

Implement one small native helper linked to the **same BuDDy implementation/ABI that owns the caller's handles**, exposing simultaneous composition and optionally batch renaming/multi-root application. BuDDy provides `bdd_veccompose`. Do not load a second libbdd with an independent global table and pass handles between them. A pybind/SWIG/C boundary must pin roots, transfer ownership explicitly, propagate resource failures, and validate that it belongs to the correct manager.

The alternative is an isolated native OxiDD generalization kernel with explicit AIG input/output and a fresh manager. Choose one route after a small build/API feasibility check; do not implement both full backends. A native BuDDy adapter is the smaller first patch. An OxiDD kernel is justified only if it removes enough conversion and manager-lifetime cost in addition to Python overhead.

Keep the old two-pass method as a test oracle on tiny functions. Required tests include variable swaps, cycles, dependencies between replacements, non-injective substitutions, constants, variables outside support, and repeated calls under GC. Substituting `x:=y, y:=x` must be simultaneous. BDD replacement restricted to variable permutations is not a general functional-substitution implementation.

### 5.2 Compile AIG roots once and cache structural maps

Current `Bdds.from_aag` reconstructs gate/input maps and starts a new memo per predicate. This repeats AIG traversal for invariant/rank roots and for repeated target goals. The unique table may merge nodes afterwards, but it does not remove the Python traversal/FFI cost that created them.

- Introduce an attempt-local compiled-AIG context: normalized gate map, public variable ABI, selected roots, shared traversal memo, manager lifetime.
- Decode all required seed predicates as multi-root DAGs or lazily memoize roots in that context. Explicitly release contexts at the end of their reuse window.
- Cache `_predicate_templates` by seed certificate identity, selected seed predicate, role/anchor/goal relation, arity, normalization mapping, and manager lifetime. Repeated target goals often refer to the same seed goal representatives. Do not re-project them each time.
- Precompute template supports and reverse canonical-variable maps. Use the owner-group index and bounded support-mask path in P2S for per-subset variable maps/cubes instead of preserving a repeated full-variable scan and merely vectorizing it. `instantiate_templates` currently even reconstructs `frozenset(subset)` inside that scan. Cache maps/cubes within a bounded reuse window; do not materialize every subset × goal × predicate product.
- Retain source semantics in keys; do not use only numeric AAG literals or BDD node IDs across different instances.
- Generic policy export must reuse a shared BDD-to-AIG memo across outputs with the **same** variable-to-literal map. The AIG builder already hashes gates; the additional gain is eliminating repeated BDD walks, not claiming gate sharing is absent everywhere.
- `game_functions()` already uses a multi-root memo. Cache its completed result in the relevant context rather than rebuilding it where only fairness functions are needed.

### 5.3 Replace fixed scratch-coordinate assumptions

`Bdds` currently uses an 8192-variable universe with blocks at quarter/half capacity. Generic policy construction additionally hardcodes `curr_base=512` and `policy_game_base=1024`. Merely increasing the universe is not enough to make every block disjoint.

Introduce an explicit attempt-local variable-layout object describing public state/letters, canonical templates, temporary composition variables, policy counter, and policy game coordinates. Use checked integer arithmetic. Reject overlap and overflow before BDD work. A reordering changes levels, not semantic variable identity. Do not renumber certificate/public variables without an explicit mapping and hash.

Tests force small artificial limits, more counter variables than the old reserved interval, overlapping replacements, and multi-instance manager use. No scaling optimization may silently cross a coordinate boundary.

### 5.4 Separate schema learning from target proof and reuse it

Current `run()` builds a complete large target candidate, then calls `generalize_once()` for a small probe, solving seed instances again; further CEGIS rounds repeat the process. It also suppresses a successful independent target proof when the auxiliary probe is UNKNOWN/CERT_FAILED.

Refactor the orchestration into:

```text
acquire_small_instances(request, deadline) -> SeedBundle
learn_schema(seed_bundle, limits) -> SchemaBundle | Decline
instantiate(schema, target, deadline) -> CandidateBundle | Decline
check_target(actual_spec, candidate_bundle, deadline) -> TargetCheckResult
```

These are proposed module boundaries, not claims that the interfaces already exist.

- Run seed feasibility/arity analysis before constructing an expensive large target monitor whenever that analysis is target-independent. Keep target role/bus compatibility checks before accepting an instantiation.
- Reuse seed games, seed solutions, and schema analyses **inside the same cold invocation**. On CEGIS refinement solve only genuinely new seeds.
- A small probe is a proposal-selection aid. If used, run it before expensive target work and instantiate the same schema; do not rebuild all seeds.
- A valid exact target certificate is sufficient for the requested target answer even if an exploratory small probe is inconclusive. Distinguish `target_verified` from `schema_validated_on_probes` in evidence. This relaxation is not permitted for any all-n claim.
- Do not add the full large target as a seed to claim target lifting after solving it directly; label direct solving separately.
- Do not restart a checker with doubled node capacity on every UNKNOWN. Retry only a diagnosed recoverable capacity failure, with sufficient remaining **absolute** deadline and memory headroom. A timeout does not justify automatically allocating a larger table and starting over.
- Keep cold and warm modes separate. No cross-target cache receives free cost in headline tables.

### P2 target tests

Compare complete generated certificates/policies semantically and final target checks, not only textual hashes when harmless DAG ordering changes. Preserve seed role, anchor, bus-schema and goal mappings. Test unrelated seed policies/tie-breaking, expired deadline between stages, cancellation/reaping, and successful target proof with an inconclusive auxiliary probe. The last case must remain a target claim only.

Primary performance targets: load_balancer n=8–11, buffer n=8–10, amba_lock n=15/16, prioritized n=7/12, and arbiter n=6/10. Break down composition, projection, relabel, export, Skolemization, and checking only on the diagnostic target subset, not with hot-loop tracing enabled in deployment timings.

## 5A. Package P2S — SIMD and bit-parallel opportunities, with the right boundaries

**This is an extension of P1/P2, not a third BDD backend or a new sparse-antichain project.** Start with S0/S1 while those packages are changed. S2 and S3 require a measured remaining bottleneck and are independently skippable. None delays a successful P1–P3 closing evaluation.

### S0. Inventory what is already vectorized; collect one useful profile

`tlsf-tools/include/tlsf/simd.h` already supplies `tlsf_words_or`, `tlsf_words_popcount`, and `tlsf_words_and_popcount`; `src/apset.c` calls the first two. Its compile-time tiers include AVX2, AVX-512, x86-64-v2 and NEON with scalar tails. At the reviewed pin, the two population-count helpers have explicit AVX-512/VPOPCNTDQ and NEON paths but **no explicit AVX2 population-count path**; AVX2 builds use the scalar builtin-count loop there, subject to compiler optimization. This is a candidate to measure, not proof of a bottleneck or absent SIMD throughout tlsf-tools.

The new Python generalizer does not use those word-array primitives in `projection_templates`, `instantiate_templates`, or `_support`. It repeatedly manipulates `frozenset`, `set`, dictionaries and BDD support cubes. Those are the places to change representation and reduce Python/native crossings before writing more intrinsics. Preserve Acacia/posets' existing SIMD paths; do not reintroduce many fixed-size instantiations or an independently templated rank container.

Extend the existing targeted P1/P2 diagnostic once, recording:

- time in projection metadata, BDD projection/relabeling, support extraction, variable-cube construction, mode specialization, and AIG/BDD construction separately;
- **actual variable-support width**, client count, ownership-tuple arity, number of reused subsets, mask word-length histogram, mode count and selected AIG cones;
- bytes in masks, caches and temporary mode data, including their cold construction cost;
- compiler/ISA flags for each C/C++ target and the Rust OxiDD build independently. A C `-Dcpu` or `-march` setting does not establish which ISA a separately built dependency uses.

Take this data from real successful, unsuccessful and near-cap targets in §1.2, not only giant synthetic arrays. Capture once and reuse for differential kernel tests; keep record overhead off the final timed path. Do not add a mandatory full-corpus profiling campaign.

### S1. Owner indexing plus fused support masks: the first implementation

**Code:** `generalize_gr1.py::{VarInfo,projection_templates,instantiate_templates,_support,Bdds.cube}` and the maintained `schema`/`bdd_kernel` modules created by P2. Reuse or narrowly extend `tlsf/simd.h` through a C boundary where useful; do not copy it into Acacia or include C-specific `restrict` declarations in an incompatible C++ boundary.

For a client subset S, the current exact rule is:

```text
keep(S) = { variable v : owners(v) is empty or owners(v) is a subset of S }
```

Replace repeated whole-variable Python scans with an attempt-local index:

```text
owner_groups[canonical_owner_tuple] -> sorted public-variable IDs
shared_variables = owner_groups[()]
```

Canonical owner tuples are deduplicated, sorted **client IDs from provenance**, not BDD levels. Build them once. To assemble `keep(S)`, collect the shared group and groups indexed by the subsets of S. For the existing maximum proposed client arity k=4, there are at most 2^k=16 group lookups, excluding the cost of emitting the selected IDs, instead of rescanning every variable. If the current capability allows another arity, use its actual bound; never hardcode k=4 into soundness.

Emit IDs in the same deterministic order as the reference. Sorted owner tuples are only lookup keys: preserve `_ordered_subset`'s goal-relative slot ordering and the existing role/relation/lowest-index-anchor group semantics when constructing canonical mappings. Keep shared variables even when S is empty, and keep all variables whose complete multi-owner tuple is included. A role class is not permission to identify concrete client IDs.

**Projection simplification:** compute and retain the exact BDD support `F = supp(f)` once per live root/context. Quantify `drop = F \ keep(S)` rather than every public variable outside `keep(S)`. This is exact: quantifying variables not in `supp(f)` has no effect. A cached structural support over-approximation is also safe for choosing the quantified superset, but an under-approximation is not. A support cache keyed by BDD identity must keep an owning reference to that root or invalidate on its release: manager-lifetime identity alone does not prevent a collected node ID from being reused. Do not confuse a mask of clients with a mask of BDD/public variables. Apply the registered public-ID-to-BDD-variable mapping when building the quantification cube; BDD levels may change under reordering.

Represent repeated **variable** masks as bounded arrays of `uint64_t` words when this beats sorted ID lists. The hot exact operations are:

```c
// m = number of valid semantic bits; word i uses the same bit mapping in all arrays.
drop[i] = support[i] & ~keep[i];
outside |= owners_or_support[i] & ~allowed[i]; // subset iff outside == 0
```

Use a fused AND-NOT write/reduction without constructing complement temporaries. In AVX2, `_mm256_andnot_si256(keep, support)` means `~keep & support`; reversing operands changes semantics. Always mask unused bits of the final word. For containment/intersection predicates, use zero/nonzero tests rather than popcount when a count is not required. Prefer `dst = support & ~keep` plus a zero test in the same pass when the cube builder needs to know whether `drop` is empty.

**Small and sparse cases come first.** A family with ≤64 clients needs at most one client-owner word, so a scalar 64-bit test or the indexed representation can beat vector setup. If a full owner scan is still justified, store one `uint64_t` owner mask per variable in a contiguous array and vectorize **across variables**. Beyond 64 clients, retain compact owner tuples or correctly sized masks; do not truncate or allocate an O(number-of-variables × number-of-clients) dense owner matrix by default. Likewise, a predicate supported on three public variables should use a small sorted list rather than scanning an 8192-bit scratch universe.

Choose a cheap deterministic representation rule based on measured word lengths/reuse, not benchmark names. Cache canonical maps and cubes by exact subset, goal normalization, instance identity, variable layout, root and manager lifetime. Bound the cache by bytes; avoid precomputing all combinations for all goals. If the owner index already removes the hot scan, **do not add an unused SIMD variant merely to tick a box**.

**Native boundary:** pass packed arrays/batches once, not one Python call per word. Do not construct Python integers or sets inside the native hot loop. Begin with portable scalar-word code; reuse the existing ISA tier or add one narrow fused helper where required. New helper contracts specify lengths, tail mask, aliasing, error behavior and ID mapping. With `restrict`, do not permit overlapping buffers; use a tested alias-safe wrapper if in-place calls are needed. Never `memcpy`/`memcmp` reference-counted BDD objects as a substitute for their API or compare handles across managers.

**Tests:** compare the original set-based `keep/drop`, normalized mapping and resulting projected/rebuilt BDD functions with owner-index/scalar-mask/SIMD implementations. Include 0/1/63/64/65 clients, empty/shared/multi-owner groups, sparse high IDs, changed BDD variable order, identical roots with different mappings, mixed concrete/canonical coordinate spaces, garbage collection, all-zero/all-one masks, unaligned buffers and incomplete tails. Randomly permute semantic ID assignments; outputs must remain equivalent after applying the mapping. Preserve exact seed separability and acceptance/progress checks.

**First targets:** load_balancer n=8/9, amba_decomposed_lock n=15, and arbiter_with_buffer n=8/9. Native metadata speed alone is not a claimed solver win; measure the complete schema/instantiation/cold pipeline.

### S2. Mode-parallel constant propagation before BDD compilation

**Conditional:** after P1.2, use this only when repeated AIG interpretation/specialization across many counter modes remains material. It is **not** permission to keep one full BDD manager or live root set per SIMD lane.

P1.2 checks the policy separately under all-zero and each one-hot counter assignment. Those fixed assignments can be propagated through the selected policy AIG cones in parallel. Use bits to denote modes and two masks per live AIG value:

- `Z`: modes in which the value is provably constant zero;
- `O`: modes in which it is provably constant one;
- bits in neither: value remains unknown and requires exact compilation.

For a valid-mode bitmask `M`, maintain `Z & O == 0` and `(Z | O) & ~M == 0`. Initialize public state/letter inputs as unknown, and counter inputs according to each separately identified mode. For complemented literals swap Z and O. For an AND gate:

```text
Z_out = (Z_left | Z_right) & M
O_out = (O_left & O_right) & M
```

One scalar word propagates 64 independent modes at once; multiple mode words may use the existing SIMD tier. This is bit-parallel ternary constant propagation, not BDD function evaluation. Correlated unknown inputs can leave an actually constant expression unknown; that is safe. It must **never** classify a nonconstant expression as constant. For an unknown result, compile the original exact function under that mode. For known constants, safely avoid the corresponding Boolean construction. Do not classify mode-invalid states or skip rank obligations from sampled values.

Reuse the selected-root/topological/last-use plan, with independent mask storage; do not write a competing AIG compiler. Gate dependencies remain sequential; parallelism is across modes/words. If SIMD/parallel code updates use counts for two operands that alias, it must preserve both decrements; never assert independence over gate edges with `ivdep` or unsound `restrict`.

**Memory discipline:** use a bounded mode tile and live-gate storage, with an explicit scratch-byte cap (initial experimental ceiling 32 MiB). Do not allocate `all_gates × all_modes × BDD_handle` or an unbounded full gate/mode mask matrix. A prepass that finds constants but retains more than the construction it eliminates fails the objective. On scratch exhaustion, drop the optional prepass and use P1.2's exact scalar specialization within the remaining deadline; no partial proof status escapes. All-zero and one-hot goal 0 remain different lanes even when generated policies happen to agree.

Do not group or skip two complete modes just because their constant masks match. Equivalent proof obligations require exact matching of residual policy functions, mode-specific next-counter behavior and rank/goal premises. SIMD has not proved that equivalence.

**Tests:** constants, complemented edges, reconvergent fanout, the same operand twice, independent and correlated unknowns, dead malformed cones (structural validation still rejects), 1/63/64/65 modes, all-zero versus explicit mode 0 differences, scratch failure, and environment-certificate modes. Exhaustively instantiate tiny circuits and compare every claimed constant with all assignments of its free inputs. Compare complete checker statuses and proof mutations against P1.2. This pass can only remove exact known-constant construction; it never grants a certificate by itself.

**Targets:** cancel n=9/10, inpchange n=6/7 and the environment-checking capacity targets, only after their mode/build profiles justify it. No large campaign for every mode-tile size.

### S3. Fill a genuine AVX2 word-kernel gap only when it is reached

An explicit AVX2 popcount/AND-popcount implementation is absent from the current `simd.h`. A candidate implementation is nibble-lookup (`vpshufb` in each 128-bit lane), followed by widened sums, or a carry-save reduction for long arrays. This is **optional**, not the first patch: scalar POPCNT is often a strong short-array reference, and S1's predicates usually require no count.

Add this only if the post-S1 pipeline still spends material time counting long masks. Reuse the existing helper names/contracts and exact feature selection. AVX2 does not imply AVX-512 vector-popcount support; keep the existing explicit `AVX512VPOPCNTDQ` guard. A nibble implementation needs the lookup table repeated per 128-bit shuffle lane, correct nibble extraction and bounded/widened accumulation to prevent byte-lane overflow. Do not let an advertised AVX-512 tier silently assume an unavailable extension.

Benchmark the real length distribution and scalar builtin baseline, including setup and tails. Test randomized arrays, all ones, large accumulations, lengths around every SIMD block boundary, zero length and unaligned addresses. Preserve the helper's documented count range; callers exceeding a 32-bit count must use a widened API rather than silently wrap. Do not change the common helper just to speed an unrepresentative synthetic megabyte bitset.

### S4. What not to do, and how to prove an actual SIMD gain

- `bdd_veccompose` is simultaneous functional substitution, **not hardware vectorization**. Multi-root construction, caching and native batching are different effects. Report them separately.
- BDD conjunction, quantification and substitution are manager operations over DAGs. A bitwise AND on handle integers computes no Boolean conjunction. Equal handles may be compared by the manager's canonical identity contract, not raw cross-manager bytes. Vectorizing pointer chasing or editing unique-table/refcount internals is out of scope.
- Do not replace O(1) canonical BDD equality with a truth-table scan. Fixed client arity is not small Boolean support: shared/global variables and monitor encodings can make support large. No new exhaustive truth-table backend follows from k≤4.
- Bit-sliced AIG simulation can accelerate randomized/tiny exhaustive regression tests. It can reject a candidate with a concrete failing assignment, but passing samples is **never** a liveness or certificate proof. Do not add it to the online route without measured rejected-candidate savings.
- No new SIMD rank/antichain storage work: those previously measured costs are not the present GR(1) checker/generalizer bottleneck. No second SIMD utility library or template instantiation per dimension.

Use a three-step attribution ladder on the **same data and semantics**:

| Comparison | What it can establish |
|---|---|
| Original Python/reference → owner-index + native scalar words | Less work, reduced FFI, better locality |
| Native scalar words → selected SIMD words on the same representation | Incremental hardware-vectorization benefit |
| Final integrated solver → B and L0 | Real cold time, coverage and whole-cgroup memory benefit |

For the second comparison isolate dispatch to the candidate kernel; keep the rest of the executable, compiler profile, manager settings, variable order, CPU/worker/thread budget and input unchanged. A whole-build `-march` change is not an isolated SIMD comparison and may reproduce the documented code-layout sensitivity. Record compiler vectorization diagnostics/disassembly for the actual kernel; the compiler may already vectorize a scalar loop. A nonvectorized reference, when needed, is compiled with vectorization disabled **only for that kernel translation unit**, not for the entire solver.

The existing tlsf-tools compile-time ISA mechanism is sufficient for initial tests; do not add global dispatch infrastructure unnecessarily. If shipping a portable runtime-dispatched helper, keep the dispatcher and scalar path baseline-compatible, check OS/CPU feature availability once, and dispatch outside hot loops. Retain forced-scalar testing. Do not globally upgrade the build to AVX-512, alter Rust/C++ flags indiscriminately, or trade additional threads and per-thread tables for an alleged SIMD result.

**Gate:** report micro/kernel time, affected-stage time, cold invocation time and whole-invocation memory, including conversion, mask construction, caching, dispatch and tails. A native/bitset win with no extra SIMD win is a valid outcome: ship the simpler scalar-word implementation if it passes the existing gates. Correctness failures reject immediately; a small-set neutral result can remain a separately identified variant in the preregistered broader discovery, but no new default without confirmed benefit and no validated regression. Carry only a small number of justified survivors, not all permutations of S1/S2/S3, into §11's three-way evaluation.

## 6. Package P3 — manager lifetimes, memory budgets, and resource accounting

### 6.1 Do not attempt to share two incompatible unique tables

BuDDy and OxiDD use different node/handle representations and manager ownership. Even two OxiDD processes do not share a unique table. Equal logical functions only share nodes inside a compatible manager and variable interpretation. There is no safe table-merging flag.

In the reviewed pipeline:

- seed solver processes use OxiDD;
- the Python generalizer creates a process-wide BuDDy manager with 8,000,000 initial node slots and 800,000 cache slots;
- `Bdds.close()` does nothing because Python proxy destruction makes ad hoc `bdd_done()` unsafe;
- the Python process then waits for external OxiDD checks while retaining that BuDDy manager and potentially candidate/probe data;
- `tlsfcertcheck::setup_bdds` sets **apply-cache capacity equal to node capacity**.

This is both lifetime overlap and duplicated conversion work. Engine unification alone would not make separate processes share data, and keeping all stages alive in one giant manager could retain more irrelevant nodes rather than less.

### 6.2 Low-risk ownership repair

Make the supervisor BDD-free. Let a candidate-building child own BuDDy, emit validated artifact paths/hashes and the learned schema representation, and **exit before the checker is started**. If the existing wrapper already has the needed process/deadline primitives, reuse them. For subsequent CEGIS work, transfer a schema/artifact bundle, not live Python BDD handles.

A more tightly integrated native kernel may later keep one generalization manager alive across seed/target work. The independent checker can remain a fresh process. Do not destroy a manager while proxies, hooks, cached roots, or traceback objects still hold references.

### 6.3 Independent node/cache controls and bounded policies

Add a checker cache-cap option/config field independently of its node cap, using the existing `OxiddSolveOptions`/resource-policy conventions where possible. Keep the prior configuration as the baseline. Test one conservative smaller-cache treatment (for example the already used solver ratio) before considering a second, rather than a large cache/node grid. The best ratio is a measurement, not a theorem.

Reuse `OxiddRun` failure/GC/retry helpers at safe boundaries. Do not invent another garbage collector. Do not retry non-pure operations or mutate substitutions under caches that assume immutable mappings.

BuDDy initialization/capacity should be a configured small-start/grow policy if measurements show a large fixed RSS floor; this is separate from its operation cache. Record actual allocated/stored-node samples and available live-root counts. Do not convert node slots to a claimed byte total using an assumed object size from a different BDD build.

### 6.4 Measure the complete process tree

Use the existing whole-invocation cgroup `memory.peak`, no swap, and a process/phase timeline. `max(RUSAGE_SELF.ru_maxrss, RUSAGE_CHILDREN.ru_maxrss)` is not the peak of a concurrently resident parent-plus-child tree. Keep it only as a labelled process-level diagnostic.

An open/interrupted stage has a censored duration, not zero. Publish its last known phase and elapsed lower bound once at cancellation. Keep that separate from measured orchestration overhead. Preserve OOM versus TIMEOUT versus UNKNOWN categories through export and reporting.

### P3 acceptance

A memory win must be actual lower whole-invocation peak memory or a recovered previously capacity-limited target, with no validated runtime/coverage regression. Do not ship a smaller cache merely because its reserved-byte estimate is smaller. Validate the full parent/child pipeline under the same 8 GiB budget.

## 7. Package P4 — monitor representation; coverage extension after core optimization

**Conditional second workstream, not a prerequisite for the first PAR-2 campaign.**

The current `scripts/gr1_monitor_game.py` translates conjuncts separately with Spot and emits one latch per deterministic-monitor state. The saved `_unreal1` study shows a delay parameter increasing one monitor's state count exponentially, with hundreds of one-hot latches. The existing followup brief is a proposal, not a completed optimization. The `param-lift-gr1` branch is an ancestor of current tlsf-tools main; do not blindly re-merge it to obtain new encoding work.

### 7.1 First binary/hybrid encoding ablation

- Preserve one-hot as reference.
- Implement binary encoding for sufficiently large monitors, and a single justified hybrid rule after measurement. Small monitor thresholds are not tuned per benchmark name.
- Reset, rejecting behavior, acceptance timing, and invalid encodings are explicit. Invalid codes must not create a spurious controller/environment success. Prove reset/reachable correspondence and ensure any supplied invariant only supports valid encodings, or totalize invalid codes consistently with the exact checker contract.
- Version provenance. Current lifting aligns `monitor_*_state_*` bits as semantic states and contains one-hot-specific code. Do not silently reinterpret these names as binary bits.
- Initially evaluate direct exact GR(1) solving/certification under the new encoding, where no old cross-size lifted-bit mapping is reused. Port lifting schemas only with an encoding-independent state/projection contract and equivalence tests.
- Differentially verify one-hot and binary monitor traces/acceptance on small formula families and exact games. Compare the interpreted semantic state, not latch bit vectors.

### 7.2 When binary encoding is insufficient

Encoding an already constructed 2^u-state DFA with u bits still pays for enumerating its 2^u states. For a recognized bounded-delay obligation, implement a direct symbolic shift-register monitor only after proving its local temporal semantics and testing overlapping triggers. Record exact applicability and preserve the generic monitor as reference.

The collector special case also explicitly enumerates a semantic product and 2^n input valuations in `_collector_candidate`. A smaller monitor latch encoding does not remove that enumeration. Do not advertise binary encoding as a complete collector fix. If pursued, use reachable symbolic product computation and partitioned transition/image operations under the fixed candidate, with the same independent certificate checks.

Primary horizon extension targets are the actual two-parameter `_unreal1` benchmark points. Preserve both n and u from source metadata, rather than matching only the n family registry. Report coverage on the full family and on fresh sibling points, not only a custom n=3 sweep.

## 8. Optional P5 — relational region checking without obligatory global Skolemization

**Do this only if P1/P2 show that policy construction or policy compilation still dominates.** It is a new proof interface and must not be disguised as a harmless performance flag.

The generalizer already has a candidate winning region, rank layers, and pre-Skolem admissible progress moves. For decision-only REAL, constructing one global output policy is not logically necessary if a checker can prove that appropriate choices always exist.

For each scheduler mode j, independently reconstruct a **joint** allowed-move predicate `Good_j(s,u,c)` from the actual game and the supplied, validated rank certificate. It includes safety, successor membership in the same invariant, and the appropriate rank-progress/counter rule. Check:

```text
reset is in W
all proof predicates have valid support and satisfy rank shape/coverage
for every state s in W and environment input u:
    there exists one control c satisfying Good_j(s,u,c)
```

Do not separately check existence of a safe action, an invariant-preserving action, and a progress action: those may be different, incompatible actions. Every chosen control must satisfy the full conjunction. Use the exact existing fairness/justice timing and least-rank rule. For deterministic finite games, such choices plus the checked ranking/counter discipline yield a strategy; formalize this sufficiency lemma alongside the checker.

- Use a new versioned method/result that verifies **game-region existence**, not an exported policy. Synthesis requests still require policy extraction and checking.
- Keep the current fixed-policy checker and explicit tiny-game solver as independent references; mutate non-total, incompatible-choice, rank, fairness, reset, support and mode certificates.
- Never credit an UNREAL answer by swapping quantifiers informally. A dual relational certificate is a separate task; retain the implemented exact environment-policy checker initially.
- This method may consume a selected set of move/progress roots; P1's root mask must be method-specific.
- Prefer per-disjoint-rank-layer Boolean checks over eagerly building a single giant move mux when the same joint existential condition can be established exactly.

This is a credible next combination of lifting and symbolic proof checking, not a prerequisite for finishing the immediate speed/memory work.

## 9. Combinations with existing branches: admit by mechanism, not by branch count

### 9.1 Reuse existing OxiDD construction/ordering infrastructure

The reviewed `codex/oxidd-ordering-experiments` tip `e1884bf1…` is already an ancestor of tlsf-tools main (zero unique commits in the comparison). Its node/cache controls, order handling, selected-root construction, diagnostics and GC boundaries are resources to reuse in the new checker path, not an unmerged optimization to cherry-pick.

The existing safety demand-transition mode computes complete updates only for state variables in a predicate's support. It explicitly does not support the generic GR(1) mode. Reuse the **principle and low-level complete-support helpers** for certificate obligations, with a separate semantic proof; do not just enable the safety-only switch on GR(1).

### 9.2 Sparse-real selector plus lifted routing

PR #190's broad campaign confirms aggregate gains from the selector, but the five-pair followup also confirms approximately 44 instances' TIMEOUT-to-MEMOUT changes across multiple families and two clean 17-second coverage losses (`lift_pb_4_pe_`, `lift_unary_enc_pb_4_pe_`). Do not reduce this to the old workstation alignment hypothesis.

A reasonable combined candidate is:

```text
sound cheap existing routes
-> source-bound eligible GR(1) proof/lifting route under its own sub-budget
-> original Acacia portfolio, with a separately justified memory-bounded sparse option
```

The exact scheduling policy is measured. Sequential preprocessing consumes fallback time; concurrent lifting consumes cores/memory and may duplicate BDD managers. Neither is regression-free by definition.

First integrate lifting with the unchanged B fallback. Only then test a bounded sparse selector combination on the residual region if source/backend evidence supports it. A sparse worker needs a local budget allowing a controlled UNKNOWN before it kills the entire shared invocation. Do not spend the whole deadline on a failing prepass, add an unrestricted fifth worker, or replace the known-good lift route globally.

Do not route using prior benchmark answers, M0 `status_120s`, or expected verdicts. Use verified source capabilities and features available within the charged invocation. Keep source/multi-parameter recognizers separate from learned performance thresholds.

### 9.3 Other negative branches

- `research/dual-frontier-sprint` (`636e7e5f…`): the saved delta census has 24 processed events all hitting a work limit and no completed qualifying large events. It provides no evidence to activate a dual antichain representation now. Its K-rank state space is not the Boolean monitor-state space used in the lifting proof.
- `sprint/b2-d1` (`fe484452…`): incremental bad predicates retain an append-only losing-generator journal. This is search-oracle work, not the current checker/instantiation bottleneck; previous high churn is a reason to avoid reviving the journal unchanged. A scoped immutable-root cache in P1/P2 is a different, better matched reuse opportunity.
- Lifecycle instrumentation, move-compaction, and loss-hint branches are not imported into the new baseline. Reopen only when current profiles show their mechanism on the integrated winning path. Removing intermediate loss checks can increase a losing worker's competition with another decisive arm.
- `sprint/step4-cleanup` and other cleanup tips: inspect their unique diff/ancestry before any cherry-pick. A branch label alone is not evidence of unmerged work. Port a specific still-relevant cleanup only after dependencies and regression controls are identified.

### 9.4 Longer-term proof compression worth retaining as a hypothesis

Lifting makes **verification-obligation symmetry** more plausible than generic game-frontier symmetry. If two fixed-target proof obligations are exactly related by a bijective variable renaming preserving the game/policy/mode semantics, checking one suffices. Reuse only with an explicit checked renaming or exact canonical structural identity; repeated-looking role labels are insufficient. Arbitrary Skolem tie-breaking may destroy this symmetry, which is another motivation for the optional relational checker.

The old maximal region's measured separability arity does not prove that every sufficient inductive winning region has that arity. Strengthening a candidate with independently justified monitor-validity constraints may reduce irrelevant off-encoding behavior. This remains an experiment requiring an inductive/progress certificate, not permission to discard states assumed unreachable.

## 10. Refactor and prune without erasing evidence or rebuilding everything

### 10.1 Separate functional changes from movement

The runtime candidate generator should not remain a 2,600-line file in a dated benchmark directory. After P0 pins and small correctness fixtures exist, extract a small maintained package; keep the dated CLI as a forwarding compatibility entry until all references are migrated.

Proposed boundaries (adapt to existing project structure instead of duplicating equivalents):

| Module | Owns |
|---|---|
| `request` / `capabilities` | source binding, parameters, semantics, one capability registry |
| `artifact` / `aig` | stable ABI, validated I/O, selected-root import/export |
| `bdd_kernel` | manager/layout lifetime, compose/relabel, bounded memos |
| `schema` | roles, projections, learned invariant/rank templates |
| `instantiate` | target predicate and optional policy construction |
| `runner` | one absolute deadline, subprocess ownership, proof invocation |
| `evidence` | stage events, immutable hashes and machine-readable outcome |

Place solver-independent TLSF/monitor/certificate functionality in tlsf-tools where appropriate; keep Acacia portfolio policy in Acacia. Do not introduce a third general plugin framework. Remove duplicated family definitions and host-specific paths. A final refactor-only change must preserve generated artifact meanings and deployment behavior.

Split `main_tlsfcertcheck.c` into CLI/validated artifact loading, shared low-level AIG/BDD construction, and **separate system/environment proof routines**. Do not make the independent checker call the game solver to decide whether a certificate is correct. Sharing Boolean primitives is different from sharing the proof algorithm.

### 10.2 Historical cleanup precedents

The local worktree retirement has already happened under P−1 or is explicitly deferred per path. This section is **tracked-code/document cleanup**, not a second opportunity to delete workspaces. Refer to retained branches/archival refs for old ideas; do not recreate a checkout for each merely to inspect a diff.

Inspect these commits rather than inventing another pruning policy:

- `bc8121825fac6096e02ab42178f6d115c9961493`: consolidate closed Markdown records while keeping living reports separate; preserve content and links.
- `d7e0891102c530b0a23f7d16f39d5c5cb1e15139` / PR #144: artifact-pruning manifest; protect hash-referenced binaries and write-protected frozen campaigns.
- `73b2ab7018d2a07ab81df3918e1697bb7441b156`: prune unreachable options/presets only after checking all consumers and preserving executable behavior.

Keep one current high-level index and one current lifting guide. Archive superseded detailed handoffs by topic, preserving decisions, counterexamples, source pins, measurements and links. Mark stale instructions such as old branch-local “do not stage” notes as historical. Do not execute them as current agent instructions.

Do **not** delete `m4-*.tsv` just because they look like experiment sediment: the current generalizer reads some of them at runtime. First replace those dependencies with validated explicit schema/capability data and regression tests, then archive the original evidence.

Prune duplicate narrative and obsolete launch instructions, not raw results, useful rejection evidence, unique certificates, source manifests, or the scripts that regenerate reports. A file with no code references may still be a documented human workflow. Preserve TACAS23 and all frozen baseline executables; keep hashes as references. Dry-run removals and save both retained and removed sets. Repair links/anchors and validate archived commands' referenced paths.

Documentation-only changes do not require solver reruns. Refactors changing compiled layout are evaluated with the final integrated binary, not excused as automatically performance-neutral.

## 11. Experiment design and final three-way delivery

### 11.1 Efficient sequence

1. Complete P−1 worktree retirement without losing protected executables/evidence; freeze B/L0, repair the dependency and input binding, and validate small certificates.
2. Collect one focused phase/memory diagnostic over the named P1/P2 targets, including S0 mask/mode distributions. Reuse existing artifacts for kernel/checker attribution; keep cold target invocation timings separate. Do not rebuild the experiment framework for SIMD.
3. Implement P1/P2 subpatches in independently reversible commits; attach S1 owner indexing and, when justified, SIMD masks to P2. Test native scalar first. S2/S3 are independent conditional treatments, not prerequisites. Reject correctness failures immediately. Run targeted tests after each change, broader small gates at package boundaries rather than recompiling the world after every documentation edit.
4. Integrate surviving optimizations, one consistent lifetime/cache policy, and the actual source-bound runtime route. Run the 55-row cohort plus easy/small and noneligible controls before broad timing.
5. Run a full-corpus discovery comparison for the integrated candidate. Include every previously solved target, unsupported specification, conversion failure and fail-fast decline. No unsolved-only deployment admission.
6. Confirm all newly gained/lost outcomes, resource changes, suspicious slowdowns and preregistered controls with fresh paired rounds. Keep the primary observations unchanged.
7. Freeze the final deployment candidate. Produce the three-way headline below; reuse a preceding full-corpus leg only when its executable, options, inputs, limits and host regime really match. If the final candidate changed, rerun its affected/full final leg as appropriate; do not synthesize a virtual score.

### 11.2 Uniform caps, fixed resources

Use the established full SYNTCOMP26 list and its exact source map: currently 1,524 logical IDs. Primary long-cap comparison is **120 seconds**, with a distinct **17-second** comparison for historical continuity and the near-cap objectives.

Run each as a **uniform-cap experiment**. Do not use the staged `--caps 17,120` summary as a uniform 120-second dataset. No 51/120-second diagnostic answer is counted as a 17-second solve. Primary rows are one observation per ID; a second balanced epoch demonstrates repeatability, not best-of selection.

Keep `MemoryMax=8G`, `MemorySwapMax=0` for the **whole solver invocation and all children**. SIMD-mask scratch, cached maps, per-mode temporary storage and any native-helper worker belong to that same limit. Freeze CPU affinity/quota, parallel arms and OxiDD threads consistently; retain historical unrestricted-core runs only as labelled historical data if the new regime differs. Use serialized measured invocations and no simultaneous builds/profiling on the timing host.

Everything used online is timed: parsing, source eligibility, parameter instantiation, small solves, schema learning, probes, exports/imports, checker retries, target checking, fallback and process startup. Warm family caches are a separate report. Learned development choices must be frozen before the confirmation/closing observations.

### 11.3 Headline series are exactly the requested tools

| Headline role | Requirement |
|---|---|
| ltlsynt | pin actual executable/version and invocation; do not silently update Spot between runs |
| Acacia v1 | exact **TACAS23**, `5ffd8f994d2348759b721f64d9fa3c9b5b8a8607`; freeze binary/build/dependencies |
| New Acacia | one actually runnable frozen configuration including charged lifting/routing/checking |

Add B, L0, evaluated-but-not-admitted candidates and virtual best only in separate internal/supplementary reports. The three-way plot must not silently replace v1 with B or replace new Acacia with an oracle union of routes.

Reuse the existing v1 adapter and verified semantics-adapted SyFCo conversion route. Keep ltlsynt's route, input/target semantics and conversion timing explicitly documented. Cached conversion timing excluded from a legacy reference measurement is not an end-to-end frontend measurement; disclose it and add a matched source-to-answer comparison if making that claim. Strict/finite/unsupported semantics and known conversion failures must not disappear by intersecting successful inputs.

Resolve wrong-answer conflicts independently; the TACAS23 tag is a comparison tool, not a semantic oracle. Unsupported/failed/wrong answers receive no solved credit. Preserve raw rows and the evidence for any corrected classification; use supported failure categories for export and a separate wrong-answer audit when needed. Never relabel a wrong verdict as a successful fast solve.

### 11.4 Reuse the actual infrastructure

Use `benchmarking/run-syntcomp26-coverage.py`, its `export-cactus` command, `run-subset.py`'s external/legacy adapters, `benchlib.par2`, `paired-admission.py`, and `cactus-report.py`. Reuse the retained three-way launch scripts from prior campaigns. If an adapter for the new real runtime route is needed, add it to the existing input/tool normalization layer; do not create another timed campaign framework.

Report rendering, after validated CSVs exist, uses this existing command shape:

```sh
python3 benchmarking/cactus-report.py \
  --csv "ltlsynt=$REPORT/ltlsynt.csv" \
  --csv "Acacia TACAS23=$REPORT/acacia-tacas23.csv" \
  --csv "New Acacia=$REPORT/acacia-new.csv" \
  --title "SYNTCOMP26 — ltlsynt / TACAS23 / new Acacia ($CAP s, 8 GiB)" \
  --timeout "$CAP" \
  --out-prefix "$REPORT/three-way" \
  --markdown "$REPORT/three-way-par2.md"
```

Invoke separately for CAP=120 and CAP=17. The source CSVs contain the same IDs and the corresponding uniform cap. Check the plot endpoints against the solved counts. No measured curve is generated until its raw observations exist.

For each cap deliver:

- PAR-2 **total and mean**, solved/REAL/UNREAL counts, TIMEOUT/UNKNOWN/resource/error/conversion categories, plus verified wrong-answer audit where relevant;
- cactus PNG and PDF from those same observations;
- gains/losses and common-solved paired time/memory analysis against B and L0;
- per-family and per-route attribution, including new17/120-only answers and any losses outside the lifting cohort;
- full provenance, input/corpus/tool/binary hashes, exact flags, resources, cold/warm boundary, manifest, raw/normalized rows, report-generation commands;
- explicit decisions for rejected candidates and the final shipping rule.

### 11.5 Admission policy

A package may continue as research when a mechanism improves without deployment admission. An integrated default needs correctness, no validated regression under the frozen policy, and a reproduced useful benefit. New solves do not erase lost solves, and TIMEOUT-to-MEMOUT failures remain visible.

Require a full-corpus PAR-2 improvement for the stated overall goal, not merely a lower score on the 55 selected residual inputs. Treat a measured overall ltlsynt lead as an open performance target, not a reason to omit or tune away losing families. Attribute current uncertainty rather than recycling an old noise floor. Do not stop an informative preregistered discovery campaign merely because an already known performance loss recurs; quarantine correctness violations immediately.

## 12. Suggested commit sequence and ownership

**Pre-commit operational step:** execute the initial prompt/P−1. Save its audit outside retirement candidates. Do not mix worktree deletions with source commits or delete refs to make a commit list look clean.

1. **Reproduce the environment certificates at the merged dependency pin.** Acacia integration owner; no optimization mixed in.
2. **Bind lifting to the actual input and make tool configuration explicit.** Acacia + tlsf-tools interface owner.
3. **Compile only checker roots using the shared OxiDD constructor.** tlsf-tools checker owner; tests plus targeted root/memory evidence.
4. **Compile certificate policies after fixing their scheduler modes.** Same owner; all-zero and dual mode tests.
5. **Reuse checker substitutions and repeated successor predicates.** Same owner; bounded ownership and peak-memory checks.
6. **Add native simultaneous composition and shared generalizer DAG work.** Generalizer/kernel owner; old operation oracle retained for tests. This is not a SIMD claim.
   - Follow with a separate **Index owner groups and fuse support-mask operations** commit (S1), retaining native-scalar and reference tests. Add an ISA specialization only if the post-index workload justifies it.
7. **Separate seed/schema work from instantiation, remove redundant probes/retries.** Generalizer owner; target proof remains authoritative.
8. **Separate heavy manager lifetimes and node/cache budgets.** Runtime/resource owner; whole-cgroup validation.
9. **Extract maintained modules and archive superseded prose with manifests.** Cleanup owner; no algorithm additions in this commit.
10. **Integrate the selected route and run the final paired/three-way campaigns.** Benchmark owner; one shared dataset schema.

S2 mode-parallel constant propagation and S3 AVX2 population count are separate followups unless their measured selection conditions hold. P4 monitor representation and optional P5 relational checking are separate followups unless they become necessary for the stated targeted improvement. Their code must not delay delivering and evaluating successful P1–P3 work. No speculative switch explosion remains enabled in the final default.

## 13. Definition of done

The final report answers all of the following with saved evidence:

- Which linked worktrees were kept, retired or deferred, and can all protected PRs, historical code refs, frozen executables and relocated evidence still be used?
- Which code paths already had SIMD, which representations removed work, and what incremental SIMD benefit remains after native-scalar optimization? Are tails, ISA fallback, variable mappings and memory caps validated?
- Do the repaired pins reproduce the 22 REAL and four direct UNREAL closures under their original budget, or what changed?
- Which exact operations and intermediate representations were removed, and how did cold time and whole-invocation memory change on their target families?
- Which of the previous 120-second successes are now also 17-second successes?
- Are any formerly solved nonlifting instances lost by routing, fallback, build changes or shared-resource interference?
- Is peak-memory reduction real at process-tree level, rather than an estimate obtained by summing or taking maxima of unrelated per-process samples?
- Are all runtime decisions based on source/structural capability rather than benchmark answers or a same-basename lookup?
- Does the new full solver beat its actual predecessor on full-corpus PAR-2 and coverage? Does it beat pinned ltlsynt and TACAS23, separately at each cap? If not, state the remaining delta.
- Can a clean checkout execute the recorded three-way pipeline without a missing environment-certificate feature, host-specific Python path, stale generated file or archived runtime TSV dependency?
- Are failed experiments still discoverable while obsolete Markdown and genuinely unused code/presets have been pruned safely?

## Sources and anchors for the coding agent

Repository source is authoritative at the pins below; web API documentation is background only and does not change the pinned dependencies.

- [Acacia PR #191](https://github.com/gaperez64/acacia-bonsai/pull/191)
- [PR #191 M6 report](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/param-lift-20260922/m6-report.md)
- [M6 raw stage table](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/param-lift-20260922/m6-campaign.tsv)
- [Generalizer reviewed here](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/param-lift-20260922/generalize_gr1.py)
- [Research campaign frontend](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/param-lift-20260922/param-lift-campaign.py)
- [Closing interpretation and missing-pin caveat](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/param-lift-20260922/closing/README.md)
- [Broad selector discovery and confirmation decisions](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/witness-lifting-20260918/decisions.md)
- [Merged tlsf-tools #35](https://github.com/gaperez64/tlsf-tools/pull/35)
- [Independent checker](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/src/main_tlsfcertcheck.c)
- [Existing shared root-construction/resource API](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/include/tlsf/oxidd_common.h)
- [Existing diagnostics, variable orders and demand-mode scope](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/docs/tlsfsolve-diagnostics.md)
- [One-hot per-conjunct monitor construction](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/scripts/gr1_monitor_game.py)
- [Monitor-encoding proposal, not an implemented result](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/benchmarking/param-lift-20260922/closing/followup-monitor-encoding.md)
- [Dual-frontier censored delta census](https://github.com/gaperez64/acacia-bonsai/blob/636e7e5fc2890fd0c1c23eedec6d0585c8fc6402/benchmarking/DUAL-FRONTIER-DELTA-CAMPAIGN.md)
- [Incremental-bad search-cache commit](https://github.com/gaperez64/acacia-bonsai/commit/fe4844529f193a86d4ada71c0a53c1e6789abeb7)
- [Pruning and shared benchmark infrastructure precedent, PR #144](https://github.com/gaperez64/acacia-bonsai/pull/144)
- [Closed Markdown consolidation precedent](https://github.com/gaperez64/acacia-bonsai/commit/bc8121825fac6096e02ab42178f6d115c9961493)
- [TACAS23 commit](https://github.com/gaperez64/acacia-bonsai/commit/5ffd8f994d2348759b721f64d9fa3c9b5b8a8607)
- [BuDDy operators including simultaneous functional composition](https://buddy.sourceforge.net/manual/group__operator.html)
- [OxiDD separate node/apply-cache manager parameters](https://docs.rs/oxidd/latest/oxidd/bdd/fn.new_manager.html)

### Additional anchors verified for this revision

- [Acacia PR #190: protected parent branch](https://github.com/gaperez64/acacia-bonsai/pull/190)
- [Existing artifact pruner: inspect its exact protection scope](https://github.com/gaperez64/acacia-bonsai/blob/651a07417ca9889d28abc3edfe9f745d260c4cf5/scripts/prune-artifacts.sh)
- [Existing TLSF SIMD word kernels](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/include/tlsf/simd.h)
- [AP-set consumers of the SIMD helpers](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/src/apset.c)
- [Existing root compiler and last-use accounting](https://github.com/gaperez64/tlsf-tools/blob/2f123f933e679266592fd90898315d25f2881232/src/oxidd_common.c)
- [Git worktree operations, porcelain/NUL format and safety limitations](https://git-scm.com/docs/git-worktree)
- [GCC vectorization diagnostics](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html)

**Evidence status:** this revision is a source-checked plan. No local user worktree was removed and no solver/SIMD performance experiment was executed while preparing it. The mode-mask equations are a proposed conservative constant-propagation rule, not an empirical speedup or a substitute for exact certificate checking.
