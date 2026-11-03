# Posets backend and SIMD study

The semantic screen reuses Posets' `downset-bm.cc` and an independent logical
integer oracle. It retains all14 implementations, including rejected/crashed
cells. Constructor preconditions, an empty result of an operation, a
zero-dimensional singleton and an all-minus-one bottom vector are distinct.
Actual solver integration must satisfy the call path's contract; a passing
nonempty trace does not establish unrestricted empty/zero support.

`acacia_simd_native=true` opts into hardware-native lane count. Otherwise the
legacy `acacia_simd_is_max` chooses max-fixed or compatible width. The benchmark
reports actual widths and emitted comparison instructions and deduplicates only
identical types/alignment/instructions. Scalar vectors are correctness controls;
no claim is made about compiler auto-vectorization of their ordinary loops.

## Actual operation capture

Use a diagnostics-enabled binary in a fresh ignored directory per invocation.
Capture is separate from clean performance measurements. The existing producer
writes `meta.tsv`, `automaton.hoa`, and complete schema2 `cpre-*.tsv` events
containing the actual region, ordered actions and result. Environment:

```
ACACIA_DIAG=1
ACACIA_DIAG_INSTANCE=the_exact_corpus_instance_id
ACACIA_DIAG_PROGRESS_EVERY=0
ACACIA_ANTICHAIN_SNAPSHOT_DIR=/absolute/fresh/case-directory
ACACIA_ANTICHAIN_SNAPSHOT_CPRE=1
ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX=8
ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS=4096
ACACIA_ANTICHAIN_SNAPSHOT_CPRE_FRONTIERS=1
ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS=1
ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS_MAX=16
```

`CPRE_FRONTIERS=1` selects the first update in each existing frontier-size bin;
without it, the original first-N update capture remains unchanged. Action-cap
rejections produce `.skipped` files. Consumed sampling bins do not establish that
no further update existed.

Contains events record actual picker or initial-state queries, their exact
answer and generator region, source-query ordinal and observed hit-prefix count.
They sample the first hit/miss in each frontier-size bin. These are stratified
point samples, not representative frequencies; do not assign guessed trace
weights. Files become final only after a successful close; `.partial` files and
missing samples remain unknown, never replay inputs.

Only a classic search reaching `record_final` writes `operation-counts.tsv`
with complete per-configured-automaton query/hit/miss/CPre counts. Exhausted,
cancelled and otherwise censored searches have no complete frequency table.
Counts do not describe the whole portfolio or an unobserved continuation.
A reconstructed initial-rank membership query used by CPre replay is a derived
control and must remain distinct from these actually recorded query events.

For a classic-backward trace collection, explicitly record the diagnostic flags
`--arms real:small:backward --spot-fast off --r-prepass off --equivariance off`.
Those flags isolate this kernel; they do not measure shipping-race frequencies.
Record the original TLSF bytes, binary/build/source/library hashes, command,
environment and termination subtype. Leave skipped/partial/censored evidence in
the invocation manifest. No performance/default selection follows from capture.

`posets-capture.py` provides the serial capture wrapper around the existing
scoped runner. It requires the immutable diagnostic executable and its source /
build provenance JSON; it fixes17s, 8GiB, zero swap and the isolated flags above.
Pass the actual corpus ID and absolute TLSF source for each input:

```sh
python3 benchmarking/posets-capture.py --binary /absolute/frozen/acacia-bonsai \
  --provenance /absolute/frozen/provenance.json \
  --input 06.ltl=/absolute/corpus/06.tlsf \
  --input robot_grid_pb_4_4_pe_.ltl=/absolute/corpus/robot_grid_pb_4_4_pe_.tlsf \
  --output _bm-logs.posets/capture
```

Do not run it concurrently with a build or another measurement. Its schema1
`posets-operation-capture` invocation manifest binds the exact instance,
`input`, `binary` and `frozen_provenance` path/SHA pairs, helper/support code
hashes, resolved runtime library paths/bytes, exact command and instrumentation
environment, raw scoped termination/resource subtype, stdout/stderr hashes,
and the entire relative `snapshot_files_sha256` inventory. The matrix validates
those bindings, not merely the manifest's own digest. The helper refuses
inherited snapshot settings and creates a fresh per-invocation directory. Its
`captures.json` retains cases with no usable operations; missing samples remain
unknown. All TLSF hashes are pinned before invocation1 and rechecked after every
invocation, along with binary/build/code/runtime identities. The supplied root
frozen schema must have this binary's `binary_sha256`, a full `source_revision`,
and `meson_options` containing the actual `acacia_enable_diagnostics=true`
option. The helper validates those fields; it does not infer a build flag from
an executable's filename.

## Replay screen

The existing `downset-bm` accepts `--trace /absolute/aut-N/cpre-N.tsv` or a
finalized `contains-N.tsv`. `--check=trace` validates source arithmetic with the
independent integer oracle and checks that backend's exact result, without
operation timers. `--check=trace-source` validates the source alone;
`--check=replay` checks the backend against an already validated source. The
matrix driver runs that source oracle once per trace, retains source failures
for all cells, and hashes all source bytes again after the run.

Both checks and trace timing select exact backend names. The old synthetic
benchmark keeps its prefix selectors. Each matrix cell also checks the emitted
backend/vector identity, so `vector_backed` cannot silently include its two
similarly named siblings. The original semantic report predates this exact
selector; its stdout preserves the extra dispatch. Its separately named
sibling cells remain evidence; a crash cannot establish what later prefix
siblings would have done. No performance claims used that report.

Declare a JSON plan before measurements; paths are relative to the plan (or
absolute). Keep every family wholly in `train` or `heldout`, including all
automata and operation samples from an instance. The capture manifest is the
invocation's provenance, not a substitute for the exact operation file:

These split labels apply to this new Posets threshold choice. Prior pipeline
folds and historical solver campaigns are already observed data; no globally
untouched-family claim follows from these labels.

```json
{"schema_version": 1, "traces": [
  {"instance": "06.ltl", "family": "numeric-example", "split": "train",
   "path": "06/aut-123-0/cpre-1.tsv", "capture_manifest": "06/invocation.json"},
  {"instance": "robot_grid_pb_4_4_pe_.ltl", "family": "robot_grid", "split": "heldout",
   "path": "robot/aut-124-0/contains-0.tsv", "capture_manifest": "robot/invocation.json"}
]}
```

Run correctness first. `--measure` additionally requires emitted `NDEBUG`, no
stdlib assertions, O3/Ofast and LTO, with matching compiler/target flags across
widths. Assertion-enabled correctness builds cannot qualify for timings. Use a
fresh release directory, for example:

```sh
meson setup build-posets-release subprojects/posets --buildtype=release \
  -Doptimization=3 -Db_lto=true -Db_ndebug=true -Dcpp_debugstl=false
```

Enable measurements only in an exclusive lane:

```sh
python3 benchmarking/posets-matrix.py --build-dir build-posets-release \
  --trace-plan _bm-logs.posets/traces/plan.json \
  --output _bm-logs.posets/replay-check
python3 benchmarking/posets-matrix.py --build-dir build-posets-release \
  --trace-plan _bm-logs.posets/traces/plan.json --measure \
  --rounds 1 --repetitions 3 --case-timeout 15 \
  --output _bm-logs.posets/replay-measured
```

The driver reuses `benchlib.run_systemd_scope`: serial, 8GiB, zero swap. It
records each rejection/timeout/resource limit, fixed-seed shuffled order, compiler
commands, actual SIMD widths/instructions, source/binary/support hashes, trace
and capture hashes, the complete automaton-directory inventory (including
`.skipped`/`.partial` files), complete operation counts when available, and
logical dimension/cardinality/bottom/suffix-sharing features. Source-oracle
timeouts remain unverified traces and cannot qualify a backend for timing.
No point sample is expanded to an invented number of queries.
Scalar controls run once per backend/trace; timing covers the deduplicated SIMD
treatments. With compatible16 and native=max64 this is42 correctness cells and
28 timed cells per trace, plus one independent source-oracle call. The same
compiler target is used throughout; scalar loops may still auto-vectorize.

`detail=0` measures the complete sampled CPre, including the bottom accumulator,
apply/union/intersection and internal hybrid conversions. `--detail` adds
per-operation clocks and is a separate instrumented diagnosis. Both also time
backend construction and reconstruction, including vector materialization,
canonicalization and allocation. Each round reconstructs the source frontier;
it does not claim to retain unobserved full-search sharing history. Cgroup peak
and process CPU include parsing and result checks; operation clocks exclude
those. A CPre replay's membership probe is reconstructed from `init_state` and
is labeled a derived control. Only a contains trace represents an actual query.
The derived control's contains timer includes query-vector construction; an
actual-query timer starts after query materialization. Keep the two trace kinds
separate in comparisons. Detail components need not sum to inclusive CPre time.
Rounds greater than one can reuse allocator/interner/cache state; they remain
repeated point replays, not independent cold solver searches.

Zero numeric prefix with a nonempty Boolean suffix has positive logical
dimension and is replayed directly. The dynamic scalar/SIMD matrix does not
strip that suffix or pretend it tested a packed wrapper. Any integration using
`x_and_bitset` must retain its active-tail/padding oracle, including numeric
prefix zero, and candidate KD split logic must support the resulting domain.

## Hybrid threshold and profiles

The existing default `KD_THRESH(M,D) = D*2<M` is unchanged. A separately built
benchmark can override the function macro, e.g. pass the single compiler
argument `-DKD_THRESH(M,D)=((D)*4<(M))` through Meson's `cpp_args`. The expanded
policy is recorded in metadata and participates in SIMD-treatment deduplication.
Predeclare a small factor grid (1,2,4,8) on training families, then confirm at
most one selected alternative on held-out families. A separately frozen build
and output directory per policy avoids pretending different policies share a
binary identity. `--backends vector_or_kdtree_backed` limits that follow-up;
the initial screen retains all14. Compare whole CPre plus build/rebuild and
contains, rather than only post-conversion queries. Do not tune on held-out
results or change the selected solver default from this microbenchmark.

Use the platform's existing tools for a small, explicitly instrumented subset:
`valgrind --tool=dhat` for whole-process allocations, or
`valgrind --tool=callgrind --instr-atstart=no --cache-sim=yes` with the existing
start/stop requests for measured regions. Keep their artifacts and hashes in
separate directories and do not mix their clocks with clean runs. Cachegrind
simulation is not hardware cache measurement; `perf stat` hardware events may
be unavailable and must then retain that reason. DHAT's whole-process counts
include trace parsing and checks. Until such profiles exist, allocation counts
and cache events remain explicitly uncollected.
