# Brief P2b — S1 owner indexing + support-restricted projection; explicit variable layout (§5.3)

Read plan.md §2 (esp. 6, 11), §5.2 last bullets, §5.3, §5A S0, S1, S4 (all of it — it is the spec),
and the committed S0 evidence: benchmarking/gr1-par2-20260923/s0/s0-summary-L0-run2.md (actual
support widths, owner-tuple arities, subset reuse, mask word lengths). Code:
generalize_gr1.py::{VarInfo, projection_templates, instantiate_templates, _support, Bdds.cube,
_ordered_subset} and the P2a GeneralizationAttemptContext. Branch sprint/gr1-par2-20260923; I commit.
One job for any compile; scratch under build_scratch/p2b/, never /tmp. Python .venv; the BDD
bindings via the configured interpreter; the BuDDy adapter via --buddy-adapter (build it with
native/build.py into build_scratch/p2b/adapter/ if needed).

Step 1 (S1, native-scalar first, in Python unless the S0 data shows Python-level word ops are the
cost): attempt-local owner index owner_groups[canonical_owner_tuple] -> sorted public var IDs
(tuples = sorted client IDs from provenance, never BDD levels); keep(S) = shared ∪ groups indexed by
subsets of S (≤ 2^k lookups, k from the capability's actual arity bound, never hardcoded into
soundness). Emit IDs in exactly the reference's deterministic order; preserve _ordered_subset slot
ordering and role/relation/lowest-index-anchor semantics. Projection quantifies drop = supp(f) \
keep(S) using an exact support cached per live root with an OWNING reference (never a node id that
can be collected and reused), mapped public-ID -> BDD var through the registered mapping (levels
can change under reordering). Remove the frozenset(subset) reconstruction inside the scan. Use
uint64 word masks only if S0 word-length data and a measurement show they beat sorted small lists;
if the owner index removes the hot scan, do NOT add a SIMD/mask variant (plan S1 last paragraph).
Report which representation you chose and why, with numbers from a tiny-instance micro-measure.
Tests (S1 Tests paragraph, all of them): reference set-based keep/drop and projected BDDs vs new,
0/1/63/64/65 clients, empty/shared/multi-owner groups, sparse high IDs, changed variable order,
identical roots with different mappings, GC, random permutation of semantic IDs.
Step 2 (§5.3): explicit attempt-local variable-layout object (public state/letters, canonical
templates, temporary composition vars, policy counter, policy game coordinates) replacing the
8192-universe quarter/half blocks and hardcoded curr_base=512 / policy_game_base=1024; checked
arithmetic; reject overlap/overflow before BDD work. Tests with artificially small limits, more
counter vars than the old interval, overlapping replacements, multi-instance manager use.
Semantic equivalence: certificates/policies BDD-equivalent to HEAD on the generalizer suite
instances. Each step a separate patch in build_scratch/p2b/stepN.patch + note. Full
test_generalize_gr1.py and the other suites green; ruff clean on touched files. Use --diagnostics
on tiny instances to show projection/support time and counts before/after. VERDICT at the end.

MACHINE MAY BE RUNNING TIMED MEASUREMENTS: before compiling anything or running test_generalize_gr1.py
or any other heavy work, wait until build_scratch/seq2/progress.txt contains a line starting with
"SEQ2 DONE" (poll every 60 s with sleep). Reading code, editing, and fast unit tests are fine meanwhile.
