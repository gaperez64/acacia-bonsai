# Downset and state-vector representations

What the antichain and its vectors are actually made of, and four attempts to
change it. Two static reviews of the same commit open the file: they were written
independently and reached compatible conclusions, so they are kept as two
readings rather than merged into one voice.

## What is in here

- [FORWARD-DATA-STRUCTURES-REVIEW.md](#forward-data-structures-review) — access patterns and SIMD in the forward structures.
- [SPRINT-CODE-REVIEW.md](#sprint-code-review) — portfolio arms and the forward safety backend, reviewed at the same commit.
- [STATE-VECTOR-TAIL-STUDY.md](#state-vector-tail-study) — zero-tail wrapper versus bare vector — the wrapper does not earn its keep.
- [TLSF-NORMALIZATION-STUDY.md](#tlsf-normalization-study) — TLSF normalization does not produce smaller or more solvable automata.


---

## Forward data structures: access pattern and SIMD review

<sub>Was `benchmarking/FORWARD-DATA-STRUCTURES-REVIEW.md`. Read-only static review of `sprint/p5-portfolio-arms` at `1d506d43`, 2026-09-04. No build was run.</sub>

Date: 2026-09-04  
Branch reviewed: `sprint/p5-portfolio-arms` at `1d506d43`  
Method: read-only static analysis. No build, test, Meson command, benchmark, or
project binary was run.

### Executive call

The SIMD hypothesis is **half right**.

The invalidation predicate really is a broadcast dominance query: hold one new
minimal losing generator `l` fixed and find every still-live environment node
`r_i` for which `l <= r_i` pointwise. That shape has data parallelism across
nodes. But the normal forward instantiation is not doing scalar coordinate
comparisons today. It inherits `VECTOR_IMPL`, normally
`simd_vector_backed<signed char>`, and every call to `State::partial_order()` is
already SIMD across the coordinates of one pair. What is missing is SIMD (or
bit-parallel filtering) **across the N environment nodes**. The present
heap-per-rank, vector-of-node-structs layout prevents that bulk access pattern.

The highest-value move is therefore **(d): add a forward-specific, stable-ID,
live dominance-reporting side index over all environment nodes**. It must answer
“report all live IDs dominated by this generator,” not just existential
membership. The best first implementation is an append-friendly live bitmap
plus a small number of coordinate-threshold bitmaps, followed by exact
verification with the existing SIMD-capable `State::partial_order()`. This is
the forward analogue of the filtering idea in Posets'
`filtered_vector_backed`, but it is not a Posets downset and should initially
live beside the forward graph while its payoff is established.

Do **not** make `minimal_upset_antichain<State>` the performance project. That
would be a reasonable library abstraction for the small generator set, but its
`subsumes/insert/size` interface cannot report the visited node IDs that need
invalidation. It addresses the structure that peaked at 177, not the structure
responsible for 148,504,205 checks. Likewise, adopting an existing downset is
the wrong semantic and operational fit.

A packed/tiled rank matrix and an across-node SIMD kernel are the second choice,
not the first. They can reduce the constant factor if bitmap filtering is weak,
but they retain work proportional to all live nodes times all relevant
coordinates. The 22,261:1 checked-to-invalidated ratio argues first for deleting
candidate work, then for making the surviving comparisons faster.

### 1. What the forward solver actually does

#### Graph and work scheduling

One fixed-K attempt owns a fresh `forward_search`; its graph, interner,
antichain, and queues are discarded before K changes
(`src/solver/forward_k_bounded_safety_aut.hh:23-28,77-103`). The search is an
event-driven loop: drain all pending losses, test the initial node, then expand
one queued environment or controller node (`src/solver/forward_reachable_safety.hh:274-298`).

The retained graph is two flat arrays plus ID-based edges:

- `env_nodes` is `std::vector<forward_env_node<State>>`, `ctrl_nodes` is a
  separate `std::vector`, and queues contain a node kind plus numeric ID
  (`src/solver/forward_reachable_safety.hh:242-257,310-323`).
- An environment record embeds its `State rank`, status, child controller IDs,
  reverse `selected_by` IDs, and proof ID
  (`src/solver/forward_game_nodes.hh:19-31`).
- A controller record embeds its parent environment ID, input index, monotone
  action cursor, current selected successor, tried environment IDs, and proof
  ID (`src/solver/forward_game_nodes.hh:33-48`).

Environment expansion is append-oriented: for every input class it appends a
controller, appends the controller ID to the parent environment, and queues the
new controller (`src/solver/forward_reachable_safety.hh:707-760`). Controller
expansion is intentionally lazy. It applies actions one at a time, returns as
soon as it finds one viable selected successor, and resumes at the next action
only after that successor loses (`src/solver/forward_reachable_safety.hh:877-1052`).
Loss propagation normally follows the reverse `selected_by` lists, so it visits
controllers actually depending on the lost environment rather than the whole
controller array (`src/solver/forward_reachable_safety.hh:1064-1109`).

These accesses are mostly append/sequential for construction, ID-indirected for
propagation, and one-successor-at-a-time for `advance_controller`. They do not
look like downset algebra.

#### Interning

Interning first computes a coordinate-wise 64-bit hash, including the vector
length (`src/solver/forward_reachable_safety.hh:149-159`). The hash selects an
`unordered_map<uint64_t, vector<env_id>>` bucket; exact equality against the
bucket's environment ranks remains mandatory for collision safety
(`src/solver/forward_reachable_safety.hh:511-528`). A miss appends the whole
environment record and then appends its ID to the hash bucket
(`src/solver/forward_reachable_safety.hh:658-695`).

That is a one-query/one-small-bucket equality workload. It shares the same rank
payload with invalidation but not its broadcast access pattern. Packing ranks
may improve locality here later, but a dominance index should not be forced to
replace the exact hash interner in its first version.

#### The two different dominance workloads

There are two importantly different structures in the forward solver.

1. The **minimal losing generator antichain** is a small flat vector. A query
   computes the candidate's coordinate sum and scans generators whose sum can
   still satisfy the pointwise order; insertion performs the dual scan and
   compacts away nonminimal generators
   (`src/solver/minimal_losing_antichain.hh:39-83,87-102`). The forward search
   consults it when expanding an environment and while testing controller
   successors (`src/solver/forward_reachable_safety.hh:707-720,999-1006`). This
   is existential: it may return on the first witness.

2. The **visited environment set** is not an antichain. When a safe losing
   environment is a genuinely new minimal generator, the solver first updates
   the small antichain and its proof-generator IDs
   (`src/solver/forward_reachable_safety.hh:584-618`). It then scans every
   environment ID created so far. Each losing record is skipped; every other
   rank is tested with `generator.partial_order(candidate).leq()`, and every
   match is marked losing with the new generator as witness
   (`src/solver/forward_reachable_safety.hh:620-635`). This is report-all, not
   existential.

The latter is the measured hot path. One generator is invariant for the entire
scan, candidate IDs are traversed in increasing order, matches are sparse, and
matching triggers proof and queue side effects. A useful precision about the
existing counter is that `nodes_checked` is incremented **before** the losing
status test (`src/solver/forward_reachable_safety.hh:624-628`). Thus
148,504,205 `nodes_checked` is not necessarily 148,504,205 full pointwise
comparisons: it also includes already-losing tombstones. The supplied
measurements establish that the scan dominates, but they do not separate status
skips, full partial-order calls, or coordinate/SIMD blocks examined.

The scan can safely be split into two phases: compute the matching live IDs,
then call `mark_environment_losing` on them in ascending ID order. The current
comment already records why a recursively attempted antichain insertion cannot
start another scan: the current generator subsumes every match
(`src/solver/forward_reachable_safety.hh:620-623`). Preserving ascending ID
order also preserves deterministic proof allocation and queue order.

### 2. Contrast with the backward/Posets regime

The concrete solver type is assembled as
`Vector = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>` and
`Downset = VECTOR_AND_BITSET_DOWNSET_IMPL<Vector>`
(`src/solver/solve_game_vector.cc:13-21`). The reviewed forward preset inherits
the rank-bucketed preset (`config/acacia-presets.json:81-84,103-127`), but the
forward search uses `SetOfStates` only to obtain its `value_type`; the selected
downset does not store its graph. The downset reappears only when a winning
strategy rank list is converted to the return type
(`src/solver/forward_reachable_safety.hh:242-245` and
`src/solver/forward_k_bounded_safety_aut.hh:155-156`). In other words, the
forward path indirectly inherits the Posets **vector representation**, but not
the selected Posets **downset data structure**.

The backward algorithm repeatedly maintains a downward-closed winning region
by its maximal antichain. For one selected input, each output action is applied
backward to every current maximum, the result downsets are unioned, and the
predecessor region is intersected into the current region
(`src/solver/k_bounded_safety_aut.hh:410-494`). The outer fixed point repeatedly
does that CPre and asks whether the initial rank remains contained
(`src/solver/k_bounded_safety_aut.hh:206-306`). Posets' `Downset` concept is
built around exactly these operations: `contains`, `insert` through
construction, `apply`, `union_with`, `intersect_with`, iteration, and access to
the backing maxima (`subprojects/posets/include/posets/concepts.hh:31-46`). The
Posets benchmark likewise measures construction/insertion and membership,
union, intersection, and a CPre-shaped apply/union/intersection sequence
(`subprojects/posets/tests/downset-bm.cc:148-218,220-317`).

`rank_bucketed_vector_backed` sorts the maximal antichain by coordinate sum. A
membership query skips ranks below the query's sum and returns on its first
dominator; insertion scans possible dominators, removes maxima dominated by the
new value, inserts the survivor at its sorted position, and rebuilds bucket
metadata (`subprojects/posets/include/posets/downsets/rank_bucketed_vector_backed.hh:37-71,81-147`).

#### Where the regimes coincide

- Both ultimately need the same componentwise partial-order predicate on
  fixed-dimension signed rank vectors.
- A coordinate sum is a sound necessary filter in both directions because
  pointwise order implies sum order. This remains true with `-1` values.
- Flat vector-backed Posets implementations also compare one query rank with a
  sequence of stored ranks, so their per-pair SIMD vector operations and some
  filtering techniques are relevant.
- Both benefit from compact rank payloads and from cheap rejection before a
  full partial-order comparison.

#### Where they diverge

- **Closure and representation:** backward stores only incomparable maxima of
  a downset. Forward invalidation must retain and query every visited graph
  state, including mutually comparable states, because each has its own graph
  dependencies and proof record.
- **Answer cardinality:** downset `contains` needs one witness and can return
  early. Forward invalidation must enumerate every live match. The measured
  result is extremely sparse, but no match may be skipped.
- **Identity:** forward results must be stable environment IDs so status,
  reverse edges, proofs, and queues can be updated. Posets downsets own, reorder,
  move, and delete rank values while maintaining an antichain.
- **Update pattern:** forward ranks are append-only and liveness only changes
  from live to losing. Backward structures are repeatedly transformed by
  whole-set `apply`, union, intersection, dominance deletion, and rebuilding.
- **Query orientation:** forward holds one lower corner and reports all stored
  points in its upper orthant. The existing downsets ask whether some stored
  maximum lies in that orthant. The predicate is shared; the required output
  and lifecycle are not.

This rules out a drop-in use of any existing downset. Even the underlying
Posets k-d tree exposes existential `dominates()` after a bulk tree build, not
dynamic append plus report-all IDs
(`subprojects/posets/include/posets/utils/kdtree.hh:148-194,199-237,289-318`).
The bounding-box tree is explicitly static over an antichain and rebuilt for
bulk-changing workloads (`subprojects/posets/include/posets/utils/bboxtree.hh:15-18,125-142`),
again the wrong lifecycle.

### 3. Concrete SIMD assessment

#### The predicate is broadcastable, but the implementation already has horizontal SIMD

Logically, for a generator `g` and live ranks `R[0..N)`, invalidation computes:

```
match[i] = live[i] && for every coordinate q: g[q] <= R[i][q]
```

That can be vectorized in two different directions:

- **Across coordinates within one node** (horizontal): load a SIMD block from
  `g` and `R[i]`, compare lanes, reduce with `all_of`, then advance to the next
  node.
- **Across nodes for one coordinate** (vertical/broadcast): broadcast `g[q]`,
  compare it with a contiguous vector of `R[i][q]` values, and AND the result
  into a live candidate mask.

The normal configuration already does the first. Unless `acacia_no_simd` is
set, `VECTOR_IMPL` defaults to `simd_vector_backed`
(`src/configuration.hh:221-230`); Meson resolves `auto` the same way and enables
that Posets component (`meson.build:371-387`). The default coordinate type is
`signed char` (`src/configuration.hh:132-134`). `simd_vector_backed<T>` is a
`generic` whose storage is `std::vector<fixed_size_simd<T,...>>`
(`subprojects/posets/include/posets/vectors.hh:38-47`). Its partial order uses
SIMD `>=` and `<=` lane comparisons followed by `all_of`, with early exit by
block (`subprojects/posets/include/posets/vectors/generic_partial_order.hh:7-50,52-91`).

So the claim “forward is not using SIMD” would be false for the normal preset.
It is using SIMD for each rank pair; it is not using the broadcast reuse of one
generator across many ranks. There may also be a smaller opportunity for a
one-way `leq` kernel: construction of `generic_partial_order` initially tracks
both `geq` and `leq` even when the caller only asks for `leq`
(`subprojects/posets/include/posets/vectors/generic_partial_order.hh:11-47`).
That is worth measuring but is still only a constant-factor refinement.

#### Today's layout blocks vertical SIMD

`env_nodes` is an array of structs, so walking IDs reads a large record stride
containing status and several vector objects around the rank
(`src/solver/forward_game_nodes.hh:19-31`). More importantly, a SIMD
`State` object embeds a `std::vector` of SIMD blocks: the object is in the
environment record, but its coordinate payload is a separate allocation
(`subprojects/posets/include/posets/vectors/generic.hh:20-59,85-128,441-442`).
The environment records themselves are contiguous; the rank bytes for
successive IDs are not. Each non-losing comparison therefore follows another
rank-buffer pointer before it can perform the already-vectorized horizontal
comparison.

Merely changing to `std::vector<State> ranks` plus a parallel status array would
improve status scanning but would still leave one heap buffer per rank. A dense
row-major rank matrix would remove that pointer chasing and make horizontal
SIMD loads regular. A true across-N broadcast kernel needs coordinate-major SoA
or, more practically for append-heavy stable IDs, a tiled AoSoA layout: fixed
chunks of node IDs, with each coordinate contiguous within a chunk. That layout
lets the scan broadcast a generator coordinate, update a candidate mask for a
whole node tile, and enumerate set lanes afterward.

The dominance detection must be separated from invalidation side effects before
an outer loop can vectorize well. The present loop branches on status and calls
`mark_environment_losing` inside the comparison loop
(`src/solver/forward_reachable_safety.hh:624-633`). A two-phase collect/apply
form removes that obstacle and provides an easy scalar oracle.

After that layout/control-flow change, the pure kernel **would** vectorize well:
each lane performs the same signed-byte comparison against one broadcast value,
and coordinate results combine with mask ANDs without cross-lane dependencies.
The qualification is memory traffic. A naive vertical implementation still
reads up to `N * dimension` rank bytes per generator. It should process node
tiles, test the most selective/non-`-1` coordinates first, and stop a tile as
soon as its candidate mask becomes zero. Otherwise it may trade the current
per-row early block exit for a bandwidth-bound full matrix pass.

#### What can be reused from `posets/vectors`

Reusable directly:

- The concrete `State::partial_order()` is the exact, SIMD-capable final
  verifier for candidates surviving an index. Keeping it avoids duplicating
  mixed-coordinate semantics in the first implementation.
- `simd_traits` supplies the fixed SIMD type, lane count, and alignment; the
  `SIMD_IS_MAX` knob selects maximum fixed width versus native width
  (`subprojects/posets/include/posets/utils/simd_traits.hh:5-19` and
  `meson.build:155-159`). It can parameterize a later packed scan kernel.
- `generic` shows the correct element-aligned load/store form and clears unused
  lanes in the final block (`subprojects/posets/include/posets/vectors/generic.hh:65-83,109-128,171-185`).
- The Boolean-tail wrappers supply correct row-wise encodings and order tests:
  map `-1/0` to `0/1`, compare tail inclusion with OR, and use the Boolean count
  as a necessary filter. See
  `subprojects/posets/include/posets/vectors/X_and_bitset.hh:27-38,85-116`,
  `subprojects/posets/include/posets/vectors/X_and_boolvec.hh:21-31,79-110`,
  and
  `subprojects/posets/include/posets/vectors/X_and_wordvec.hh:31-45,99-134`.
- Of those wrappers, `x_and_wordvec` is the best model for a runtime-sized
  Boolean tail: it stores dynamic `uint64_t` words, compares with word-wise OR,
  and explicitly clears unused high bits
  (`subprojects/posets/include/posets/vectors/X_and_wordvec.hh:250-307`).

Reusable conceptually, but not as a drop-in type:

- `traits.hh` records the runtime `bool_threshold` and the rule for sound
  one-dimensional bins (`subprojects/posets/include/posets/vectors/traits.hh:3-22`).
  It contains no bulk layout or scan API.
- `filtered_vector_backed` is the closest existing algorithmic machinery. It
  chooses a small set of coordinates, builds `bits_ge` threshold bitmaps, ANDs
  them for a query, and invokes the full partial order only on surviving set
  bits (`subprojects/posets/include/posets/downsets/filtered_vector_backed.hh:45-60,91-167,177-210`).
  The forward index needs the same necessary-condition filtering with node IDs
  as bit positions.

What must be new:

- append/inactivate operations keyed by stable environment ID;
- report-all rather than Boolean/existential query output;
- a live mask so old losing records cost a bit clear instead of another future
  `nodes_checked` iteration;
- incremental or chunked threshold filters, rather than rebuilding a filter
  over a mutating antichain after inserts;
- deterministic ascending-ID enumeration and a separate scalar side-effect
  pass;
- for true across-node instruction SIMD, a packed/tiled coordinate store and a
  one-way broadcast comparison kernel.

The three `X_and_*` classes are per-state row representations. They can make a
single mixed numeric/Boolean comparison cheaper, but they do not transpose
coordinates across nodes and therefore do not implement the hypothesized bulk
operation. `x_and_bitset` has a compile-time Boolean capacity and splits at the
separate `bitset_threshold`; `x_and_boolvec` is runtime-sized but uses
`vector<bool>` and a per-bit loop; `x_and_wordvec` is runtime-sized and
word-packed, but still owns a separate word vector per state
(`subprojects/posets/include/posets/vectors/X_and_bitset.hh:20-38`,
`subprojects/posets/include/posets/vectors/X_and_boolvec.hh:14-31,213-217`, and
`subprojects/posets/include/posets/vectors/X_and_wordvec.hh:17-45,303-307`). The
current `Vector` alias does not use any of them in any case; it names
`VECTOR_IMPL<VECTOR_ELT_T>` directly (`src/solver/solve_game_vector.cc:17-18`).

### 4. Recommended implementation order, ranked by value

#### 1. Build a live, stable-ID dominance reporter (recommended: option d)

Add a sidecar with the conceptual interface `append(id, rank)`,
`inactivate(id)`, and `report_ge(generator) -> IDs`. Keep `env_nodes` and the
hash interner authoritative at first.

The first implementation should use:

1. one live bit per environment ID;
2. a small selected set of coordinate filters, each representing
   `rank[id][q] >= threshold` as node-ID bitmaps;
3. word-wise intersection of those filters for the generator's constraining
   coordinates (skip `g[q] == -1`);
4. the existing `State::partial_order()` as an exact check of every surviving
   ID over all coordinates;
5. collection and then ascending-ID invalidation.

This is deliberately a **filter plus exact fallback**, so poor coordinate
selection hurts speed but cannot affect soundness. Boolean-tail coordinates are
cheap candidates for filters because they need only the `>= 0` bitmap; numeric
coordinates may have up to the K-bounded value range. A fixed small filter
budget avoids an all-dimensions/all-thresholds memory explosion. Selection
should be based on observed rejection/selectivity, not just distinct-value
count; the latter is only the simple heuristic used by the existing filtered
downset (`subprojects/posets/include/posets/downsets/filtered_vector_backed.hh:129-161`).

A cached coordinate-sum/live-ID bucket is a lower-complexity preliminary filter
if desired. It is sound for this large visited set for the same reason as the
rank-bucketed downset (`subprojects/posets/include/posets/downsets/rank_bucketed_vector_backed.hh:81-93`),
but it should have an immediate kill criterion because sum selectivity is not
established by the supplied measurements. This is **not** the discarded change
to the 177-element generator vector: it indexes the 81,470-scale visited-node
population exemplified by the supplied campaign
(`benchmarking/p4-forward-expansion-profile.tsv:9`).

Early confirmation/kill measurement:

- Add separate counters for status tombstones avoided, bitmap words read,
  candidates after each filter, exact `partial_order` calls, and IDs reported.
- Time index maintenance, candidate filtering, exact verification, and scalar
  invalidation separately; also record retained index bytes/RSS and total solve
  time.
- Require exactly the same invalidated IDs, in the same order, as a scalar
  oracle and the same proof/verdict outcome.
- On `prioritized_arbiter`, the index should reduce 148.5M raw visits to a much
  smaller number of exact comparisons and reduce invalidation wall time enough
  to move end-to-end time. `lift_gr1` is the smaller control. If most live IDs
  survive the filters, or update/memory cost cancels the saved scan time, stop
  this design early and move to the packed scan below.

The checked-to-invalidated ratios make this the highest-upside test: an index
can remove orders of magnitude of candidate work, while SIMD alone offers at
most a hardware-width/locality constant factor.

#### 2. If filtering is weak, build a tiled vertical SIMD scan (option c as fallback)

Keep graph records hand-rolled but add a packed signed-coordinate shadow in
fixed node tiles. Scan only live bits, broadcast one generator coordinate over
contiguous candidate coordinates, and intersect lane masks. This directly tests
the user's SIMD shape while retaining append-friendly stable IDs.

Compare it against both the current per-State horizontal SIMD and a packed
row-major one-way `leq` kernel. The decisive measurements are invalidation
milliseconds, effective rank bytes read, exact matches, and end-to-end time—not
only instruction counts. If packed vertical SIMD does not beat the current
horizontal SIMD after accounting for shadow-store construction and extra rank
bytes, the SIMD hypothesis is wrong for this solver's actual dimensions and
sparsity and should be dropped.

Replacing the authoritative graph ranks with the matrix should be deferred.
`advance_controller`, hashing, proof export, and strategy construction all want
individual `State` objects (`src/solver/forward_reachable_safety.hh:968-1033,1140-1212`),
so doing that in the first experiment would confound the scan result with a
large solver rewrite.

#### 3. Consider a Posets contribution only after the forward index wins

If the stable-ID reporter pays across the campaign, its generic query core could
be contributed to Posets as a new orthogonal dominance-reporting structure. Its
interface must include stable handles/report-all iteration and inactivation;
`subsumes/insert/size` is insufficient.

A `minimal_upset_antichain<State>` remains a tidy dual of the downset maxima
container and could replace `minimal_losing_antichain`. It is not the measured
optimization target. With a generator peak of 177 and the failed rank-sorting
experiment already supplied, it ranks last for performance value and should
not precede the visited-node index.

#### 4. Do not adopt an existing downset (reject option a)

Negating coordinates or reversing comparisons can dualize closure polarity,
but it cannot fix the report-all, stable-ID, retain-all-points, append/inactivate
requirements. Downset `contains` and antichain insertion solve a different
problem. Adapting one until it supports the forward lifecycle would amount to
writing the new reporter behind a misleading interface.

### 5. Soundness and engineering traps

Ranked by risk:

1. **Signed `-1` sentinel.** The initial rank uses `-1` as bottom and the safe
   vector uses `K-1` for numeric coordinates and `0` for the Boolean tail
   (`src/solver/forward_k_bounded_safety_aut.hh:72-86`). Forward actions also
   initialize unreachable coordinates to `-1`
   (`src/actioners/standard.hh:116-143`). The default element type is explicitly
   `signed char`, so current SIMD comparisons have the intended order. A new
   byte kernel must not reinterpret bytes as unsigned: `-1` would become 255 and
   reverse the crucial bottom relation. Either compare signed lanes or bias all
   values consistently before unsigned comparison.

2. **Boolean-tail encoding.** Coordinates at and beyond `bool_threshold` have
   domain `{-1,0}`, while the prefix has numeric K-bounded semantics. The
   automaton pass arranges the counting prefix before the Boolean tail
   (`src/boolean_states/forward_saturation.hh:58-90`), and `bool_threshold` is
   installed before solving (`src/solver/solver_invoker.cc:570-576`). Uniform
   signed pointwise comparison is sound today. A packed Boolean representation
   must map exactly `-1 -> 0`, `0 -> 1`; that is the assumption made by all
   `X_and_*` constructors. Do not silently clamp or treat every nonzero byte as
   true.

3. **Report-all and side-effect order.** A SIMD mask is only the discovery
   phase. Every set bit still needs the same proof witness, dependency, status
   transition, queue insertion, and reverse-dependency propagation as the
   scalar path (`src/solver/forward_reachable_safety.hh:584-597,1064-1109`).
   Enumerate IDs in increasing order and exact-check false-positive filter
   candidates before applying effects.

4. **Tombstones and the generator itself.** `mark_environment_losing` changes
   status before inserting/scanning the generator (`src/solver/forward_reachable_safety.hh:584-603`),
   so a live mask must clear that ID before the query. Every other loss must
   clear its bit exactly once. Leaving tombstones active is sound only if the
   scalar status check remains, but it gives back much of the intended win.

5. **Padding and tails.** `generic` compares complete allocated SIMD blocks, not
   only `k` lanes, and makes that exact by zeroing unused lanes
   (`subprojects/posets/include/posets/vectors/generic.hh:65-83,187-204`). A new
   packed row kernel must mask its final lane group or initialize identical
   padding. `x_and_wordvec` similarly requires unused tail bits to remain zero
   (`subprojects/posets/include/posets/vectors/X_and_wordvec.hh:250-253,287-300`).

6. **Alignment.** Posets exposes the SIMD alignment requirement
   (`subprojects/posets/include/posets/utils/simd_traits.hh:14-19`) and uses
   `element_aligned` loads for arbitrary spans
   (`subprojects/posets/include/posets/vectors/generic.hh:109-128`). A plain
   `vector<signed char>` matrix must either use unaligned/element-aligned loads
   or provide an aligned allocator and padded row/tile stride. Assuming every
   row is aligned because the allocation base is aligned is incorrect unless
   the stride is also an alignment multiple.

7. **`SIMD_IS_MAX` is an ABI-width choice, not order semantics.** With the knob
   true, Posets uses `max_fixed_size<T>`; false selects the implementation's
   native SIMD size (`subprojects/posets/include/posets/utils/simd_traits.hh:8-16`).
   A new kernel should follow this knob or deliberately document a separate
   dispatch strategy. `acacia_no_simd` changes the entire `State`
   representation (`meson.options:71-74,110-112`), so a whole-solver on/off
   comparison would not isolate the invalidation kernel.

8. **Sparse constraints may defeat vertical SIMD/indexing.** A generator
   coordinate of `-1` rejects nothing. If most generators contain many `-1`s,
   or selected coordinate distributions are unselective, threshold bitmaps will
   leave large candidate sets. This cannot be inferred from
   checked/invalidated alone; record active generator coordinates and candidate
   survival per filter. This is the principal empirical reason to keep an early
   kill gate.

9. **Dynamic storage and references.** Appending can reallocate `env_nodes`.
   Store IDs in the index, not pointers/references to environment records. A
   packed sidecar should define its own stable tile addressing and verify that
   `id` is the bit/lane position used for report results.

10. **Memory can erase the speed win.** The current accounting explicitly
    tracks rank and index bytes (`src/solver/forward_reachable_safety.hh:114-127,416-419,1170-1187`).
    Threshold bitmaps duplicate information; an all-coordinate/all-threshold
    scheme can be much larger than the ranks. Charge the side index to
    `index_bytes`/total limits and retain a small filter budget until measured.

### Bottom line

The invalidation scan is a valid bulk data-parallel target, but “turn on SIMD”
is not the answer: SIMD is already active inside each normal `State` comparison.
The unexploited axis is across visited nodes, and reaching it requires a new
layout/API.

Build the stable-ID live dominance reporter first, borrowing Posets' threshold
bitmap idea and using the existing SIMD partial order as an exact fallback.
Measure candidate survival, exact comparisons, index cost, invalidation time,
and end-to-end time on the two supplied targets. Only if filtering fails should
the next experiment pay the complexity and duplicate-rank memory for a tiled
vertical SIMD matrix. Do not spend this phase on the 177-entry generator
antichain or on wrapping the forward graph in an existing downset.

---

## Sprint code review: portfolio arms and forward safety backend

<sub>Was `benchmarking/SPRINT-CODE-REVIEW.md`. Read-only static review of the same commit, `1d506d43`. Written independently of the review above.</sub>

Review target: `sprint/p5-portfolio-arms` at `1d506d43`.

This was a read-only static review.  Per the active benchmark constraint, I did
not configure, build, compile, run Meson, execute tests, or execute any project
binary.  The only workspace change is this report.

### Executive summary

The runtime-per-arm backend change has a good existing template seam.  Both the
forward and backward implementations already consume the same concrete
`SetOfStates`, IO-precomputer maker and actioner maker in
`solve_with_downset`; selecting between them at runtime needs two solver
instantiations for that one component tuple, not an instantiation per arm.  The
main work is to stop using the same preprocessor symbols for both *availability*
and *selection*, and to make the arm policy an explicit value passed down the
existing call chain.

The first refactor I would do is introduce an owned `solver_arm` /
`game_solver_options` value containing at least `game_backend`,
`local_certificate`, `k_schedule`, and the equivariant policy, construct it in
`acacia-bonsai.cc`, and thread it without changing behaviour through
`run_ltl -> run_one_ltl -> solve_game -> solve_game_vector ->
solve_with_downset`.  This establishes one policy seam before any `#if` is
removed and prevents backend policy from continuing to be inferred from worker
polarity in several layers.

I found no path by which the P1 checker can report UNREALIZABLE for a realizable
formula.  Its deliberate restriction is intact.

The highest-value concrete issues beyond that refactor are:

1. Solver wrapper objects retain references to temporary maker objects.  The
   current makers happen to be empty types with static `make` functions, but the
   references are already dangling and make stateful runtime policy unsafe.
2. Enabling the forward backend does not necessarily run it: the equivariant
   solver is tried first unless a separate flag is disabled.
3. Every forward result deep-copies the proof log and all environment ranks,
   including resource-limit and production results, after the byte cap has been
   evaluated.  This can defeat the solver's graceful memory limit.
4. The lazy controller hot path repeatedly walks `std::list`s and repeats hash
   lookups for the same successor.
5. The portfolio selector silently hides cross-arm verdict conflicts by keeping
   whichever conflicting answer is faster.

### 1. Preparing for runtime, per-arm backends

#### What is selected where today

| Decision | Mechanism today | Consequence for flexible arms |
|---|---|---|
| Worker polarity, unreal transform and translation preference | Runtime values in `acacia-bonsai.cc:67-110`, stored by `run_one_ltl` at `src/solver/solver_invoker.cc:264-308` | Already value-level, but the data model is split across `real_strategies`, `unreal_strategies`, and one shared `primary_translation_pref` (`src/arg_parser.hh:38-40`).  It cannot describe arbitrary per-arm combinations cleanly. |
| Automaton preprocessor and Boolean-state implementation | Type macros invoked before `solve_game` at `src/solver/solver_invoker.cc:510-570` | They can remain build-wide if only the game backend varies per arm. |
| Downset implementation | Concrete `Downset` chosen once in `src/solver/solve_game_vector.cc:17-21` | Helpful: runtime backend dispatch remains inside one downset specialization. |
| IO precomputer, actioner and input picker | Type macros converted to maker template parameters at `src/solver/solve_game_impl.hh:175-201` | Helpful for backend selection.  Forward and backward already share the IO/actioner types.  The input picker is backward-only and should be removed from the forward template. |
| Equivariant solver | Include and whole attempt under `#if ACACIA_ENABLE_EQUIVARIANT_SOLVER` at `src/solver/solve_game_impl.hh:11-13,164-173` | Availability and selection are conflated; it also pre-empts the requested backend. |
| Forward versus backward solver | Include and branch under `#if ACACIA_FORWARD_SAFETY_SOLVER` at `src/solver/solve_game_impl.hh:14-16,178-203` | This is the primary gate to replace with runtime dispatch. |
| Local-certificate probe | Declarations, state, and loop body under `#if ACACIA_LOCAL_CERTIFICATE` at `src/solver/k_bounded_safety_aut.hh:37-121,199-277,325-328,381-384` | Must become a value on the backward backend configuration. |
| K schedule | Enum exists, but `ACACIA_K_SCHEDULE` is passed as a compile-time constant at `src/solver/k_bounded_safety_aut.hh:355-358` and `src/solver/forward_k_bounded_safety_aut.hh:139-142` | Straightforward runtime field; it does not need another solver instantiation. |
| Eager comparison mode | Non-type template parameter, defaulted from a macro at `src/solver/forward_reachable_safety.hh:1229-1244` | Runtime selection would instantiate both branches.  This is comparison tooling and need not be part of the first flexible-arm API. |
| Conditional covering | Structural `#if` in node layout at `src/solver/forward_game_nodes.hh:26-29,39-43` and throughout the search | This is not currently a runtime policy.  Given its mixed result and default-off status, keep it out of the first arm enum rather than doubling node/search variants. |
| Component header availability | `ACACIA_COMPILE_*` include gates in `src/solver/configured_components.hh:5-80` | Not a blocker for backend-only dispatch.  These gates matter only if component *types* also become per-arm choices. |

#### Recommended first refactor and dispatch shape

Create a small header independent of `configuration.hh`, for example:

```cpp
enum class game_backend { backward, forward };

struct game_solver_options {
  game_backend backend;
  bool use_local_certificate;
  bool allow_equivariant;
  acacia::k_schedule::kind schedule;
};

struct solver_arm {
  std::optional<UNREAL_X_T> unreal_transform;
  TRANSLATION_PREF_T translation_preference;
  game_solver_options game;
};
```

Construct `solver_arm` before `start_proc` rather than passing the two unrelated
arguments currently accepted at `src/acacia-bonsai.cc:67-68`.  Pass the immutable
game options through the public `run_ltl` declaration
(`src/solver/solver_invoker.hh:39-44`), store them beside the other runner values,
and add them to the non-template `solve_game` and `solve_game_vector` interfaces
(`src/solver/solve_game.hh:11-15`, `src/solver/solve_game_branches.hh:13-16`).
The only actual dispatch then lives at `src/solver/solve_game_impl.hh:158-203`.

This does not cause a per-arm template explosion.  For the currently selected
component tuple, the translation unit will instantiate one backward class and
one forward class.  Every arm calls one of those same functions by runtime
value.  Do not turn the backend itself into a template argument: it would move
the selection back to compile time and make every policy combination a new
instantiation.

Split compilation availability from runtime selection.  A unified portfolio
build should compile both implementations (or use a separate
`ACACIA_COMPILE_FORWARD_BACKEND` capability gate), while the old
`ACACIA_FORWARD_SAFETY_SOLVER` value can temporarily supply a legacy default.
Likewise, compile the local-certificate implementation once and guard the probe
with `options.use_local_certificate`; `local_certificate.hh` is already included
unconditionally at `src/solver/k_bounded_safety_aut.hh:13`.

Make the dispatch order explicit.  At present the equivariant attempt precedes
both ordinary backends (`src/solver/solve_game_impl.hh:164-178`).  A request for
`game_backend::forward` should not silently run equivariant first.  Either make
equivariant a third backend or make `allow_equivariant` an explicit backward
pre-pass.  Similarly, define what `forward + do_synthesis` means: current code
silently uses backward for synthesis (`src/solver/solve_game_impl.hh:179-196`).
Rejecting an invalid combination at arm construction is clearer than silently
changing it.

Finally, replace the forward wrapper's `optional result +
should_fallback_to_backward()` side channel
(`src/solver/forward_k_bounded_safety_aut.hh:170-189`) with a typed outcome such
as `won`, `lost_at_kmax`, `resource_limit`, or `certificate_rejected`.  Runtime
dispatch should be able to decide fallback from the returned value without
having to know which internal boolean the backend set.

#### [High] Maker references already dangle and block stateful runtime policy

Both K wrappers accept makers by `const&` and store those references
(`src/solver/k_bounded_safety_aut.hh:131-145,322-324` and
`src/solver/forward_k_bounded_safety_aut.hh:36-51,225-230`).  Their callers pass
temporary makers, then call `solve()` in a later statement
(`src/solver/solve_game_impl.hh:184-202`; the Python path repeats this at
`src/python/python_interface.cc:138-142`).  The temporaries die at the end of the
construction full-expression, so the stored references dangle.

The current maker APIs use static `make` functions, which is why this has not
shown up as an observed read of maker state.  It becomes an immediate lifetime
bug as soon as a maker carries per-arm options.  Accept and own makers by value;
`[[no_unique_address]]` keeps empty makers free.  Alternatively, remove maker
objects and invoke the maker types statically, but value ownership is the more
future-proof interpretation of the existing maker abstraction.

The forward wrapper's `InputPickerMaker` is entirely unused
(`src/solver/forward_k_bounded_safety_aut.hh:29-42`).  Remove that template
parameter and constructor argument instead of carrying backward-only policy
through the forward backend.

#### [High] “Forward backend” is not an exclusive backend selection

`solve_with_downset` tries the equivariant solver before it reaches the forward
branch (`src/solver/solve_game_impl.hh:164-178`).  Meson's forward option does
not disable the equivariant option: their defaults and definitions are
independent (`meson.options:80-83`, `meson.build:185-193`).  Therefore a user who
sets only `-Dacacia_forward_safety_solver=true` can run equivariant on eligible
instances despite the option description saying forward is used “instead of”
backward.  The checked-in forward preset avoids this by separately setting
`enable_equivariant_solver=false` (`config/acacia-presets.json:123-127`), but the
backend option itself remains composition-dependent.

This is a current configuration correctness issue and must be resolved before
runtime arm selection; an enum value should name the backend that will actually
run.

#### [Medium] Backend defaults disagree between the two configuration frontends

`meson.options:78` defaults `acacia_local_certificate` to `true`, while
`config/acacia-options.json:127` defaults `local_certificate` to `false` and the
fallback in `src/configuration.hh:17-19` is also false.  The comment in
`src/solver/k_bounded_safety_aut.hh:82-88` says the Meson option is default-off,
which is no longer true.  Builds made directly with Meson and builds made via
`scripts/acacia-config.py` therefore do not have the same backward backend.

Choose one default and generate all frontends from it before using that default
to initialize per-arm runtime options.  Otherwise the “real worker uses local
certificates” rule will depend on how the binary was configured.

### 2. Forward solver: refactor and optimisation findings

#### [High] Result construction defeats the byte cap and deep-copies test-only data

`make_result` is called for win, loss, and resource-limit exits
(`src/solver/forward_reachable_safety.hh:274-297`).  It then:

- deep-copies the proof vector, including each dependency vector
  (`src/solver/forward_reachable_safety.hh:1188-1189`);
- copies every environment rank (`src/solver/forward_reachable_safety.hh:1190-1192`);
- builds a controller-parent vector (`src/solver/forward_reachable_safety.hh:1193-1195`);
- copies all losing-antichain ranks (`src/solver/forward_reachable_safety.hh:1196-1200`);
- and, for wins, also copies the reachable strategy ranks
  (`src/solver/forward_reachable_safety.hh:1210-1211`).

Those copies occur after `result.total_bytes` has been set from the search's
logical accounting (`src/solver/forward_reachable_safety.hh:1170-1187`).  A
search that reaches its configured memory limit can therefore allocate a second
copy of its largest structures while attempting to return `resource_limit`.
The reported byte count also understates peak live memory during result
construction.

Production only consumes the scalar metrics and `strategy_ranks`; proof replay
data is consumed by `tests/forward_safety_game_test.cc:177-192`.  Add an explicit
`capture_replay_proof` test option or split the result into a lightweight
production outcome and an optional replay artifact.  On losing results, moving
owned data is preferable to copying it.  On winning and resource-limit results,
do not materialize replay data at all.

This cleanup is made easier by removing `loss_evidence::have_certificate`: the
field is initialized from `result.losing_proofs` at
`src/solver/forward_k_bounded_safety_aut.hh:131-138`, but is never read by
`is_cheap` or `next` (`src/solver/k_schedule.hh:11-27,44-77`).

#### [High] The lazy hot path repeats linear list walks and interner work

Every call to `advance_lazy_controller` starts at the beginning of the input
range and advances by `input_index` (`src/solver/forward_reachable_safety.hh:877-884`).
It then starts at the beginning of that input's action range and advances by
`next_action_index` (`src/solver/forward_reachable_safety.hh:964-971`).  The
configured actioner stores both levels in `std::list`
(`src/actioners/standard.hh:26-39`), so repeated loss/reselection makes the
cursor work linear, and potentially quadratic over a controller's action list.

Build a stable random-access view once per fixed-K search, for example a vector
of pointers to each input's actions and a vector of action pointers per input.
Then `input_index` and `next_action_index` remain valid O(1) cursors without
changing semantic action order or the backward picker's splice behaviour.

The same successor is also looked up twice.  The lazy loop calls `find_env`
(`src/solver/forward_reachable_safety.hh:979`), then `intern_env` recomputes the
coordinate hash and repeats the bucket scan
(`src/solver/forward_reachable_safety.hh:658-664,1032`).  Conditional covering
adds another exact lookup in `find_cover`
(`src/solver/forward_reachable_safety.hh:535-542`).  Compute the hash once and
use one `find_or_intern` operation that accepts the already-known lookup result.

For controllers that reject many duplicate successors,
`std::ranges::contains(ctrl.tried_env_ids, ...)` is another linear scan in the
per-action loop (`src/solver/forward_reachable_safety.hh:983-996`).  Keep the
vector for deterministic proof order, but add a small-set/hashed-set threshold
if profiles show large tried sets.

#### [High, known algorithmic target] Invalidation still scans all visited nodes

Every successful losing-antichain insertion scans every interned environment
node and performs a partial-order comparison on every still-live candidate
(`src/solver/forward_reachable_safety.hh:602-634`).  This is the structure the
O3 numbers identify; indexing the much smaller generator list would not address
it.  The next optimisation should index the visited, non-losing environment
nodes for upward-orthant queries, with the exact Posets comparison retained as
the final check.  Even a coordinate-sum bucket prefilter can be shared with the
covering experiment, although a proper visited-node dominance index is the real
fix.

The O3 counters still earn their place while this issue is open: `nodes_checked`
and `nodes_invalidated` quantify the waste, and one scan count supplies context.
However, `losing_insertions` and `invalidation_scans` are necessarily identical:
the only successful `minimal_losing_antichain::insert` increments `insertions`
(`src/solver/minimal_losing_antichain.hh:56-82`), and the caller unconditionally
increments `invalidation_scans` immediately afterward
(`src/solver/forward_reachable_safety.hh:602-618`).  Remove one of those two
counters and shorten the fragile positional diagnostics API at
`src/solver/diagnostics.hh:673-721` by passing a named metrics struct.

O5 left no code scaffolding behind; its abandoned generator index is absent.

#### [Medium] Conditional covering treats exact interning hits as covers

`find_cover` deliberately returns an exact live state first
(`src/solver/forward_reachable_safety.hh:535-542`).  The caller then stores a
second full copy of that exact successor, registers a `covered_by` dependency,
and enters cover-resolution machinery
(`src/solver/forward_reachable_safety.hh:1013-1028`).  An exact hit needs none of
that: it can be selected directly through the normal `selected_by` path.

Handle `known_env` first and restrict `find_cover` to strict dominators.  This
removes one rank allocation and one parallel dependency path for a common case,
and also eliminates the repeated exact lookup noted above.  This affects only
the default-off O4 mode.

#### [Medium] P3's default linear mode still pays for adaptive evidence

The backward solver updates peak/loop evidence on every fixed-point iteration
and reads the clock on bound changes (`src/solver/k_bounded_safety_aut.hh:203-210,329-358`).
The forward wrapper similarly timestamps every attempt and constructs evidence
(`src/solver/forward_k_bounded_safety_aut.hh:100,131-142`).  Linear, geometric,
and direct-max schedules do not inspect that evidence; only
`cheap_loss_adaptive` does (`src/solver/k_schedule.hh:50-51`).

Once schedule becomes a runtime field, collect timing/frontier evidence only
for the adaptive schedule, or make `next` accept a lazy evidence callback.  The
kept P3 tooling should not impose measurement overhead on the default linear
path.  Also remove the unused `have_certificate` field described above.

#### [Low] End-of-search and comparison-only state can be trimmed

- `losing_antichain_ranks` is written at
  `src/solver/forward_reachable_safety.hh:1196-1200` and has no reader anywhere
  in the tree.  It is dead and potentially large.
- `forward_ctrl_node::selected_action_index`
  (`src/solver/forward_game_nodes.hh:44-45`) is not used to build the returned
  certificate or strategy.  Outside O4 it is only assigned/reset; inside O4 it
  is round-tripped without affecting the cursor or proof.  Remove it unless
  forward strategy synthesis is about to consume it.  The eager reducer's
  `representative_action_index` is currently meaningful only to unit tests.
- `build_strategy` allocates `seen_ctrl` at
  `src/solver/forward_reachable_safety.hh:1113-1130`, but each controller node
  belongs to exactly one environment via `parent_env` and each environment is
  visited once.  `seen_ctrl` is redundant.
- `forward_minimal_successors` is not a meaningful reduction counter on the
  shipped lazy path: that path increments `distinct_successors` and
  `minimal_successors` together (`src/solver/forward_reachable_safety.hh:1008-1009`).
  Keep it explicitly eager-only or remove it from normal diagnostics.

### 3. Dead or inert sprint code

P2 is not unreachable: the preset at `config/acacia-presets.json:115-118` and
the direct tests still exercise it.  It is correctly default-off.  Its only
default-build cost is source/compile coupling: `actioners/standard.hh:3-5`
includes the 268-line dominance implementation even when the flag is false.
Guard that include with `ACACIA_PROFILE_DOMINANCE` and keep the direct test
including the helper itself.  The unbudgeted and budgeted dominance comparisons
also duplicate the same merge walk at `src/actioners/profile_dominance.hh:69-87`
and `src/actioners/profile_dominance.hh:132-163`; one comparator with an optional
budget observer would reduce proof-maintenance risk.

P3 is also reachable through presets/options and is intentionally retained as
tooling, but `loss_evidence::have_certificate` is dead and its default-path
evidence collection should be removed as described above.  The schedule code
itself is small and does not need deletion.

The eager forward comparison path is reachable from a build option and heavily
used by the differential test, so it is not dead.  Its metrics should be named
as comparison metrics rather than emitted as if the lazy production path were
performing Pareto minimisation.

Two older leftovers are worth removing while this area is open:

- `MAX_CRITICAL_INPUTS` is defined at
  `src/solver/k_bounded_safety_aut.hh:3-4` and referenced nowhere else.
- `k_bounded_safety_aut_detail::gen` is initialized and stored at
  `src/solver/k_bounded_safety_aut.hh:142,321` but never read.

### 4. P1 forced-output checker: soundness review

I found no false-UNREAL path.

- **Top-level implication:** `try_direct` explicitly declines it at
  `src/solver/forced_output_contradiction.hh:229-233`.  Independently, candidate
  collection does not descend through `Implies` or `Or`, so a missed syntactic
  spelling of implication would be a false negative, not an unsound match.
- **Direct-consequence traversal:** `collect_candidates` descends only through
  conjunctions, accepts the body of a direct `G`, and turns an immediate
  `G(A & B)` into separate `G A` / `G B` work items
  (`src/solver/forced_output_contradiction.hh:155-175`).  It never descends under
  `Or`, implication, temporal binary operators, or negation.  Translating an
  already-selected output-only Boolean formula may of course evaluate its
  internal Boolean `Or`; that does not weaken the consequence traversal.
- **Empty invariant set:** `chi_all` starts at `bddtrue`
  (`src/solver/forced_output_contradiction.hh:250-258`).  With no invariant, a
  response can match only when `beta` itself is false.  `G(alpha -> X^d false)`
  with a forceable bounded input trigger, and unconditional `G(F false)`, are
  genuinely unrealizable.  `And(empty)` is used only to format the proof
  (`src/solver/forced_output_contradiction.hh:260`).
- **Vacuous trigger:** the no-implication branch sets the trigger to `true` but
  still accepts only `F beta` or a finite `X` chain ending in output-only
  Boolean `beta` (`src/solver/forced_output_contradiction.hh:193-218`).  Because
  the candidate came from a direct `G`, the obligation is unconditional; the
  reasoning in the comment is valid.
- **Triggered response:** an implication trigger is accepted only when
  `bounded_input_pattern` can translate it using input APs, Boolean
  conjunction/disjunction, literal negation, and finite `X` offsets
  (`src/solver/bounded_input_pattern.hh:22-63`).  A satisfiable such finite input
  pattern is environment-forceable; unsupported syntax declines.
- **Contradictory invariants:** every member of `invariants` is an output-only
  body of a direct global consequence.  Therefore the early return when their
  propositional conjunction is false
  (`src/solver/forced_output_contradiction.hh:260-269`) proves the whole formula
  unsatisfiable, which is stronger than unrealizability.

The checker can be cleaned up without changing that surface.  It currently
creates one BDD dictionary in `try_direct` before even checking the top-level
guard (`src/solver/forced_output_contradiction.hh:226-233`), creates another
dictionary for every trigger satisfiability call
(`src/solver/bounded_input_pattern.hh:86-95`), and allocates BDD variables for
every declared output up front (`src/solver/forced_output_contradiction.hh:71-77`).
Use one checker-owned BDD context, one reusable `(input AP, offset)` translator,
and lazy output-variable allocation.  The input and output translators also
duplicate the same Boolean AST-to-BDD walk; sharing a conservative Boolean
encoder with pluggable variable lookup would reduce the chance that their
accepted operator sets drift.

### 5. Tooling findings

#### [High] Portfolio selection masks verdict conflicts

When two arms decide the same instance, `evaluate` retains only the faster
answer without comparing the verdicts
(`benchmarking/select-portfolio-arms.py:53-60`).  A REAL/UNREAL conflict can
therefore increase the apparent union and enter the selected portfolio without
any warning.  Current campaign notes say there were zero conflicts, but the
selector should enforce that invariant itself: collect all verdicts per
instance, fail loudly if the set has size greater than one, then apply the
minimum-time rule.

#### [Medium] Resume markers are not tied to campaign inputs

`run-portfolio-arms.py` skips an arm solely because `<arm>.done` exists
(`benchmarking/run-portfolio-arms.py:77-81`) and writes that marker after either
a clean run or a conflict-collecting exit (`benchmarking/run-portfolio-arms.py:105-113`).
The marker does not encode the binary, corpus/list, flags, cap, memory settings,
or `--limit`, and the code does not require the output/summary files to still
exist.  Reusing an output directory with changed parameters can silently mix or
skip campaigns.  Store a small manifest/fingerprint beside each marker and
validate it on resume.

#### [Low] Backend-race output calls ties “slower”

`compare-backend-race.py:86-89` counts only strict `<` as faster and reports all
remaining cases as slower, so equal timings are mislabeled.  Report faster,
equal, and slower separately.  Also reject an empty `decisive_seconds` field
instead of coercing it to `0.0` at `benchmarking/compare-backend-race.py:32-39`,
which would make malformed data look maximally fast.

### 6. Other correctness conclusions

I did not identify another game-verdict bug in O1, O2, or O4 by static
inspection.  The lazy cursor advances monotonically, losing-antichain decisions
are confirmed by exact partial-order checks, hash collisions are resolved by
exact rank comparison (`src/solver/forward_reachable_safety.hh:511-523`), and a
lost O4 dominator correctly causes the saved smaller successor to be reconsidered
(`src/solver/forward_reachable_safety.hh:886-933`).

The findings above that are genuine current correctness/reliability defects are
the non-exclusive forward/equivariant option composition, inconsistent local
certificate defaults, hidden selector conflicts, and unfingerprinted resume
markers.  The dangling maker references are a latent lifetime defect with the
current static empty makers and become observable as soon as runtime state is
put into a maker.  The forward result-copy issue is a resource-limit defect: it
can allocate beyond the limit while constructing the result that is supposed to
report that limit.

No dynamic verification was performed because doing so would violate the active
benchmark campaign constraint.

---

## Zero-tail versus bare-vector study

<sub>Was `benchmarking/STATE-VECTOR-TAIL-STUDY.md`. Closed. The bare `Vector` is the default; the reported bare-vector regression did not reproduce.</sub>

### Decision

Use the bare `Vector` as the default. The controlled study does not reproduce
the previously reported catastrophic bare-vector regression: exact twins
execute the same solver work and the same dominant machine-code kernel. The
later memory-limit follow-up also gives the two types identical outcomes and
8 GiB peaks. The zero-length wrapper therefore does not justify a permanent
production type or source-level branch. The ordinary build also stops enabling
Posets' `x_and_bitset` component; it remains available only through the
explicit compile-all-components developer configuration.
The study is closed. Its harness, `benchmarking/state_vector_tail_study.py`,
was removed once the decision landed; recover it from the history of this file
if the tail representation is ever revisited.

### Controlled twins and protocol

Both variants were built from Acacia `e5b1d3b2571955b8a1397052ca0e5643c105e029`
with Posets `7562564163741d7378d4bbacd7d6d5e7b856d20d`. The only solver-source
difference was the state alias in the solver instantiation. GCC 16.1.1,
Meson 1.11.2, and Ninja 1.13 were used on an Intel i7-11850H. Runs alternated
variant order, used one user-systemd scope per solver, 8 GiB RAM, no swap, a
20-second deadline, five repetitions, and `perf stat` hardware counters.

| build | zero-tail SHA-256 / bytes | bare SHA-256 / bytes |
|---|---|---|
| release + LTO | `99d883eb8de4...` / 454,432 | `a26788b73bda...` / 453,816 |
| release, no LTO | `e3c884a671a9...` / 495,376 | `0349d60485bb...` / 496,120 |

The clean release/LTO builds took 59.87 s and 60.24 s respectively and peaked
at 586,804 KiB and 586,752 KiB. Thus the wrapper is not a meaningful compile
cost after static sizing was removed.

No separate always-512-bit build was run. The old shipping matrix already
contained a 512-bit specialization as the largest of nine tail widths, but its
coverage/timing cannot isolate that one type. Hardwiring it would also be a
different representation from both exact twins: small states would carry an
unused fixed tail and large states would still split at 512 bits. The follow-up
protocol explicitly excluded that speculative bucket, and the later decision
to keep only the bare vector removes the reason to add it now.

### Five-by-20-second results

All 30 LTO pairs timed out under both variants. The table reports the median
within-pair bare/zero ratio; for capped pairs, counters measure work retired in
the same wall-time budget, not time to completion.

| target | LTO cycles | LTO instructions | no-LTO cycles | no-LTO instructions |
|---|---:|---:|---:|---:|
| SYNTCOMP21 round-robin 4 | 1.0037 | 1.0020 | 0.9900 | 1.0065 |
| SYNTCOMP24 buffer 5 | 1.0004 | 0.9939 | 0.9780 | 0.9760 |
| SYNTCOMP24 hints 6 | 1.0031 | 0.9551 | 1.0362 | 0.9984 |
| SYNTCOMP24 round-robin 4 | 1.0021 | 0.9943 | 1.0063 | 1.0149 |
| SYNTCOMP25 buffer PB 5 | 0.9959 | 0.9949 | 1.0051 | 1.0026 |
| SYNTCOMP25 robot-resource | 1.0006 | 0.9902 | 1.0257 | 0.9994 |

In the no-LTO campaign, five targets again timed out 5/5 under both variants.
`robot-resource-1d-unreal` solved 5/5 under both: the median was 16.888 s for
zero-tail and 17.171 s for bare, a 1.69% bare regression. Retired instructions
were 0.06% lower for bare while cycles were 2.57% higher, so frequency and
thermal variation explain more of that wall-time delta than extra work.

A diagnostics-enabled paired run reached identical solver checkpoints and
exact work counts. On `hints6`, both performed 30 loops, three K attempts,
1,856 actions, and 18,287,364 meets; bare completed in 18.604 s and zero-tail
in 18.955 s. The round-robin and robot paths likewise matched their loop,
action, and meet counts. Diagnostics change code generation and are therefore
directional evidence only.

### Memory-limit follow-up

The final full-panel rerun placed every invocation in its own 8 GiB, zero-swap
cgroup. Bare vector reached that limit on `robot_grid6_6` and `robot_grid7_7`,
where the older aggregate zero-tail campaign had recorded one timeout and one
unknown. To distinguish a representation regression from a harness-label
difference, the release/LTO exact twins above were rerun in alternating order
for five 20-second repetitions on each target. The harness recorded cgroup
`MemoryPeak`; hardware counters were disabled for this memory-focused pass.

| target | zero-tail outcomes / median limit time | bare outcomes / median limit time | median peak, both |
|---|---:|---:|---:|
| `robot_grid6_6` | 5/5 resource / 8.63 s | 5/5 resource / 8.35 s | 8 GiB |
| `robot_grid7_7` | 5/5 resource / 14.93 s | 5/5 resource / 15.08 s | 8 GiB |

All 20 samples hit exactly 8,589,934,592 bytes. The small time-to-limit
differences reverse direction across the two targets; they do not show a
zero-tail advantage. The full-panel classification difference was caused by
the newer per-invocation resource isolation and strict OOM detection, not by
collapsing the wrapper to its underlying vector. Raw samples and hashes live
under `_bm-logs.final-v1-current-1d48a15f-20260825/tail-memory-5x20`.

### Profile and disassembly

A 20-second, 499 Hz DWARF call-graph profile of the LTO `hints6` run gave the
same shape:

| self cycles | zero-tail | bare |
|---|---:|---:|
| `vector_backed::insert` | 45.15% | 44.39% |
| `generic_partial_order` | 44.71% | 45.25% |
| critical input picker | 3.31% | 3.41% |
| LTL worker | 2.72% | 2.68% |
| BDD node creation | 1.13% | 1.09% |

The 192-byte `generic_partial_order` routine—the dominant inner comparison—has
byte-for-byte identical machine code in both binaries (normalized opcode hash
`b856ab3b90179acc116c53762e3212eba8cb75b892d807188c4395e5e08a6524`).
It performs the same AVX-512 `vpcmpleb` loop.

The enclosing `vector_backed::insert` is 1,303 bytes for zero-tail and 1,239
bytes for bare (328 versus 316 disassembly lines, including continuation and
alignment lines). The substantive difference is a small move/alias guard in
the zero-tail wrapper path; the comparison loops and call to
`generic_partial_order` are the same. Sampling places the time in those common
comparison loops, not in the wrapper-only move sequence.

This demystifies the earlier apparent 119% regression: exact twins execute the
same hot kernel and the same solver work. The earlier binaries/campaign did
not isolate the representation alias well enough for that claim. The bare type
is now the production default; the one no-LTO 1.69% timing delta is retained as
directional thermal/code-layout noise rather than treated as a wrapper benefit.
A future optimization effort should target the common `generic_partial_order`
/ `vector_backed::insert` comparison path, which accounts for about 90% of
cycles.

---

## TLSF normalization to Acacia automata

<sub>Was `benchmarking/TLSF-NORMALIZATION-STUDY.md`. Closed: rejected. Zero losses, but zero alternate-only wins and no graph-size reduction.</sub>

### Outcome

The equivalence-preserving TLSF normalization ladder does not produce smaller
or more solvable automata for the three decisive `lift`/`robot_grid` targets.
It is rejected as an Acacia-tailoring mechanism: it has zero losses, but also
zero alternate-only wins and no graph-size reduction.

The durable experiment infrastructure lives in `tlsf-tools`: source-only guard
features, normalized TLSF and LTL artifacts, exact Acacia-oriented HOA bundles,
per-orientation translation failures/timeouts, replay orchestration, atomic
resume files, and zero-loss/opposite-conflict summaries. Acacia supplies
uninstalled Spot-dependent automaton-generation and HOA-replay helpers behind
`-Dbuild_research_tools=true`; tlsf-tools links to neither and exposes no Spot
types. The production CLI and TLSF frontend are unchanged.

### Ladder and boundary

The schedules were `off`; `split`; `split,nnf,weak,bool-canon`; `pre-safe` plus
`split,match-safe`; `split,route-safe`; and bounded Sickert normalization at
one and two iterations. Each used Acacia's optional realizability simplifier,
worker orientation and X shift, input-before-output BDD registration,
Büchi/state-based acceptance request, `Small` preference, and translator
options (`simul=0`, `ba-simul=0`, `det-simul=0`, `tls-impl=1`,
`wdba-minimize=2`).

This is the formula-to-automaton boundary. Formula-level direct-strategy
shortcuts, safety-core witnesses, and Acacia's formula decomposition occur
before or around that boundary, so HOA replay is intentionally not described
as an end-to-end CLI replacement.

### Decisive screen

The official SYNTCOMP 2024 TLSF archive supplied `lift_gr13`,
`lift_unary_enc3`, and `robot_grid2_2`. For every schedule, the three worker
orientations had these exact sizes:

| target | real states/edges | unreal-formula | unreal-automaton |
|---|---:|---:|---:|
| `lift_gr13` | 225 / 2,758 | 89 / 1,457 | 33 / 206 |
| `lift_unary_enc3` | 221 / 2,855 | 615 / 13,117 | 172 / 2,262 |
| `robot_grid2_2` | 78 / 770 | 1,001 / 31,539 | 187 / 2,116 |

Every schedule therefore totaled 2,621 states and 57,080 edges. Aggregate
formula nodes were 3,599 for `off`, `split`, `pre-match`, and both Sickert
schedules; `split-safe` increased this to 4,056 and `route` to 4,209.
Different HOA hashes reflect syntactic/state-order choices, but not a smaller
automaton.

At a 5-second, 8 GiB, zero-swap replay cap, each schedule won exactly one of
nine workers: `robot_grid2_2` real in 0.10--0.12 s. The other eight timed out.
There were zero baseline losses, gains, errors, or simultaneous real/unreal
wins. A 20-second confirmation of all 21 real workers was identical:
`robot_grid2_2` solved under all seven schedules in 0.111--0.121 s, while both
`lift` targets timed out under all seven.

This agrees with the earlier B1 result: global formula-level normalization
lost coverage and gained no answer. B3's few gains came from constructing a
different specialized MP-NBA, not from presenting Spot with equivalent syntax;
that construction also lost 15 baseline answers and remains rejected as a
global frontend. In particular, `lift_unary_enc3` is still undecided.

### Next experiment boundary

Do not add more unconditional normalization passes. Any follow-up should use
the new bundle/replay infrastructure to evaluate an explicitly alternate
automaton construction or translator portfolio. A selector may use only the
stored `guard_*` fields and declared input/output counts, with at most three
predicates, family-grouped holdouts, zero baseline losses/opposite conflicts,
non-worse PAR-2, and wins in at least two families. Automaton metrics, solver
results, source paths, and filenames are forbidden selector inputs.
