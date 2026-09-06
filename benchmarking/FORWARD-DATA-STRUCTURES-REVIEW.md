# Forward data structures: access pattern and SIMD review

Date: 2026-09-04  
Branch reviewed: `sprint/p5-portfolio-arms` at `1d506d43`  
Method: read-only static analysis. No build, test, Meson command, benchmark, or
project binary was run.

## Executive call

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

## 1. What the forward solver actually does

### Graph and work scheduling

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

### Interning

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

### The two different dominance workloads

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

## 2. Contrast with the backward/Posets regime

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

### Where the regimes coincide

- Both ultimately need the same componentwise partial-order predicate on
  fixed-dimension signed rank vectors.
- A coordinate sum is a sound necessary filter in both directions because
  pointwise order implies sum order. This remains true with `-1` values.
- Flat vector-backed Posets implementations also compare one query rank with a
  sequence of stored ranks, so their per-pair SIMD vector operations and some
  filtering techniques are relevant.
- Both benefit from compact rank payloads and from cheap rejection before a
  full partial-order comparison.

### Where they diverge

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

## 3. Concrete SIMD assessment

### The predicate is broadcastable, but the implementation already has horizontal SIMD

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

### Today's layout blocks vertical SIMD

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

### What can be reused from `posets/vectors`

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

## 4. Recommended implementation order, ranked by value

### 1. Build a live, stable-ID dominance reporter (recommended: option d)

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

### 2. If filtering is weak, build a tiled vertical SIMD scan (option c as fallback)

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

### 3. Consider a Posets contribution only after the forward index wins

If the stable-ID reporter pays across the campaign, its generic query core could
be contributed to Posets as a new orthogonal dominance-reporting structure. Its
interface must include stable handles/report-all iteration and inactivation;
`subsumes/insert/size` is insufficient.

A `minimal_upset_antichain<State>` remains a tidy dual of the downset maxima
container and could replace `minimal_losing_antichain`. It is not the measured
optimization target. With a generator peak of 177 and the failed rank-sorting
experiment already supplied, it ranks last for performance value and should
not precede the visited-node index.

### 4. Do not adopt an existing downset (reject option a)

Negating coordinates or reversing comparisons can dualize closure polarity,
but it cannot fix the report-all, stable-ID, retain-all-points, append/inactivate
requirements. Downset `contains` and antichain insertion solve a different
problem. Adapting one until it supports the forward lifecycle would amount to
writing the new reporter behind a misleading interface.

## 5. Soundness and engineering traps

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

## Bottom line

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
