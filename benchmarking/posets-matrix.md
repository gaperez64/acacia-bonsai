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
