# Command line

[Build the CLI](../README.md#build), then run from the repository root in Bash:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
"$build_dir/src/acacia-bonsai" --help
"$build_dir/src/acacia-bonsai" --version
```

`--help` exits 0; `--version` prints version/configuration and exits 2.

## Read a specification

`-f` takes a quoted formula; `-i` and `-o` take comma-separated input/output
names. Declare every proposition; use an empty string for an empty list:

```bash
"$build_dir/src/acacia-bonsai" -f 'G (req -> F grant)' -i req -o grant
"$build_dir/src/acacia-bonsai" -f 'G grant' -i '' -o grant
```

`-F` reads a formula file and still requires `-i`/`-o`:

```bash
"$build_dir/src/acacia-bonsai" -F tests/ltl/realizable/ltl2dba_U1_1.ltl -i p_0 -o acc
```

`-T`/`--tlsf` reads TLSF, including I/O declarations and timing, without an
external converter:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf
"$build_dir/src/acacia-bonsai" -T examples/unrealizable-small.tlsf
```

The first TLSF example is realizable, the second unrealizable (exit 1).
Raw LTL uses Mealy timing: the controller sees current inputs before choosing
outputs. TLSF can specify Moore timing, where it chooses first.

## Read the result

| Verdict | Exit | Meaning |
|---|---:|---|
| `REALIZABLE` | 0 | A controller can satisfy the specification. |
| `UNREALIZABLE` | 1 | No controller can satisfy it. |
| `UNKNOWN` | 2 | Workers did not decide within their limits. |
| Error | 3 | Invalid input/options or execution failure; read stderr. |

Status 1 is a valid answer and stops shells using `set -e`. Worker diagnostics
also go to stderr.

## Write a controller

`-s FILE` writes ASCII AIGER after a REALIZABLE result, respecting the TLSF target,
including Moore. See the [quick start](../README.md#quick-start).
The [Docker wrapper](../Docker/README.md#write-a-controller) can write to stdout.

Portfolio workers stage controllers in a private directory named
`.acacia-private-XXXXXX` in `FILE`'s parent, independent of its basename length.
After cancelling and reaping competitors, the parent forks one publisher to copy
the selected closed controller through the ordinary controller writer,
following symlinks and truncating existing files in place. Existing inode, mode,
ownership and hard links are preserved; new files use the caller's umask.
The parent supervises publication under the invocation deadline and cancellation
signals; REALIZABLE requires a successful publisher exit and a final acceptance
check. An output-write failure, including a publisher killed by a signal, reports
UNKNOWN with no winner. Normal exit, handled
interruption and timeout remove the directory. Killing the parent with `SIGKILL`
can leave this directory and partial private files behind; they can be removed
after the invocation stops. The requested `FILE` is unchanged until publication.
If the private directory cannot be created, workers retain master's first-proof
selection and write directly to the requested destination, including overwriting
an existing writable file. Failed parity writes still report UNKNOWN with complete
worker records and no winner.

Extraction turns and their memory admission are Linux-only, like optional parity
reduction (#467): other platforms keep master's first-proof selection and recovery
after extraction failure, without grace turns or fallback admission.
The first selected extractor keeps running throughout, under its original resource
policy. After its grace expires, at most one fallback runs concurrently with it.
If admission fails, the original continues alone. The first complete, valid
controller matching the latched verdict is published; the other workers are
cancelled and reaped. At grace or fallback turn expiry, ready proofs matching the
first verdict have priority. Only when none is admissible can an undecided worker
capable of that verdict take a turn
to finish its proof. Its export gate stays closed until it publishes a matching
verdict; an opposite verdict is rejected. Workers capable only of the opposite
verdict stay stopped. Each worker's first turn gets one quarter of the remaining
invocation time from `ACACIA_OUTER_DEADLINE_MONOTONIC` (absolute monotonic seconds),
or 2 s without a known deadline. Fallbacks rotate among themselves; later turns
double separately for each worker, capped at the time remaining when each turn
starts. The grace lets ordinary extraction finish before fallback starts: in the 433-input
SYNTCOMP26 sweep, 375/401 successful master syntheses finished within 2 s
(315 within 250 ms). These whole-run times upper-bound extraction; the Core probe's
2.37 s extraction fits a quarter of its roughly 17 s remaining budget.
Each priority class retains its own round-robin cursor across turns in the other class.
This lets unfinished searches progress while the selected extractor stalls.
Admission reserves the sum of latent anonymous capacity `max(0, VmData - RssAnon)`,
COW exposure (conservatively its
admission RSS), and growth allowance `max(64 MiB, admission RSS)` below the scope's
last eighth. Admission also reserves the original's nonnegative RSS increase since
the current grace or turn began. During concurrent extraction, every 10 ms poll
reads `memory.current` for every previously admitted fallback, including stopped
ones; headroom at or below the last eighth plus its latent/COW/growth reservation
immediately kills one fallback, with nonblocking reaping. After its leader and
tracked descendants are reaped, headroom is checked again; reclamation continues
one victim at a time until the remaining reservations fit or no fallback remains.
The original is never a reclamation victim. Rotation keeps stopped fallbacks
available for later turns while headroom permits. Another fallback needs headroom
above the last eighth plus its own reservation and the original's sampled growth.
The last eighth cushions growth
between polls; crossing it would require the original to allocate more than
`memory.max/8` in one poll, implausible for these extractors. The fallback's own
growth stays capped by `RLIMIT_AS`.
Without a readable finite scope limit (`memory.max=max` included), concurrent
fallback requires the existing parity/helper RSS-share budget to cover its
reservation and the original's sampled growth. Otherwise the original continues
with master's first-proof behavior and extraction-failure recovery.
On Linux, `parent_extraction:no_budget` identifies this selection without changing
its execution.
The fallback's soft address-space limit is capped at its current mapping size
plus **only the growth allowance**; RSS supervision additionally enforces
admission RSS plus growth. The same admission checks and limits apply to undecided
workers. Allocation failure discards that worker. A sole surviving fallback regains
its pre-admission soft address-space limit and loses the additional RSS ceiling. ASan skips both the
address-space cap and latent term with a diagnostic because its shadow mappings
inflate `VmData`; COW/growth admission and RSS supervision remain active.
Discarded workers and their tracked descendants are killed and polled without
blocking supervision; admission waits until their reclamation completes.
If the original fails, unusable workers are reclaimed one at a time and admission
retried before matching unfinished searches are reclaimed.
Decision-only runs skip this scheduling and its cgroup-memory probes.

`--parity-mealy-reduction none|bisim|bisim-output|reselect` selects the parity route's
post-extraction reduction under `-s` only. `bisim-output` uses Spot's
bisimulation with output assignment (simplification level 2); `bisim` uses level 1.
Both preserve realization by specializing the selected controller's allowed output
traces. `none` restores the previous extraction and encoding pipeline. Decision
runs do not execute any reduction. Machines above 512 states, 8,192 edges, or
65,536 label BDD nodes (summed over edges) skip the optional work. SAT minimization
is deliberately excluded because Spot's API offers no cancellation budget.
The default, `reselect`, additionally re-solves a copy of small tier-4 arenas after sorting
outgoing edges by destination, then applies level 2. It retains the alternative
only if its encoded circuit has fewer AND gates plus latches. The second solve is
skipped above 2,048 arena states, 8,192 arena edges, or four acceptance sets. It does not change
the decision solve, winning regions, or exported game certificates.

The incumbent is encoded before optional work starts. Both attempts share at most
200 ms and 5% of the time remaining before the invocation deadline. Each runs in a
forked child with a private address-space/RSS cap of at most 512 MiB; the restriction
prepass uses the same subprocess supervisor. With a finite `memory.max`, scope usage must
leave room below the portfolio's 7/8 kill threshold for the child plus a full
resident copy-on-write margin (at least 64 MiB) and result transfer. The child cap
uses at most half the headroom left after that margin. With `memory.max=max`, an
absent memory controller/file, or no cgroup support, there is no finite threshold:
reduction runs within its own time and memory caps without reading `memory.current`.
In either case the child cap also reserves the worker's resident set and result
mapping within the supervisor's per-worker RSS share. Skip if the remaining cap
cannot cover the child's inherited RSS plus 64 MiB, or if worker RSS cannot be read.
Reduction requires RSS sampling, which is supported only on Linux today. On other
platforms it skips reduction and exports the unreduced incumbent controller.
Insufficient finite headroom records `scope_headroom`; an exhausted worker share
records `rss_share`. A present but unreadable, empty, malformed or overflowing
`memory.max`, unreadable cgroup membership, or missing/unreadable/malformed
`memory.current` under a finite limit records `scope_memory` and skips reduction.
A timeout, resource limit, child failure, invalid result, or non-improvement retains the
incumbent. Accepted AIGER is capped
at 1 MiB and checked structurally without rebuilding BDDs in the parent. The
200 ms cap also applies when no invocation deadline is supplied.
`parity_reduction_attempt` phase records report the option, attempt, outcome,
cause, wall time, deadline, and memory cap.

`--aiger-mode MODE` selects Spot's runtime encoder for every synthesis route.
The default encoder remains `isop`. Use `ite` (if-then-else),
`isop` (irredundant sum of products), or `both` (try both and keep the smaller encoding).
Append `+ud` to try negated conditions too, and
`+sub0`, `+sub1`, or `+sub2` for no splitting, input/latch/gate splitting (ISOP),
or common-subexpression search. Modifiers may appear in any order; repeated
modifiers follow Spot's behavior (the last `subN` wins). For example:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf -s controller.aag \
  --aiger-mode both+ud
```

Spot 2.16 also accepts `optim` (experimental global BDD encoding) and a
comma-separated list of alternatives, retaining the circuit with the fewest
gates. Acacia accepts a canonical subset of Spot's syntax: no empty components.
Empty methods/modifiers (including trailing `+` or `,`) and unknown tokens are
rejected before solving, both by the CLI parser and direct C++ entry points.
The flag is case-sensitive; the dual modifier is spelled `+ud`, not `+dual`.
This option changes encoding only; strategy extraction and the selected reduction
are independent. Compare sizes only after independent verification.
The `+dc` output don't-care modifier is rejected before solving, including in
comma-separated alternatives. The smoke sweep found incorrect controllers with
Acacia's original BDD dictionary; fresh-dictionary HOA replay did not reproduce
them. The cause may involve Acacia's machine/BDD-variable handling as well as
Spot; see issue #466 and the
[measured rejections](../benchmarking/RESULTS.md). The default remains `isop`.

## Choose solver workers

The portfolio runs workers called arms. `--arms` replaces the whole portfolio:

```bash
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant \
  --arms real:small:backward,unreal:formula:forward
```

`real:small:backward` proves realizability with small-automaton translation and
backward search. `unreal:formula:forward` proves unrealizability through a formula
transformation and forward search. Compiled components determine available arms;
see [configuration](configuration.md) and `--help`.

`-r LIST` selects realizability with `small`/`any` translation; `-u LIST` selects
unrealizability via `formula`, `automaton` or `both`. Each enables only its own
direction unless both are supplied. Both need values and cannot combine with
`--arms`.

`-K` sets the final counter bound, `-M` its starting value and `-I` its increment.
Reaching the bound without proof can return UNKNOWN.
The default counter schedule starts at 2, increments by 3 and ends at 99.
