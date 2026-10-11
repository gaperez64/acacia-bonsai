# Environment lifting ordering (#209)

The opt-in `both:gr1-lift:oxidd` worker first gives direct GR(1) a **1 s**
allowance, including independent certificate verification. A verified direct
answer bypasses all seed discovery and U construction. A local allowance stop
releases its artifacts and returns allocator pages before lifting. Discovery,
R/U, checking and the final direct fallback share the original absolute arm
deadline. Cancellation and expiry of that deadline stop the arm. The shipping
`both:gr1-real-lift:oxidd` worker keeps its ordering.

The versioned `TlsfGr1BothOptionsV3` entry point carries this allowance. Zero
retains the earlier ordering; older entry points remain available, including
pure U candidate and independently checked U calls. A direct attempt that reaches
checking can add an unsuccessful check to the total. Only the independently
verified final proof, its exact target binding, and its artifact hashes authorize
a verdict; an attempted check never does.

U eligibility checks the required 3/4/5 seed window, typed target axis and variable
inventory before seed expansion. The 3M-variable preflight is a conservative
search limit derived from the existing node cap, rather than a lower bound on
BDD size or a realizability test. With R disabled, discovery expands only those
three sizes. R discovery still searches its existing windows.

Policy construction computes `forall response` on each implication before
conjoining the implications. This uses `forall c (A and B) = (forall c A) and
(forall c B)`; the environment's existential choice remains outside. The
independent fixed-policy checker is unchanged. `move_*`, which the checker
validates as interface evidence without trusting its Boolean roots, records this
quantified allowed-input relation. Truth-table tests compare permitted choices
and `exists input, forall response` totality against explicit enumeration.

A global structural allowance bounds relation construction and choice extraction
across all fairness modes: `64 * (inputs + latches + AIG ANDs + 1) *
(rank layers + fairness modes + 1)`, clamped to **65,536–1,000,000 apply calls
and new nodes**. The existing cumulative 3M-node/12M-apply guards still apply.
Exhaustion reports `policy_capacity` and safely falls back. The constants bound
construction work using the current graph and rank inventory; they were not
fitted to instance identities or expected verdicts. The 1 s direct quantum is
also global. Difficult inputs may pay for an unsuccessful direct quantum and a
later direct restart; the matched driver screen must assess that tradeoff.

## Evidence and pending admission

Source baseline: Acacia `fd6634f45600cf874130e7eea7c3c2ab0f86147e`, tlsf-tools
`1887bea53b81a53e52d4c2c06c55a88509b0e9b1`. Both working trees remain uncommitted.
The checked build uses GCC 16.2.1, Meson 1.11.2, debugoptimized without LTO.
Separate release/LTO builds have built with `-j 3` for the driver, using the same
`otf_sparse_formula_gr1_real_prepass` preset. Binary SHA-256:

- baseline: `8c79a8b6d71e9f2165220ac7a20192d43ee4719d7d7f4aa6a4c88874cc49d5c7`
- changed: `344715b7354af5b77ee9e8f4d9146c3ef033a4e87d3537ebd145a475ca879c8a`

The optimized binary contains no native fault, routing-pause or new U test hooks.

`opt20260927-p4n4` was fetched and verified against the evidence index:
SHA-256 `c5cd36464c6fba4b04832529454847975c84f6c18a144d93740a2b282e134abd`,
1,403,575 bytes, 9,027 verified members. Reconstruction joins the archived
selection membership (46 U-target rows plus 18 additional parametric UNREAL
rows) to the current logical TLSF source map and hashes every input; it uses no
filename family heuristic. The 64-entry list SHA-256 is
`b1e20db4af1cb0b8a5c5eb32489cba82286e9e3bc05b9073a8f529b1068b2406`.
Scratch inputs and reproduction commands are under `build_scratch/i209/`.

| Comparison | U-on solves | Control solves | U-only answers faster than direct |
|---|---:|---:|---:|
| Archived N4, 64 inputs, 60 s; control is direct G | 29 | 29 | 0 |
| Current master, U-on vs REAL-only, 64 inputs, 60 s | Driver pending | Driver pending | Not measured |
| Changed worker, U-on vs REAL-only, 64 inputs, 60 s | Driver pending | Driver pending | Not measured |

The archive's 28 recorded direct fallbacks have first stops `seed_window` 16,
`schema_capacity` 6, `schema` 2, `candidate_allowance` 2, `typed_alignment` 2.
It has no fallback-boundary timestamps, so it cannot measure exact U decline
costs. Among the 24 cases both routes solve, the archived wall differences are:

| Stop | Paired solved rows | Sum G′−direct (s) | Median (s) | Maximum (s) |
|---|---:|---:|---:|---:|
| seed_window | 16 | 2.368 | 0.059 | 1.400 |
| schema_capacity | 4 | 17.819 | 4.447 | 5.156 |
| schema | 2 | 9.898 | 4.949 | 5.939 |
| candidate_allowance | 1 | 13.264 | 13.264 | 13.264 |
| typed_alignment | 1 | 2.057 | 2.057 | 2.057 |

These are overhead proxies including direct-solve variation, not measured U
phase time. New timestamped route records support separating discovery, U,
checking, direct probes, and fallback by stop cause. Censored/incomplete records
must stay explicit.

No timed panel campaign was run: the user's instruction and charter assign
screens to the driver. `build_scratch/i209/driver-screen.sh` prepares serial
60 s comparisons with the existing coverage runner, 8 GiB/no-swap scopes,
per-input records and memory provenance. The driver must assign identical CPUs,
record thermal conditions, rotate/alternate treatments, and adjudicate near-cap
changes. The supplied treatment order is only a first paired screen. Do not
recycle old rows into these new comparisons.

The single-instance profiles below do not establish coverage/PAR-2 admission.
No demonstrated U answer currently beats direct GR(1); #209 remains open and
U stays opt-in.

## Correctness and build checks

- Acacia unit suite: **82 passed**, none failed.
- Full `python3 -m pytest tests/pytest/ -q`: **2,302 passed, 40 skipped,
  23 subtests passed**.
- Anti-hardcoding guard, including tlsf-tools solver/include trees:
  **96 passed, 1 skipped**.
- Regenerated decline inventory: **745 passed**.
- Ruff on touched Python: passed.
- tlsf-tools full CI format scan with **clang-format 22.1.8**: passed.
- Checked Acacia, standalone native tlsf-tools CI, baseline release/LTO and
  changed release/LTO builds: passed, all compiled with **`-j 3`**.
- tlsf-tools native CI suite: **326 passed, 1 environment failure** initially;
  the remaining `oxidd_build_stamp` test **passed** after setting
  `GIT_WORK_TREE` to the copied OxiDD tree (`GIT_OPTIONAL_LOCKS=0`). Its copied
  Git pointer uses the original checkout as `core.worktree`, so the guard had
  reported an unknown source revision and its stale-commit test could not run.
  All **228 tracked OxiDD files** were byte-identical to the clean pinned
  checkout at `bc4354cbb86f3940bacc6675e4a0d2abb97ed8c4`. No source, guard or
  Git metadata was changed for the rerun. The initial full-suite log and
  successful isolated rerun log are both retained.

Logs, phase records, profiles and frozen binaries are retained under
`build_scratch/i209/`. Unused checked-build objects, archives and unrelated
test executables were removed after verification; rebuilding those tests
requires `meson compile -j 3` again. The active optimized driver builds and
profiled binaries remain available. No commits were made.


## Single-instance diagnostics

These are serial `perf record -F 99 --call-graph dwarf,16384 -m 64`
observations in 8 GiB/no-swap scopes, each with a 60 s outer limit. Neither
builds nor correctness tests ran concurrently with them. Scope time includes
launch and profiler overhead. Worker latency runs from dispatch to the parent's
accepted answer. Memory is the authoritative cgroup-v2 `memory.peak`; there
were no OOM events, resource stops or outer timeouts. This is one observation
per treatment, without cooled repetition or a coverage/noise-floor claim.
Earlier setup attempts (a perf buffer lock-limit failure and missing phase
directories) are retained separately; the table uses the complete paired
telemetry captures.

For `simple_arbiter_unreal2_pb_6_pe_.tlsf`, all four treatments return the same
independently verified **UNREAL** answer:

| Build / arm | Winning route | Scope wall (s) | Worker latency (s) | Scope peak (MiB) | Seed probes |
|---|---|---:|---:|---:|---:|
| Baseline U-on | U | 0.992 | 0.7865 | 220.6 | 16 |
| Baseline REAL-only | direct | 0.275 | 0.1010 | 45.3 | 10 |
| Changed U-on | direct | 0.259 | 0.0605 | 50.2 | 0 |
| Changed REAL-only | direct | 0.288 | 0.1010 | 45.7 | 10 |

The changed U-on worker performs no seed discovery, R/U construction or
U verification on this target: direct GR(1) verifies within its first quantum.
The baseline U profile spends time in EnvBdd apply/unique-table work and
OxiDD manager construction. The after profile contains only 8 samples, too
few for percentage comparisons; route records establish which work vanished.

A separate generated structural probe raises the source's `n = 8` to `n = 9`
in `simple_arbiter_unreal2_pb_8_pe_.tlsf`, with no other source edits. It is
not a panel member or a coverage gain. Pure U bypasses the direct-first route
to exercise policy construction. The frozen baseline and matched changed
Acacia probes both use GCC `-O2 -g -std=c++23`.

| Pure U probe | Stop / context | Total applies | Nodes | Policy time (s) | Scope wall (s) | Scope peak (MiB) |
|---|---|---:|---:|---:|---:|---:|
| Baseline | schema_capacity / mode_relation 0 | 12,000,001 | 2,056,738 | 2.484 | 4.086 | 489.7 |
| Changed, matched build | policy_capacity / mode_relation 0 | 1,486,271 | 379,569 | 0.379 | 1.495 | 190.7 |

Both runs decline safely with zero target checks and no proof verdict. The
changed stop occurs after 1M policy apply calls, while the cumulative counter
also includes prior rank work. It bounds the previously reproduced stop at
apply 12,000,001, rather than claiming a new U answer. An exploratory C++20
CI-build profile has identical operation/node counts (1.104 s scope wall,
0.216 s policy time); the table uses the C++23 matched build.

Generated source SHA-256: `e7edbb8e77df8f753178d626997312d409e8002c3f6cf76b1fe5ed590e2da320`.

The driver must still measure solves, time to answer and time spent before
direct fallback, grouped by U stop cause, on the fresh matched 64-input
screens. Until then, there is **no demonstrated U answer that direct GR(1)
does not find faster** and no admission claim.
