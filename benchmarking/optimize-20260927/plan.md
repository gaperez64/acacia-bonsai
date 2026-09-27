# Acacia follow-up: a lean repository, optimized existing arms, then UNREAL lifting

**Revision date:** 27 September 2026. Supersedes both earlier follow-up handoffs; replace the active handoff rather than keeping parallel instructions.

**Inspected PR:** `gaperez64/acacia-bonsai` #192, branch `sprint/gr1-par2-20260923`, head `26cbb45f19df1cd1b2eb55f844680fefea4466bb`. The PR is still open and unmerged. The three commits since the previously inspected `f74ad9d1` add campaign material, scripts, and decisions, not solver changes. Companion tlsf-tools source remains the adopted `a294419` revision. Refresh these facts at kickoff. [S1]

**Scope:** evidence-storage migration and serious source cleanup; optimize the existing native and legacy implementations; then implement environment-certificate lifting (#195). Issues #194 and #196 remain in scope. Verilog output (#117), a new portfolio scheduler, and a broad representation sweep do not.

**What changes in this revision:** optimization precedes adding/comparing a new arm; REAL lifting remains an optimization target despite not being selected; historical scientific evidence is preserved outside the source Git repository, not indefinitely checked in; the selected evaluation portfolio is distinguished from unchanged shipping defaults. No new solver measurements or repository-size measurements are claimed here.

## Initial prompt for the coding agent

Continue the work of PR #192 from its latest inspected/admitted state. Read the end of its decision log: the owner stopped the unfinished closing campaign and requested optimization before further extension. Do not resume that campaign or repeat the seven-arm census as your first action.

First freeze the actual source/dependency/configuration state and selected five-arm invocation. Audit repository growth and export bulky evidence to checksum-verified compressed release assets; keep only small current reports, a compact evidence index, and necessary regression fixtures in Git. Preserve original observations, corrections, rejected experiments, and frozen-binary provenance externally. Update the repository's old evidence-retention instructions so they no longer require raw campaign data in Git. Safe source/worktree cleanup follows; do not force-push or rewrite shared history without explicit approval.

Next optimize implementations that already exist: direct REAL GR(1), experimental REAL parameter lifting, their shared native substrate, and the selected legacy engines. Begin with duplicate work, ownership/lifetimes, fail-fast behavior, memory budgets, and concrete action-table allocation waste. Run focused correctness and performance screens; do not launch full-corpus standalone legs for every intermediate build. Try sparse backward storage and SIMD/locality changes only after their measurement gates, and timebox unsuccessful alternatives.

Freeze an optimized five-arm checkpoint **O5**, with the same arm membership/order as the selected baseline **E5**, before adding `unreal:param-lift:oxidd`. UNREAL lifting is the next major coverage extension, using the now-cleaner and optimized shared infrastructure. It does not depend on an ambitious BDD-engine merger or an optional proof-API redesign succeeding. Complete and tune the bounded prototype before its expensive standalone leg and actual portfolio comparison.

All solver behavior must be generic. Never dispatch by benchmark names, known hashes, family fingerprints, stored family templates, or verdict tables. Hashes identify source/artifact bytes; they do not establish genericity or prove a reduction correct. Preserve independent checking against the exact target. At closure, compare actual concurrent portfolios at 17 and 60 seconds with ltlsynt and Acacia TACAS23, retaining the frozen selected baseline for cumulative regressions. Store new raw campaigns outside Git from their first observation onward.

## 1. Updated state: what the selection establishes

### 1.1 Shipping defaults and the evaluation choice are different

The current shipping group/registry remains unchanged. The owner-selected **evaluation** portfolio is the previous four arms plus direct REAL GR(1), in this order:

```text
real:small:backward
real:small:forward
unreal:formula:spot-guarded-sparse
unreal:automaton:forward
real:gr1:oxidd
```

Call this invocation **E5**, not an already-shipped new default. The smoke driver passes the mix explicitly with `--arms`; its recorded frozen binary is `build_final_f74ad9d1/src/acacia-bonsai`, SHA-256:

```text
65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96
```

Verify that binary on the actual measurement host before reuse. The source revision containing the latest evidence is not the source revision embedded in that frozen binary. Do not rebuild it and call the result the same baseline. `docker_default` names build presets, not physical arms: resolve inheritance and the explicit invocation separately. [S1–S3]

### 1.2 Available evidence and its limits

The updated record reports the following **virtual best of isolated 60-second legs**, not full-corpus concurrent results:

| Mix | Solved / 1,524 | PAR-2 total, as rounded in the log |
|---|---:|---:|
| Previous four arms {1,2,3,4} | 1,217 | 38,787 s |
| Best four-arm subset {1,2,3,5} | 1,225 | 38,006 s |
| Selected five {1,2,3,4,5} | 1,238 | 36,260 s |
| Five-arm runner-up {1,2,3,5,6} | 1,235 | 36,789 s |

The deciding reruns did not change the selected subset. Its 50-input, 60-second concurrent smoke achieved 36 solves, matching the prediction on that panel. The obfuscated full run was stopped after 80 rows; that partial output was explicitly not retained as evidence. The 17-second run, TACAS23 leg, and final join did not happen. Therefore there is no completed full-corpus E5 result or completed new three-way to reuse. Do not convert the virtual 1,238 figure into a deployed result. [S1]

`real:param-lift:oxidd` finished its standalone leg with 19 REAL answers, 1,354 UNKNOWNs, and 151 TIMEOUTs; it has one unique solve among the seven measured arms and was not selected. This supports **optimizing it before another admission attempt**, not declaring generic lifting useless or already default. `unreal:gr1:oxidd` exists but is not selected. Environment-certificate lifting is not implemented; the four historical M6 UNREAL closures were direct target solves. [S1, S4–S5]

### 1.3 Sequence and finite endpoints

| Stage | Required work | Endpoint |
|---|---|---|
| P0 | Evidence migration, baseline freeze, documentation/source cleanup | Small active checkout; verified external evidence; working native/non-native builds |
| P1 | Optimize existing GR(1) and REAL-lifting implementations | Measured phase/resource improvements or a bounded negative result |
| P2 | Existing-arm ownership/locality fixes; gated sparse-backward and SIMD work | Independently admitted or rejected changes, without new physical arms |
| O5 checkpoint | Freeze the optimized selected five-arm mix | Same membership/order; correctness and focused regression screens pass |
| P3 | Implement and locally optimize UNREAL lifting | Exact-target verified prototype, attribution tests, useful screen result |
| P4 | Selected standalone legs and actual portfolio comparisons; closure | Measured admission decisions and an honest final report |

Optimization-first is not an unbounded demand to solve every memory problem. Finish concrete P1 fixes and P2's small ownership fixes; run census/limited probes for speculative items and stop those that fail their gates. Then proceed to P3. A failed sparse or SIMD experiment must not indefinitely delay UNREAL lifting.

## 2. Scientific retention without a growing source repository

### 2.1 Recommended policy

**Keep current conclusions in Git; keep full measurements in versioned compressed release assets; keep only active/recent working data locally.** “Latest only” applies to the source checkout and default local cache, not to destruction of the scientific record.

| Material | Source Git repository | External campaign archive / local cache |
|---|---|---|
| Solver, tests, benchmark/reporting tools | Keep | Snapshot/version in archive for reproduction |
| Current aggregate scorecard and residual-gap explanation | Keep one maintained version | Preserve every closed campaign's original report |
| Campaign identity, archive locator/checksum, key binary hashes | Keep compact index/manifest | Keep complete per-run provenance |
| Raw observations, repeats, conflicts, thermal records, diagnostic traces | Do not track, including the newest run | Preserve unique evidence in closed archives |
| Per-instance summary TSV, cactus CSV, raw sidecar and generated plots | Normally regenerate; at most a small current presentation | Preserve originals for the initial migration; later retain canonically or regenerate deterministically |
| Minimal correctness/mutation/regression fixtures | Keep small, purposeful fixtures | Large reproducers and full checkpoints are opt-in assets |
| Frozen executables and build environments | Keep identity/provenance, not binaries | Archive measured binaries and needed environment/source information |
| Failed/abandoned scratch runs that support no decision | No | Discard after classification; do not archive indiscriminately |

A negative experiment supporting a stop/admission decision is evidence, not disposable scratch. Preserve the actual gain/loss rows, important errors, and corrections as well as headline successes. Do not erase an old result just because a newer run exists.

### 2.2 Why zipping files inside Git is not the main solution

Git already compresses objects and can delta-compress similar versions. A newly committed ZIP/tarball does not remove the raw versions from earlier reachable commits; repeatedly replacing a compressed snapshot can also make inter-version delta sharing less effective. Compression is useful **for assets outside Git**, not as a substitute for separating data from source. [G1–G2]

Deleting old data at HEAD reduces checkout clutter but does not remove its reachable Git history. `git gc` can repack objects; it does not make reachable historical data disappear. A normal incremental fetch does not download every old object again, but continuing to add campaign data creates continuing transfer growth, and full clones include the reachable histories requested. Measure these effects separately. [G1–G3]

Do not propose a data branch in the same remote as a complete fix: ordinary clones/fetch refspecs can include that branch. A separate repository helps only when it is not fetched automatically. Git LFS stores pointer files rather than bulk blobs in Git, but introduces another data-transfer/storage workflow and does not by itself remove pre-existing raw blobs. Since these measurements are not needed to build/run Acacia, release assets plus explicit retrieval are the simpler default here. [G3–G4]

### 2.3 Storage destination and archive shape

Use GitHub release **assets** as the initial external store. GitHub explicitly supports distributing large files this way rather than tracking them. Do not confuse manually uploaded evidence archives with the automatically generated source ZIP/tarball. Do not mark a data-only release as the latest software release. Use a unique campaign tag/asset name and a write-once policy; publication can be through an existing release or a clearly labeled evidence release, without changing software release automation. [G1, G5]

Use one deterministic `.tar.gz` per bounded campaign bundle as the low-dependency default. Use zstd only if the project already supports it or the additional retrieval dependency is explicitly accepted. Compression runs outside timed solver experiments. Split very large campaigns into independently retrievable rows/proofs/binaries bundles; verify the provider's current asset limits rather than building a monolithic archive at the limit. The release documentation currently states a sub-2-GiB individual-asset limit. [G5]

Each archive must contain or reference:

- All authoritative observations/repetitions, outcome subtypes, relevant stderr, conflicts/adjudications, and calibration/thermal evidence used in a decision.
- Exact corpus identity and generation/obfuscation provenance, binary SHA-256, source and dependency revisions, build flags, arm order, runner/reporter versions, commands, caps, and host regime.
- A manifest of member paths, byte lengths, and SHA-256 values; the original campaign report/corrections; enough sources or frozen tools to rerun reporting. Preserve measured binaries externally where required, not only a recipe that cannot reconstruct their bytes.
- A stated completeness/status field: a stopped campaign is not promoted into a full evaluation. Do not resurrect the discarded 80-row run as a valid result.

Keep a small in-Git index entry with campaign ID, status, archive URL/asset identity, archive digest and size, relevant source/binary identities, and what conclusion it supports. A current pointer may advance; historical asset bytes must not be silently replaced. An amended campaign gets a new asset/version and explicit correction. Do not put a self-referential archive digest inside the bytes whose digest it defines.

GitHub Actions artifacts are suitable for temporary CI outputs, not the sole durable store: they expire under the retention policy (90 days by default in the documentation). A separately maintained institutional archive or a research-data repository can mirror publication-grade bundles; it is not a prerequisite for this sprint. Keep an independent backup of irreplaceable campaigns. Do not create new accounts/services as a hidden dependency. [G6]

### 2.4 Migration: publish and restore-test before deleting

**Inventory first.** Measure tracked HEAD bytes by category/path, object/history size, new objects reachable from this PR stack, packed size, and a reproducible fresh-clone/fetch test. Report submodule costs separately. `git count-objects -vH`, `git ls-tree -r -l`, and object-size inspection are starting points, not a network-transfer benchmark. Work in a complete disposable audit clone for historical inspection; do not accidentally hydrate an enormous partial clone on the timing host.

There is a concrete duplication pattern in the latest data: each measured arm has the original table plus derived summary/CSV/raw-sidecar outputs and logs. The inspected arm-1 directory alone has six such files totaling 954,615 uncompressed bytes. That is neither its packed Git contribution nor a whole-repository size estimate. Start the audit there and at older campaign/proof/generated-data directories. [S14]

**Package existing evidence conservatively.** For the first migration, preserve all original files needed by old reports byte-for-byte, even when some exports look redundant. Verify which outputs are reproducible before deciding future retention. Do not silently normalize old evidence or throw away a raw field merely because a cactus CSV omits it. Archive chronological corrections and known bad measurements with their labels.

**Publish, read back, restore.** Upload only to an authorized location with appropriate visibility and redistribution rights. Download into a fresh directory, verify the outer digest and each member, rerun the archived reporter/selection calculation, and compare known counts/PAR-2 to the recorded result. Missing files or expired/private links are a failed migration, not permission to delete the original. Protect retained measured binaries and source references throughout.

**Then untrack bulk data.** Update active reporting tools to accept an explicit `--evidence-root`/archive ID, and direct all new measurements to an ignored local output/cache directory or a directory outside the checkout. Remove bulk files from the current tree only after the restore test passes. Repair active links and leave compact campaign-index entries. Old commit links may remain historical pointers, but must not be the only durable location relied on before any history cleanup.

**Update pruning protection before moving provenance.** The existing pruning policy protects read-only directories and hashes found in committed records. Moving those records out of Git must not suddenly make frozen binaries disposable. Keep a compact baseline/binary registry and make the pruning tool read it and the evidence index. Protect active campaigns, unadjudicated failures, required baselines, dirty worktrees, and the only local copy of unpublished data. An external archive must be verified before it can release a local retention pin. [S13]

**Automate the narrow workflow, not a data platform.** Extend existing tooling, or add one small evidence command with pack/verify/fetch subcommands. Network access must be explicit and absent from normal builds, unit tests, and solver invocations. Fetch verifies integrity before extraction, rejects unsafe paths/links, and enforces declared extraction-size bounds. Do not execute downloaded scripts implicitly. Stable relative archive layouts must replace host-specific `/home/...` paths in active consumers.

### 2.5 Keep the repository from growing again

Update `CLAUDE.md` and `benchmarking/README.md`: “durable evidence” now means a checked external archive plus a committed manifest, not every raw row in Git.

Add a lightweight CI/pre-commit guard for **generated evidence**, with explicit small fixture exceptions. Suggested initial policy: no new raw campaign/proof/thermal/log bundles in Git, no compressed archive used to bypass that rule, a 256-KiB soft review threshold for individual generated report/fixture blobs, and a 2-MiB budget for the current tracked generated-evidence presentation. These are proposed engineering limits, not measured optimums; inventory and record justified fixture exceptions, without blanket exemptions for all of `benchmarking/`.

Check newly reachable blobs in the PR's commit range as well as the final diff: adding a huge file and deleting it in a later commit still grows history. Count aggregate growth and old-path renames, not just individual large files. Existing inherited history is reported, not allowed to make the guard permanently fail; new bulk additions must not be grandfathered. Test the guard on raw, compressed, renamed, and add-then-delete examples.

For local storage, default to keeping the active campaign, its pinned baseline, and the most recent completed campaign in the retrieval cache. Older verified archives can be evicted from local cache. Never auto-evict active/unpublished evidence or required frozen binaries. A short retention period for unclassified scratch must be an explicit policy, not a glob matching evidence directories.

### 2.6 Stop future growth separately from removing existing Git history

There are two distinct deliverables:

**Immediate, non-destructive:** externalize evidence, clean HEAD, prevent new raw-data commits, and provide opt-in shallow or `--filter=blob:none` clone instructions. These improve appropriate checkout/download paths; they do not promise that old full-history clones become small. A blobless checkout still downloads the blobs needed for files it checks out. Verify version-generation/submodule workflows before changing CI clone depth. [G3]

**Before merging the still-open stack:** prepare a clean integration branch based on the intended mainline, containing the reviewed final solver/test/tool changes and small manifests, but not raw evidence or the evidence-heavy intermediate commits as ancestors. A squash/curated transplant must cover the whole unmerged stack (#190–#192), not just squash #192 onto its already-bloated parent branch. Do not merge the dirty history back into the clean branch. Verify solver-relevant source/submodule equivalence and rerun correctness; preserve actual old measured hashes rather than rewriting provenance strings. This is a proposed landing arrangement, not authorization to replace PRs or force-push them.

Old remote branches/tags still make their histories reachable to consumers that fetch them. After externally backing up the stack, the owner can retire superseded refs deliberately; do not create “archive” tags on bulky commits in the main remote and claim ordinary clone size has been solved. PR-specific/server-retained references and existing clones are separate concerns. A release tag on a cleaned commit can carry external evidence for an earlier measured commit; its manifest must name that measured commit accurately.

**Already merged history:** prepare a disposable-clone analysis and an explicit path/object removal plan using `git filter-repo` only if the retained historical footprint warrants the disruption. Rewriting changes commit IDs and affects tags, signatures, open PRs, links, and collaborators' clones. Preserve an offline/external history backup and old-to-new mapping; coordinate a maintenance window. Do not rewrite `master`, published tags, or active PRs automatically. Do not delete all of `benchmarking/`, which also contains reusable code and contracts. GitHub's sensitive-data purge process is not a promise to garbage-collect non-sensitive benchmark data. [G1, G7]

Acceptance distinguishes HEAD byte reduction, future fetch-growth prevention, clean-branch/full-clone effects, and remaining historical bytes. Report what was actually measured; moving files or gzipping them is not evidence that Git history shrank.

## 3. P0 — Source and documentation cleanup

Freeze E5 and record exact options, build/dependency/toolchain identities, launch order, helper threads, corpus/runner manifests, and resource regime. Keep the actual shipping four-arm configuration as **S4** for release-regression checks. The current selected E5 is not a completed full-corpus baseline; use its frozen executable for paired screens now and final baseline measurement later.

Audit worktrees against master and open PRs, including the detached timing workspace recorded in the latest log. Keep dirty/untracked work, unmerged work, current measurements, and pinned artifacts. Classify obsolete clean worktrees; preserve otherwise-unreachable commits in an external/local backup before removal. Use the existing dry-run tool. Do not force-remove worktrees, silently delete remote branches, or run destructive history operations. External archival is not authority to delete active work.

Consolidate living markdown to the user README, contributor contract, one architecture reference, measurement protocol, current result/gap report, and one active sprint record. Historical campaign narratives move with their archives after verification. Keep compact index entries and distilled negative findings so rejected ideas are not rediscovered. No new markdown for each agent brief, review round, or microbenchmark. Audit tracked `build_scratch/` explicitly; preserve reusable tools before removing scratch.

Move non-template native orchestration out of large arm headers. Share small RAII/resource/diagnostic helpers, keep source/proof binding auditable, and move fault injection into test support absent from release builds. Separate arm parsing, process lifecycle, and dispatch without introducing a framework or changing ordering. Preserve process-group killing, reaping, cancellation, and rejection of late answers.

Keep supported legacy/TLSF/formula/synthesis behavior. The native arms are decision-only and require TLSF in the inspected implementation; do not claim a decision certificate is an emitted controller or silently break `-f`, `-F`, or `-s` when a later default is changed. Audit supported nondefault consumers before deleting an option. Isolate genuinely unadmitted research implementations from production linking where possible; retain useful independent oracles outside the shipped solver path. [S2–S3, S7–S9]

The configuration registry remains authoritative; keep Meson options, JSON declarations, and generated-header fallbacks consistent. Avoid a repository-wide formatting churn, virtual dispatch in hot templates, and multiplication of fixed-size vector instantiations. Measure build time, compiler peak memory, text size, and smoke performance. LTO/code-placement regressions still count even for a semantic no-op. Freeze accepted cleanup as C5, retaining E5 for cumulative comparisons.

## 4. Invariants for every subsequent package

**Genericity:** structural conditions such as declared parameters, ownership, reducibility, rank depth, support, and bounded separability can guide one uniform algorithm. Never use familiar names, known source hashes, canonicalized family fingerprints, campaign labels, or hardcoded family templates. Learned schemas from this input are allowed. Test renamed parameter declarations, signals, basenames, and crossed ownership-looking names. Resource/order effects near a cap are not automatically name-dependent logic; diagnose them separately.

**Three different checks:** generic algorithm design; trusted derivation/binding of the target game from immutable source plus parameter valuation and semantics; independent certificate validity for that game. A self-consistent bundle of hashes can still contain the wrong game. Hashes support byte identity/provenance, not semantic reduction or a winning argument. They are allowed in evidence manifests and current-invocation artifact binding, not benchmark dispatch. Different renamed source bytes should have different source hashes.

**Polarity:** REAL keeps only semantics licensed by the existing API; UNREAL lifting requires exact reduction and an independently checked environment proof. A strict reduction, failed REAL certificate, UNKNOWN, or seed verdict is not an UNREAL proof. Preserve initial-state quantification, move timing, ownership, assumptions, guarantees, and progress.

**Accounting:** all parsing/expansion, seed work, candidate construction, checking, copies, waiting, and cleanup belong to one absolute monotonic deadline and invocation memory limit. Every nonsolve is charged as a nonsolve. Keep workers killable; never initialize threaded BDD managers in the parent then fork them to claim sharing. Do not introduce mandatory sequential lifting before the existing race. Benchmark identity/status corrections belong in scoring infrastructure, never solver dispatch.

## 5. P1 — Optimize existing native routes before new arm comparisons (#194)

### 5.1 Measure the current native implementation, not historical Python costs

Use opt-in phase records on a fixed development panel: successful direct REAL cases, opposite-polarity solves, unsupported inputs, REAL-lift successes/unique success, representative declines, and memory-heavy/near-cap cases. Measure standalone and selected-portfolio interference. Separate source/provenance, reduction/monitor construction, seed search/solve, schema learning/instantiation, policy export, independent check, and teardown. Record allocation/live-object information as well as wall time.

The historical Python sample of slow unsuccessful lifting motivates a question, not a claim about the native path. The new arm-7 leg establishes limited marginal coverage, not its cause. Diagnose the actual native outcomes before tightening caps or revisiting a failed idea. Do not tune to file/family identity or to the eventual evaluation labels. [S1, S4]

### 5.2 Concrete duplicate-work and lifetime fixes

Audit the direct REAL arm's copy-then-free game/certificate/policy strings, unused strategy objects, and frontend/reduction owners surviving until checker teardown. Use explicit owned buffers or safe borrowed spans, and release unneeded phase state before checker allocation. Preserve every byte needed for binding and independent checking. Measure actual resident memory, not only C++ destruction: allocators and GC threads can retain pages. [S7]

The direct routes solve/export before checking the requested polarity. If opposite-side proof export is costly, expose a compatible solve/result boundary so REAL-only execution can decline before exporting an unused environment proof. It must not start answering the opposite polarity implicitly.

Inspect the native REAL lift lifetime graph. In the inspected tlsf-tools code, schema/learned objects remain live while `prove()` invokes the checker. Separate candidate serialization from verification where feasible so generation state is no longer needed when checker state is created. Preserve fallback behavior; do not reintroduce huge regeneration costs or assume the Python child-exit memory savings survived the native port. [S8–S9]

Attribute source parsing before the race: the legacy frontend is invoked during argument handling for a mixed portfolio. Moving it is conditional on measured startup cost and correct ownership; do not replace one shared conversion with five redundant copies. Ensure the parent still enforces the true deadline if parsing is expensive. [S3]

### 5.3 Fail-fast REAL lifting, then reuse the policy for UNREAL where appropriate

Bound seed-window exploration, per-seed solving, monitor growth, rank-depth/alignment checks, arity/subset enumeration, and candidate size with a small set of global development-chosen limits. Prefer cheap structural rejection before expensive seed work. Retain slow successful cases in the development stress panel; a shorter timeout is not automatically a better algorithm.

Preserve target-derived moves and learned semantic predicates; avoid overgeneralizing arbitrary seed Skolemization. Declines need precise stages and work counts. Saved core-seconds or peak memory on UNKNOWNs matter through reduced portfolio interference; they do not themselves change PAR-2 penalties.

For decision-only system lifting, compare policy-first with a bounded region-first alternative where the existing checker supports it. Choose globally or by documented generic operation costs, not a family list. Do not assume environment region checking exists. Test all supported check modes and metadata independently. REAL lifting remains experimental during these optimizations; improve it before deciding whether it earns a slot.

### 5.4 The repeated-check issue, without weakening the proof boundary

Direct `real:gr1:oxidd` has one visible solve/export/final-check path. The inspected REAL lifting API checks internally; Acacia then re-reduces the immutable source for game binding and checks the returned candidate again. Attribute these separately:

```text
internal check                    candidate validity for G
source re-reduction / comparison   binding G to this input
outer check                       repeat verification of returned C for G
```

Possible optimization: trusted source reduction creates an immutable target context; candidate generation cannot replace its game; one final independent check consumes that context and candidate. Preserve the public verified-lift convenience API as a compatible wrapper. An existing source hash in a returned object is not a replacement for trusted derivation/ownership, and an opaque pointer is not a mathematical proof. Keep the swapped-game-with-recomputed-hashes mutation test. [S7–S9]

The goal is **no unnecessary repeat check of the same final candidate**, not one checker call per invocation regardless of failed candidates or fallback. If a small auditable refactor cannot preserve the boundary, retain the conservative check. This optional refactor does not block the optimized checkpoint or UNREAL lifting.

### 5.5 Memory budgets and backend-locality work

The inspected direct-arm limits are `2^22` nodes and `2^20` cache entries; REAL-lift defaults permit `2^25` solver nodes, `2^26` checker nodes, and `2^23` caches. These are count limits, not measured bytes or necessarily immediate allocations. Measure creation/growth/retention and choose a small global arm/phase budget policy. A per-child 8-GiB address-space cap is not a split of an 8-GiB whole-invocation physical-memory budget. Account for parent/shared pages, all physical workers/helper threads, artifacts, stacks, and reserve. Keep the cgroup hard boundary; a soft allocation model is not a guarantee. [S7–S8]

Use actual cgroup peak/OOM records and per-child RSS/PSS carefully, without double-counting shared pages. Apply Spot aborters/state bounds where supported; cooperative checks do not interrupt every library call. Add generic checked-size/allocation failure handling for early `bad_alloc` cases instead of benchmark exceptions.

Profile remaining AIG import/export, support traversal, and role/owner indexing. Replace repeated map/set construction or repeated support work only where measured, with compact stable IDs/arrays or bounded reusable caches. Do not carry useless roots/import caches across phases. Validate same-thread repeated-manager creation/destruction and the pinned OxiDD lifetime fixes; an already-submitted upstream patch is not new work.

Do not coalesce REAL/UNREAL direct workers as a priority: E5 contains only one of them. Do not merge BuDDy and OxiDD first. Shared-library count is not evidence of duplicate unique-table contents; remove redundant computation, imports, retained roots, and excess caches before a backend replacement.

## 6. P2 — Improve current legacy arms before expanding the race

### 6.1 Remove unnecessary copying and allocation first

In `k_bounded_safety_aut.hh`:

```cpp
auto input_output_fwd_actions = actioner.actions();
```

The inspected actioners return mutable references, so this copies the nested table while the actioner retains another copy. Test one-owner storage with mutable borrowing or explicit transfer. The critical picker splices successful actions to the front; a blind const view is insufficient. Preserve action construction/deduplication order, mutation, stable references, lifetime, K changes, both application directions, and synthesis consumers. Check all actioner variants before shortening frontend/transition-table lifetimes. Attribute copy removal independently. [S10–S11]

The critical picker also builds a vector of references and then an outer linked list of the same references on every invocation. That outer list is traversed; the splice is on an inner action list. Test contiguous reference traversal/reusable scratch without changing witness order or returning a dangling reference. Audit `std::move` of const results, which can still copy. Measure allocations, copied bytes, and the actual selected arm. [S11]

### 6.2 Optional payload locality, not a search-policy change

After copy removal, separate stable action IDs and row/endpoint payloads from the small mutable order structure. Try only the AoS/SoA/offset layout justified by the access profile. A source-major index can aggregate backward minima instead of scattering, but forward application is still used by the picker. Compare one payload plus compact indexing with duplicating both orientations; do not cancel memory savings by keeping two edge copies.

Preserve ordering, tie-breaking, RNG consumption, predecessor traces, and strategy extraction. This is not a revival of the rejected sparse-forward compaction package. No representation redesign is required unless the small ownership fixes leave a substantial measured bottleneck.

### 6.3 Sparse backward vectors (#196): census, one implementation, stop/go

Measure both non-`-1` density and deviations from the safe ceiling, separately for numeric/Boolean coordinates, stored generators/temporary predecessors, across fixpoint iterations and K raises. Include comparison-weighted work, dense scratch reset, conversion, indexed-read and normalization costs. The safe initial generator and backward scratch start at ceilings (`K-1` numeric, `0` Boolean), not bottom. Sparse forward success does not establish backward bottom-sparsity. [S6, S10–S12]

If justified, implement dimension plus contiguous sorted unique `(index,value)` pairs, implicit `-1`, in the reusable posets layer where appropriate. No linked node per coordinate and no new downset semantics. For bottom supports:

```text
x <= y  iff  supp(x) subset supp(y) and x_i <= y_i on supp(x).
```

A merge costs `O(nnz(x)+nnz(y))` in general. Implement both mixed sparse/dense comparison directions. Meet is intersection/minimum, coordinatewise join union/maximum; downset union is maximal-antichain union, not componentwise join. All-bottom is a vector, not the empty downset. Hash/equality and dimension/Boolean/K contracts must agree across representations.

For the inspected backward action, source q, destination p and acceptance increment e:

```text
b_q = min(U_q, min over transitions (q,p,e) of max(-1, m_p - e)).
```

No transitions leave the safe ceiling `U_q`. An omitted destination bound equals `-1` and still constrains every relevant predecessor: iterating only stored coordinates and ignoring the complement is wrong. A `-1` on the left of an order comparison needs no extra test, but a `-1` in a maximal generator restricts its downclosure. It is not “don't care.” [S10–S11]

The current operator still has dense scratch and indexed reads. A sparse type alone can add searches/conversions without removing any dense work. A reusable dense scratch rank may be the best first implementation; measure rather than promise sparse-native speed.

K raising adds the delta to **every numeric coordinate, including omitted `-1`**, and resets Boolean coordinates to zero. Thus an implicit `-1` becomes `delta-1` and can densify storage. Preserve this operation exactly; changing it needs a separate proof and measurement. [S10]

Differential-test intermediate vectors/downsets/predecessors, random canonical normalization, both order directions, missing rows, accepting edges, empty/all-bottom cases, Boolean tails, maximum K, repeated raises, and strategy extraction. Keep the dense oracle and default available. A hybrid with one disclosed global threshold/hysteresis is conditional on a useful sparse regime and a demonstrated dense penalty; do not build it speculatively. Stop at census or prototype if gains do not survive selected-arm/portfolio measurement.

### 6.4 SIMD after the ownership/layout decisions

Choose a few hot operations: dense dominance/equality, componentwise min/max, rank updates, support/Boolean scans, or reductions over compact transition payloads. Fuse passes only if summary work is actually consumed. Sparse kernels need enough support/batch size to amortize setup; irregular BDD unique-table operations are not automatically SIMD-friendly.

Keep scalar oracles and existing ISA/build mechanisms; no extra family of fixed-width template instantiations. Test signed `-1`, widening before saturation, all alignment/tail lengths, numeric/Boolean boundaries, and extreme K. K raising is not forward saturation. Report cycles, instructions, cache/TLB misses, branches, bytes moved and allocations where available, then deployed outcomes. Prefetching, huge pages, global allocators and unbounded arenas need independent evidence.

### 6.5 New evidence-directed limit probe

The latest log identifies the same three large specifications failing in two legacy UNREAL legs because Spot was built with a 64-acceptance-set limit; it also records that ltlsynt answers them rapidly. This is a focused new lead, not a promised coverage gain. Reproduce the limit generically and inspect where it arises. Test a coherent higher-limit Spot/dependency build, or a semantics-preserving way to avoid an unnecessary construction, on a tiny targeted panel plus ordinary controls. A larger acceptance representation may slow common cases, so do not raise it globally without measuring the unchanged selected portfolio. Avoid incompatible library/header build configurations. No benchmark-name workaround. [S1]

This probe may stop with a measured result and must not become an unrestricted Spot fork/translation project.

## 7. O5 checkpoint before new arms or expensive new legs

Freeze the cumulative optimized binary with exactly E5's five arms/order as **O5**. Record accepted/rejected P1/P2 packages separately. Screen against frozen E5 with the existing deterministic panels and targeted 60-second adjudications; do not demand a new seven-arm full census or resume the stopped final campaign to reach this checkpoint.

Run unchanged-mode correctness, native/legacy capability tests, proof mutations, scalar/dense differential tests, and whole-invocation memory/timeout checks. Confirm near-cap gains/losses with the repository protocol. Retain E5 and the original actual shipping configuration for cumulative regressions. Screens establish the checkpoint's suitability for development, not a full-corpus performance claim.

Also freeze the optimized existing REAL-lifting implementation/configuration and its development results. It is allowed to remain outside O5. Do not lose the opportunity to reconsider it later just because the earlier unoptimized version had one unique solve. Only now start the new environment arm and schedule its complete leg after the prototype is ready.

## 8. P3 — UNREAL parameter lifting after the optimization checkpoint (#195)

### 8.1 First establish the current environment-certificate contract

Locate the existing exporter/checker/fixtures. Document initialization, legal environment actions, allowed policy dependencies, universal system-response obligations, ranks/progress, assumptions/guarantees, deadlocks and safety violations. Use the format actually supported; do not assume the system-side region-only checker supports environment witnesses. [S5, S8]

For an input-first Mealy round the environment chooses an input before seeing the current system output, so its step obligation has an existential-input/universal-system-response shape. An environment policy depending on that current output is invalid there. Follow actual target semantics, not a label swap. An invariant or an existential path is insufficient for an assumption-respecting liveness counterstrategy. Do not silently require a single fixed violated guarantee on every branch unless that is a justified, explicitly incomplete certificate restriction.

### 8.2 Four bounded milestones

**U0: replay.** Export small exact environment witnesses and independently replay them. Align fields/goals/modes with typed provenance, not names. Mutate assumptions, action dependencies, and progress to establish that the tests discriminate.

**U1: generalize.** Reuse optimized parameter-window, role-alignment, bounded-arity projection and instantiation machinery only where its contracts apply. Learn environment rank/layer predicates and admissible move information, not solely an arbitrary Skolemized seed counterstrategy. Keep polarity-specific proof logic explicit.

**U2: target proof.** Instantiate at the target valuation and derive legal, progress-compatible environment choices from the exact target transition relation, with all relevant system responses universally covered and causal dependencies respected. Construct the existing environment certificate/policy and check it independently. No new environment region-only format is required.

**U3: opt-in integration and local tuning.** Add `unreal:param-lift:oxidd` to existing parser/dispatch/configuration tests. It returns UNREAL only on exact-target verification, otherwise a structured decline. Demonstrate a target larger than its seeds and screen residual-workload value; a generated success is a correctness milestone, not default admission. Optimize obvious phase/ownership problems before its full 60-second leg.

### 8.3 Seeds and attribution

Use a finite global window and structural-regime checks; detect missing conjuncts/roles, inconsistent rank shape and polarity changes. Multiple parameters need an explicit domain: varying one declared axis while holding others at target is a legitimate restricted prototype, but not full multi-axis generalization. Decline unsupported cases rather than using familiar parameter names or magic seed values.

Seed agreement is not monotonicity or a cutoff proof. A small UNREAL instance can become REAL when the system gains resources. Only the exact target check establishes the target verdict. Include this polarity reversal in tests.

Small seeds may be solved directly. **Do not directly solve the target to create rank layers or as a hidden fallback.** Target reduction/checking are allowed and charged; target synthesis is not a lifted proof. Add test-only call attribution recording every solved valuation. Reuse existing source-binding checks if optional P1 API work was not admitted. Preserve API compatibility and ownership; the older `*_v1` issue wording is illustrative, not authorization to break unversioned public structs.

### 8.4 Required tests and stop conditions

Use several independently designed parametric generators, held-out size regimes, degenerate windows, multiple axes, no parameters, exact/strict distinctions, causal dependencies, assumption/vacuity cases, and supported Mealy/Moore boundaries. Compare small targets with the direct exact route or known outcomes.

Reject corrupted ranks/justice/memory updates, wrong side/method, duplicate decoded JSON keys, truncated artifacts, and a wrong game paired with recomputed self-consistent hashes. Test alpha-renaming including parameter declarations and crossed ownership-looking signal names. Keep deadline/tiny-cap and repeated manager-lifetime tests.

Report decline stage and work counts, not just UNKNOWN. Stop a bounded prototype when candidate shape or costs do not provide useful target coverage; preserve the negative evidence externally and do not leave a large unadmitted subsystem linked into production. UNREAL lifting remains a priority, but not an unconditional promise that it earns a default slot.

## 9. P4 — Delayed long legs, matched comparisons, and closure

### 9.1 Development measurements before campaigns

Use existing focused gates throughout optimization. Tune global budgets/thresholds on a disclosed development set, keep structural/parameter-size holdouts, and treat the heavily explored SYNTCOMP corpus as an incumbent-regression benchmark. Renamed copies are metamorphic genericity tests, not independent generalization data.

Do not rerun seven full standalone legs after each cleanup, pin bump, or kernel patch. Reuse old observations only for the exact binary/configuration/corpus/cap/runner/resource regime to which they belong. Old M1/M2 timings are evidence about those binaries, not measurements of O5. A full leg is warranted only for a stable changed route that survives screening and may affect the final portfolio choice.

No side builds/tests on the timing host during decisive runs. Record temperature/clock and memory conditions; the recent calibration showed throttle-event counts alone do not quantify slowdown. Keep no-swap and the existing whole-invocation limits; measure physical workers and helper threads, not just logical arm names. [S1]

### 9.2 Reconsider optimized REAL lifting as well as new UNREAL lifting

Let R be optimized REAL lifting, U the new environment lifting, and D the direct UNREAL GR(1) comparator built on the same relevant optimized substrate. Screen R and U separately for marginal value against O5. Run the justified standalone 60-second legs after those implementations stabilize, rather than pre-optimization. D needs fresh matched measurements where old timings/pins are incompatible; an unoptimized historical D is not a fair control for U.

From development evidence nominate at most a small number of replaceable slots and freeze the comparison list. First measure fixed-size portfolios:

```text
O5                                  optimized unchanged membership
O5 minus r plus R                   optimized REAL-lift candidate, if warranted
O5 minus s plus U                   UNREAL-lift candidate, if warranted
O5 minus the same s plus D           matched direct-UNREAL control
```

If both R and U show independent benefit, allow one preregistered combined replacement; do not repeat an unrestricted subset sweep. Do not automatically drop the direct REAL GR(1) arm or keep it regardless of evidence: any change needs explicit attribution against its exclusive wins. A six-arm race is a later contention experiment only under the same total CPU/memory allowance, not automatic admission.

A virtual best can nominate candidates, but all reported deployed results come from actual races. A lift arm need not dominate D everywhere; complementary wins may matter. Conversely, a unique toy/standalone win is insufficient when a same-slot direct arm or unchanged O5 does better in deployment.

### 9.3 One closing campaign, with an honest baseline

Plan the full-corpus runs once candidates are frozen. There is no completed full-corpus E5 evaluation to reuse: measure the frozen E5 invocation in the closing regime as the cumulative internal baseline. Compare O5/final candidates as required for attribution, and the actual shipping S4 where release-regression checks are needed. Avoid duplicating full controls when one genuinely identical frozen series answers both questions.

Close at 17 and 60 seconds with **ltlsynt / Acacia TACAS23 / new Acacia**, retaining E5 internally. Preserve and disclose the established legacy conversion timing boundary. Audit legacy crashes/wrong-answer/status corrections rather than trusting raw exit conventions blindly. Map known corpus status exceptions only in scoring; investigate additional conflicts. Deadline-dependent policies require genuine runs at their own cap; any valid censoring/reuse remains labeled derived.

For each series report solved/REAL/UNREAL, PAR-2 total and mean, failure subtypes, actual invocation peak memory, exclusive solves, paired gains/losses, and manifest identities. For lifting include seed versus target work, check method, decline categories, and target-solve attribution. For storage/kernels include allocation/conversion work and compile cost. A cactus alone is insufficient.

Admission requires correctness and confirmed deployed benefit without validated new coverage losses or additional memory failures under the project's gates. Maintainability and memory-only improvements can have explicit non-speed justifications; do not relabel them as PAR-2 wins. Recheck cumulative performance against E5 rather than allowing successive small regressions to disappear behind rolling baselines. Shipping-default changes happen only after an actual portfolio passes the final gates.

All new raw evidence goes to ignored/external staging. Publish a verified closed archive before evicting local measurements or updating the retained current report. If a campaign is stopped again, report the completed scope and preserve only properly labeled useful observations; never fill missing rows with synthetic solves or launch a large campaign merely to satisfy paperwork.

## 10. Reviewable deliverables and exit criteria

| Package | Deliverable | Must not be mixed in |
|---|---|---|
| P0a | Size audit, verified external archives, compact index, explicit retrieval, retention/CI guard | Unapproved history rewrite or loss of unique evidence |
| P0b | Bounded documentation/source/research-build cleanup; frozen C5 | New default membership or weakened proof semantics |
| P1 | Separately attributable existing-native and REAL-lift optimizations | New physical arm or premature full-leg census |
| P2a | Action-copy/reference-scratch fixes | Search-order or payload-semantics changes |
| P2b, conditional | Locality, sparse-backward, selected SIMD and acceptance-limit probes | Unbounded sweeps or template/build explosion |
| O5 | Optimized same-five-arm checkpoint plus rejected-package record | Calling the old virtual score a deployed measurement |
| P3 | Environment replay, generic lifting, exact-target check, opt-in arm | Hidden direct target solve or a mirrored unsound certificate |
| P4 | Stable-route legs, actual portfolio comparisons, three-way and issue disposition | Virtual-best-only admission or raw evidence back in Git |
| Landing proposal | Clean stack integration and, if justified, a separate history-reduction plan | Automatic force-push, remote-ref deletion, or rewritten provenance |

P0 finishes with an auditable distinction between files removed from HEAD and historical bytes still reachable. P1/P2 finish with finite admitted/rejected packages so UNREAL work proceeds. The sprint ends with one current plan/decision/closure record, small in-Git manifests, independently retrievable evidence, and no accumulation of abandoned production paths. Reusable tools remain in source control; data and per-campaign chatter do not.

## Source pointers

These are inspection/provenance references, not hardcoded runtime inputs. Acacia links are pinned to the refreshed snapshot; the unchanged implementation observations were checked against the prior snapshot and the three-commit comparison. Public platform documentation was consulted on 27 September 2026; publication limits and permissions should be rechecked when implementing uploads.

- **S1:** [PR #192](https://github.com/gaperez64/acacia-bonsai/pull/192); [updated decisions, selection and campaign stop](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/benchmarking/gr1-par2-20260923/decisions.md); [three-commit comparison](https://github.com/gaperez64/acacia-bonsai/compare/f74ad9d14bb8879550d1af0126d371ebfd4e6253...26cbb45f19df1cd1b2eb55f844680fefea4466bb).
- **S2:** [Preset registry](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/config/acacia-presets.json); [selected invocation and full frozen hash](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/benchmarking/gr1-par2-20260923/campaign/perarm-m1/smoke/final-portfolio/phase1-smoke.sh).
- **S3:** [Argument/default selection and capability checks](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/arg_parser.hh); [arm grammar](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/portfolio_arm.hh).
- **S4:** [Native optimization issue #194](https://github.com/gaperez64/acacia-bonsai/issues/194).
- **S5:** [Environment lifting issue #195](https://github.com/gaperez64/acacia-bonsai/issues/195).
- **S6:** [Sparse backward issue #196](https://github.com/gaperez64/acacia-bonsai/issues/196).
- **S7:** [Direct native arm](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/native_gr1_arm.hh).
- **S8:** [Lifting API](https://github.com/gaperez64/tlsf-tools/blob/a294419/include/tlsf/gr1_lift.h); [independent checker API](https://github.com/gaperez64/tlsf-tools/blob/a294419/include/tlsf/gr1_check.h).
- **S9:** [Acacia lift arm](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/native_param_lift_arm.hh); [tlsf-tools lift implementation](https://github.com/gaperez64/tlsf-tools/blob/a294419/src/lib/gr1_lift.cc).
- **S10:** [Backward engine and K raising](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/solver/k_bounded_safety_aut.hh).
- **S11:** [Standard actioner](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/actioners/standard.hh); [delegate actioner](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/actioners/no_ios_precomputation.hh); [critical picker](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/input_pickers/critical.hh).
- **S12:** [Sparse forward value type, not a backward drop-in](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/src/solver/sparse_forward_rank.hh).
- **S13:** [Contributor contract to revise](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/CLAUDE.md); [benchmark protocol to revise](https://github.com/gaperez64/acacia-bonsai/blob/26cbb45f19df1cd1b2eb55f844680fefea4466bb/benchmarking/README.md).
- **S14:** [Inspected arm-1 evidence files and byte sizes](https://api.github.com/repos/gaperez64/acacia-bonsai/contents/benchmarking/gr1-par2-20260923/campaign/perarm-m1/legs/arm-1?ref=26cbb45f19df1cd1b2eb55f844680fefea4466bb).
- **G1:** [GitHub: large files, release distribution, and history removal](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github).
- **G2:** [Git: compressed objects and delta packfiles](https://git-scm.com/book/en/v2/Git-Internals-Packfiles).
- **G3:** [Git clone: ref scope, shallow and partial-clone options](https://git-scm.com/docs/git-clone).
- **G4:** [GitHub: Git LFS pointer/object model](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage).
- **G5:** [GitHub releases and release-asset limits](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).
- **G6:** [GitHub Actions artifact expiry/retention](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts).
- **G7:** [GitHub: history-rewrite side effects and limits of server-side purge](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository). The side effects apply to this proposed cleanup; benchmark evidence is not being described as sensitive data.
