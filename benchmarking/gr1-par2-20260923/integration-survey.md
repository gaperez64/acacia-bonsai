# GR(1) lifting integration survey

Snapshot: 2026-09-24. This is a design/provenance survey; no solver was run. `B` below means the
frozen benchmark baseline, not a virtual best over Docker images.

## 1. Exact baseline B and its execution boundary

### Identity

| Item | Exact meaning | Evidence |
|---|---|---|
| Deployed group pointer | `docker_default` currently lists `otf_sparse_formula`, `best_decomp_rank_bucketed_semantic_mona`, `best_four_arm_bboxtree`, and `best_decomp_mona_any`. These are four separately shipped images/configurations, not one four-binary race. | `config/acacia-presets.json:516-522`; `benchmarking/witness-lifting-20260918/manifest.json:15-20` |
| B | The group's headline/first preset, **`otf_sparse_formula` alone**, selector disabled. Its compiled default is the four-child portfolio `real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward`. | `benchmarking/witness-lifting-20260918/manifest.json:23-31`; `config/acacia-presets.json:382-386` |
| Frozen executable | `build_w1_B/src/acacia-bonsai`, source revision `50384cf69a733199fa17c9c571da1761d8451991`, release/LTO/native profile, SHA-256 **`398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a`**. | `benchmarking/witness-lifting-20260918/manifest.json:4-5`; `benchmarking/witness-lifting-20260918/manifest.json:23-31` |
| Not B | The `6467869a...` executable is the frozen G1 correctness-gate baseline, not the deployed/full-corpus B. | `benchmarking/witness-lifting-20260918/manifest.json:53-59` |

The live read-only command `python3 scripts/acacia-config.py list-group docker_default` returned the
four presets above in that order, matching the registry.

`scripts/acacia-bonsai.sh` reloads the group dynamically, chooses its first member when the first
argument is an option, resolves `build_$CONFIG/src/acacia-bonsai`, and `exec`s it. `--tlsf` is only a
shell convenience which becomes native `-T /dev/stdin`; there is no SyFCo conversion in this path
(`scripts/acacia-bonsai.sh:10-12`, `scripts/acacia-bonsai.sh:42-67`,
`scripts/acacia-bonsai.sh:75-91`). Thus a deployment invocation whose first argument is an option
(for example `--tlsf`) and names no preset currently selects `otf_sparse_formula`; a zero-argument
invocation prints usage. A frozen campaign instead names `build_w1_B` directly and records its hash
rather than following the mutable group pointer.

### Campaign invocation and charging

| Path | Input and command | Deadline/resource boundary |
|---|---|---|
| Full coverage | `run-syntcomp26-coverage.py` resolves the 1,524-ID list and TLSF map, hashes the executable, then runs `[B, *flags, "-T", tlsf_path]`. This is the native TLSF frontend; no `.ltl/.part` pair or SyFCo conversion is involved. | One `run_systemd_scope` call per row with timeout=`cap`, `MemoryMax=8G`, `MemorySwapMax=0`, and optional whole-invocation CPU controls (`benchmarking/run-syntcomp26-coverage.py:805-854`, `benchmarking/run-syntcomp26-coverage.py:917-952`). |
| Subset | With `--tlsf-map --tool acacia`, `run-subset.py` also uses native `[B, "-T", tlsf]`. Only the old no-map path uses Acacia's `-F/-i/-o` LTL adapter. | `--systemd-scope` puts that command under the same timeout and memory parameters (`benchmarking/run-subset.py:218-243`, `benchmarking/run-subset.py:287-336`). |
| Docker CLI | `scripts/acacia-bonsai.sh --tlsf` feeds TLSF stdin to the linked frontend. Without `--tlsf`, arguments pass through unchanged. | The shell adds no timeout/cgroup; Docker or its caller owns those limits. |

The scope has `KillMode=control-group`, so all portfolio descendants share one memory/CPU budget;
the wall timer is monotonic, stops the named scope on expiry, records `MemoryPeak`, and always tears
down remaining descendants (`benchmarking/benchlib.py:512-575`, `benchmarking/benchlib.py:605-729`).
The raw row records whole-invocation wall time, exit, timeout, binary hash, limits, scope and scope
peak (`benchmarking/run-syntcomp26-coverage.py:953-985`). Exit/output agreement is mandatory:
Acacia 0/1/2 means REAL/UNREAL/UNKNOWN; timeout and OOM take precedence and malformed disagreement
is ERROR (`benchmarking/benchlib.py:778-811`).

### Existing portfolio support

There are two different mechanisms, neither a host for an external lifting route:

- The C++ `--arms` parser accepts only Acacia polarity/transform/backend/provider tuples and the
  main process forks all selected arms as concurrent children (`src/arg_parser.hh:140-152`,
  `src/acacia-bonsai.cc:62-64`, `src/acacia-bonsai.cc:133-152`). It has no external-command arm and
  no sequential prepass/fallback state.
- `benchmarking/run-portfolio-arms.py` does the opposite: it isolates one ordinary Acacia arm per
  invocation for attribution, then runs whole corpus campaigns **sequentially**. A manifest can
  name another executable, but it becomes another series, not a per-instance route followed by B
  (`benchmarking/run-portfolio-arms.py:1-22`, `benchmarking/run-portfolio-arms.py:78-95`,
  `benchmarking/run-portfolio-arms.py:107-142`).

It can supply preregistered single-arm commands for a cheap first stage, but it cannot itself
schedule `cheap -> lift -> B` within one row.

## 2. TACAS23, ltlsynt, and three-way data

### Adapters and conversion boundary

| Series | Route | What is timed |
|---|---|---|
| TACAS23 Acacia v1 | Exact revision `5ffd8f994d2348759b721f64d9fa3c9b5b8a8607`. `run-subset.py --tool acacia1x` invokes `-c BOTH -F formula --ins ... --outs ...`. v1 predates `-T`, so it uses SyFCo `ltlxba` pairs whose semantics are overwritten to the TLSF-declared target. | The already converted `.ltl/.part` solver call only. Pair generation is outside the row (`benchmarking/run-subset.py:95-111`, `benchmarking/run-subset.py:247-251`; `benchmarking/make-syfco-pairs.sh:1-7`, `benchmarking/make-syfco-pairs.sh:20-28`). |
| ltlsynt | `run-subset.py --tool ltlsynt` invokes `--realizability -F formula --ins=... --outs=... --semantics=Mealy|Moore` on the **unadapted** SyFCo pair. Strict TLSF has no equivalent ltlsynt switch and is explicitly a semantic mismatch. | `convert_tlsf()` queries semantics and creates/reuses the pair before `run_systemd_scope`; therefore neither conversion nor even the cache-hit semantics query is in `seconds`. Cache hits return at lines 160-161; timing starts at lines 321-330 (`benchmarking/run-subset.py:112-123`, `benchmarking/run-subset.py:137-185`, `benchmarking/run-subset.py:293-331`). |

The seven `finding_nemo` inputs remain `SYFCO-FAIL`; they must not be removed by intersecting
successful inputs (`benchmarking/plots/three-way-full-20260905/README.md:28-35`). Cached external
rows are therefore legacy solver-only measurements, not source-to-answer frontend timings, exactly
the distinction required by `benchmarking/gr1-par2-20260923/plan.md:607-619`.

### Latest full-corpus observations

| Leg | Latest full corpus/caps | Binary and regime |
|---|---|---|
| B | `benchmarking/witness-lifting-20260918/opening/{120s,17s}/epoch-{1,2}/B-cap*.tsv`, with validated `.csv` and `.raw.tsv` exports. Both epochs at each cap have 1,524 rows. | Frozen B/hash above. Fedora kernel 7.2.5, 16 CPUs/15 GiB, no CPU quota, serial quiet host, one 8 GiB/no-swap scope per invocation (`benchmarking/witness-lifting-20260918/manifest.json:75-87`; completion at `benchmarking/witness-lifting-20260918/decisions.md:25-33`). |
| ltlsynt | `benchmarking/witness-lifting-20260918/opening/120s/ltlsynt-cap120.csv` and `.../17s/ltlsynt-cap17.csv`, 1,524 rows each, created from 24 shards. No timing row is reused across caps; only the conversion cache is reused. | `/usr/local/sbin/ltlsynt`, version `2.15.1.dev`, under the same serial 8 GiB/no-swap regime (`benchmarking/witness-lifting-20260918/commands.sh:41-60`, `benchmarking/witness-lifting-20260918/commands.sh:78-89`). W1 records path/version but **not its binary hash**. The most recent prior committed pin for that path/version is `ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900` (`benchmarking/demand-sparse-20260916/closing/manifest.json:37-45`); identity with W1 must be re-hashed, not assumed. |
| TACAS23 v1 | `benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv`, 1,524 rows, uniform **17 s only**. No full 120 s v1 leg exists in the retained latest data. It was reused unchanged in the 2026-09-09 report. | Preserved `acacia-v1-best23`, O3/LTO/native, revision `5ffd8f99`; local frozen-binary provenance gives SHA-256 `75fabd3c081030512a0fa5348244aee7382144e0043eb1da2b581651edf84cd8` (`_bm-logs.fmcad26-head-6dda2f3b-20260822/provenance/v1-binary-sha256.txt:1`). The committed report records its path/build flags but not the full hash (`benchmarking/plots/spot-otf-threeway-20260909/PROVENANCE.txt:6-14`, `benchmarking/plots/spot-otf-threeway-20260909/PROVENANCE.txt:39-50`). The run used sequential 17 s scopes, 8 GiB/no swap (`benchmarking/plots/three-way-full-20260905/README.md:1-8`). |

Consequently, a final matched 120 s three-way needs fresh TACAS23 and new-Acacia legs and an
explicit hash of the actual ltlsynt executable. The old 17 s v1/ltlsynt CSVs are reusable only if
binary, inputs, flags and host regime are shown to match; the plan forbids a synthetic virtual score.

### Raw-to-report path

`run-subset.py` writes the four-column rows `instance,result,seconds,exit`
(`benchmarking/run-subset.py:331-342`). Coverage rows are richer TSVs; `export-cactus` requires one
uniform-cap row for the exact list, consistent provenance, no unresolved conflicts, finite values,
and verdict/exit agreement, then writes the same four-column CSV plus a five-column `.raw.tsv`
sidecar. MEMOUT becomes RESOURCE_LIMIT and CRASH becomes ERROR only in the plot view
(`benchmarking/run-syntcomp26-coverage.py:588-691`,
`benchmarking/run-syntcomp26-coverage.py:694-729`).

`cactus-report.py` rejects duplicate/mismatched instance sets, counts only REAL/UNREAL as solved,
charges every other result at `2*cap`, and reports input hashes, solved split, failure categories,
PAR-2 total/mean and plots (`benchmarking/cactus-report.py:83-150`,
`benchmarking/cactus-report.py:190-275`, `benchmarking/cactus-report.py:375-415`). Run it separately
for 17 and 120 seconds using the three labels in
`benchmarking/gr1-par2-20260923/plan.md:625-638`; preserve the coverage TSVs,
exports, external raw CSVs, conversion failures and wrong-answer audit.

## 3. Integration choices for N

All viable designs must make the **outer** observation one process tree: eligibility, all SyFCo
calls, cheap probes, lift/generalize/check, fallback startup and B are charged to the row and the
same 8 GiB/no-swap scope. A stage timeout is not the row timeout unless no later stage can run.

| Design | Measurement and classification | Attribution/files | Main risks |
|---|---|---|---|
| **A. Logic in the coverage adapter** | Today the coverage runner can scope only one argv. If it runs stages itself, they become separate scopes/peaks; keeping one scope requires refactoring `benchlib.run_systemd_scope` to run an orchestration callback inside the scope or launching a helper, which collapses into B. Final REAL/UNREAL/UNKNOWN still must obey Acacia 0/1/2; outer expiry is TIMEOUT and OOM is MEMOUT. | Change `benchmarking/run-syntcomp26-coverage.py`, `benchmarking/benchlib.py`, their tests and output schema; duplicate equivalent adapter work in `run-subset.py`. Add route/stage fields to raw TSV. | Large benchmark-harness change, easy nested scopes, split memory peaks, different teardown behavior, and a harness that now owns solver policy. |
| **B. Acacia-compatible wrapper executable** | The coverage runner invokes `scripts/acacia-lift-portfolio.py ... -T source` as its one scoped command. The wrapper sequentially runs preregistered cheap single-arm B probes, source binding/lifting under a cutoff, then `exec`s the unchanged frozen B with the original `-T` and remaining outer time. Children inherit the existing scope; the wrapper creates/kills/reaps only process groups, never another cgroup. Whole wall and `MemoryPeak` are already measured correctly. | New wrapper plus maintained package in section 6. A small coverage-runner addition supplies an absolute monotonic deadline and a unique route-record path in the child environment, and copies selected JSON fields into raw TSV. `run-subset.py` needs no new solver semantics because the wrapper preserves Acacia's CLI/exit contract. After admission, `scripts/acacia-bonsai.sh` may select the wrapper for deployment; do not change it for discovery. | Re-reading native TLSF and rerunning an isolated Acacia arm in B duplicates frontend/translation work; this is charged and must be measured. Buffer/suppress losing-stage output. Kill a timed-out route completely before B. A wrapper-local deadline based only on its own start would omit `systemd-run` startup; pass a conservative deadline computed immediately before the outer launch. |
| **C. Native C++ portfolio route** | Extend the parent from a concurrent arm race to staged external-route/fallback scheduling. It naturally remains in one scope and can emit the same exit contract. | Change `src/portfolio_arm.hh`, `src/arg_parser.hh`, `src/acacia-bonsai.cc`, build/install rules and diagnostics. Add a Python/executable arm ABI and route records. | Much larger semantic surface: current arms are homogeneous concurrent C++ children, while lifting is a sequential Python/native-helper pipeline. It couples Python/tool paths to B, risks overlapping BDD managers, and still needs absolute sub-deadlines and child cleanup. |

### Recommendation: B, with one thin campaign hook

Use the wrapper as the scheduler and leave both the coverage framework and frozen B's internals
unchanged. `run-portfolio-arms.py` provides known single-arm argv shapes, so an initial cheap stage
can invoke a preregistered B arm for a very small hard budget; a decisive answer is sound and an
inconclusive/killed probe falls through. Do **not** call an unrestricted B as the prepass. The Spot
fast path currently falls through to the normal solver when inconclusive
(`src/solver/solver_invoker.cc:560-598`), so there is no existing `fast-only` CLI to pretend is a
separate route.

Minimal runtime protocol:

1. `run-syntcomp26-coverage.py` computes `outer_deadline = monotonic_now + cap` immediately before
   `run_systemd_scope`, supplies it and a route-record filename in `env`, and otherwise continues to
   run one argv exactly as today.
2. The wrapper reads/spools the TLSF source once if it is stdin, starts no nested scope, and gives
   each cheap route and lifting `min(stage_limit, outer_deadline - fallback_reserve - now)`. It
   buffers their output. On a verified decisive 0/1 result it emits exactly one verdict and exits
   0/1. On decline, UNKNOWN, stage timeout or an expected route failure it kills/reaps that stage and
   continues. Configuration/programming corruption is not disguised as a decline.
3. Before `execve(B, original_argv)`, atomically write JSON containing invocation/input hash,
   capability and binding reason, each route argv/hash/result/exit/elapsed/peak if available, lift
   stages and elapsed, fallback start/remaining time, and `winner="fallback-pending"`. The final
   coverage row establishes B's outcome; joining it with the sidecar changes that to winner B or
   final B non-answer. A lift win records `winner="lifting"` before exit. Keep final stdout clean.
4. Outer TIMEOUT and OOM remain TIMEOUT/MEMOUT. If B returns UNKNOWN, the row is UNKNOWN. A clean
   eligibility decline followed by B is attributed to B, not ERROR. A wrapper failure that prevents
   fallback returns a non-contract exit and is ERROR. Existing conflict collection still audits any
   wrong decisive answer.

This reuses the exact whole-tree accounting at `benchmarking/benchlib.py:524-537` and the existing
coverage normalization at `benchmarking/run-syntcomp26-coverage.py:524-533`. It also makes the
route independently replaceable without teaching the campaign driver a new portfolio framework.

## 4. Source-binding cost and cheap sound decline

All 14 registered capabilities currently require exactly parameter tuple `("n",)`
(`benchmarking/param-lift-20260922/request.py:63-78`,
`benchmarking/param-lift-20260922/request.py:81-166`). Nevertheless, current source binding builds
the full actual identity before it checks any candidate's parameter signature:

| Work before an arbitrary non-parametric/LTL-only TLSF declines | Calls |
|---|---:|
| `tlsfinfo --parameters` | 1 |
| `tlsf2tlsf` to find concrete parameter assignments, even when the name tuple is empty | 1 |
| Actual identity: `tlsf2tlsf --basic`, four `tlsfinfo` queries (semantics, target, expanded inputs, expanded outputs), and `tlsf2ltl` | 6 |
| Template instantiation | 0: only now does the loop see `() != ("n",)` and skip every capability |
| **Total** | **8 subprocesses** |

Every tool call has a 5 s timeout (`benchmarking/param-lift-20260922/request.py:301-315`); the exact
sequence is at `benchmarking/param-lift-20260922/request.py:329-381` and
`benchmarking/param-lift-20260922/request.py:403-435`. A raw/non-parseable `.ltl` passed where TLSF
is required will usually fail on the first `tlsfinfo` call; the eight-call estimate is for a valid
non-parametric TLSF whose body is an arbitrary LTL family. No workspace/generalizer is created on
this decline because request resolution precedes `_create_workspace`
(`benchmarking/param-lift-20260922/param-lift-campaign.py:294-340`,
`benchmarking/param-lift-20260922/param-lift-campaign.py:808-813`). Conversely, an unrelated source
which does declare `n` and has no family hint may cause six identity calls for each of all 14 pinned
templates: 8 actual-side calls + 84 template-side calls = up to **92** successful subprocesses before
decline, plus reads/hashes. This is charged online work.

Cheapest fail-closed order:

1. Validate reduction mode; read/decode and hash the actual bytes. If a family hint is supplied,
   reject an unknown capability and verify its pinned template hash before invoking SyFCo.
2. Run only `tlsfinfo --parameters`; index capabilities by exact parameter-name tuple. The common
   non-parametric input now declines after one tool call. This uses source structure, not filename or
   historical verdict.
3. Only for a nonempty candidate bucket, run `tlsf2tlsf` once to obtain concrete values; check the
   target hint, integer bounds and route-kind/reduction compatibility.
4. Compute cheap actual metadata/basic form, rejecting semantics/target/I/O mismatches before the
   lowered formula where possible. Then compute the exact lowered identity required by the current
   content-verification contract.
5. Instantiate only the shortlisted templates. Cache expected identities by
   `(template_sha256, lowering-tool hashes, parameter values)` in a validated immutable capability
   data file; a missing/mismatched cache entry falls back to recomputation or declines. Never use a
   basename, M0 status, or a prior answer.

The final equality must remain the current basic/formula/semantics/target/ordered-I/O comparison;
this ordering only moves sound rejects earlier (`benchmarking/param-lift-20260922/request.py:1-14`).

## 5. Lifting sub-budget choices (not an admission decision)

P2a is one 120 s observation per target, not a 17 s campaign. Its decisive walls range from 1.171 s
to 117.732 s; two rows consume the cap and remain UNKNOWN. The detailed values are at
`benchmarking/gr1-par2-20260923/s0/s0-summary-P2a-e2e.md:7-30`. L0 replay has a slower 33.7-55.9 s
tail on rru2/cancel/inpchange/amba/load-balancer
(`benchmarking/gr1-par2-20260923/p0-replay/replay-L0.tsv:2-27`). Treat these as cutoff-planning
evidence and remeasure the integrated wrapper.

| Policy family | At 120 s | At 17 s | Trade-off |
|---|---|---|---|
| Fixed fallback reserve | A 30 s lift cap catches P2a through cancel n10/load-balancer n9 but misses rru2 n7 (31.053) and amba n15 (35.903); 40 s catches both and leaves 80 s for B. A 60 s cap covers every decisive L0 replay row (maximum 55.874) and every P2a success except buffer n9 (117.732), leaving 60 s for B. | Candidate cutoffs 2/5/8/12 s leave 15/12/9/5 s for B. From P2a, roughly: 2 s reaches the smallest arbiter/rru rows only; 5 s adds lbu2 n6; 8 s adds cancel n8 and rru2 n6; 12 s adds cancel n9, lbu2 n7 and load-balancer n8. Boundary noise/startup requires margin. | Transparent and easy to preregister. A near-cap route win can still destroy more B coverage than it gains. Buffer n8 at 15.268 s, L0 buffer n8 at 16.095 s and L0 cancel n8 at 16.882 s leave no useful 17 s fallback. |
| Fixed fraction | Examples: 25% = 30 s lift/90 s B; 50% = 60/60. | 25% = 4.25/12.75; 50% = 8.5/8.5. | Stable across caps, but the same fraction crosses very different measured family frontiers. |
| Capability/size tier | Give only source-verified, preregistered small/direct families a short cap; give larger known capabilities 30/40/60 s tiers. | Limit eligible small/direct routes to approximately 2/5/8 s; otherwise decline immediately to B. | Better reserve use, but tiers must use source capability/size available inside the invocation, never expected status or learned per-instance answers. |
| Progress-aware cutoff | Keep a hard outer lift cap and B reserve; continue only at validated stage checkpoints whose remaining-work bound is useful. | Only worthwhile if checkpoints arrive well before 5-12 s. | Can rescue variable workloads, but current long native calls can censor progress. It needs a sound, tested cancellation/cleanup contract and cannot extend the preregistered hard cap. |

In every policy, eligibility and wrapper/process startup consume the route budget and the outer cap.
The 117.732 s buffer result is a research closure, not a sensible sequential prepass at 120 s, and
none of the approximately 15-17 s rows should be credited as a 17 s portfolio gain without an
actual integrated 17 s observation.

## 6. Maintained package extraction

The current checkout has grown beyond the brief's approximate size: `generalize_gr1.py` is 4,475
lines at this snapshot. Movement should be a refactor-only change with the dated evidence retained,
following `benchmarking/gr1-par2-20260923/plan.md:547-565`.

Proposed Acacia-owned layout:

```text
scripts/acacia_lift/
    __init__.py
    capabilities.py       # Capability/SourceRequest and fail-closed source binding
    tools.py              # explicit ToolConfiguration, probes, child environments
    artifact.py           # validated AIG/provenance/ABI import and hashes
    bdd_kernel.py          # manager lifetime, compose/relabel, bounded memo tables
    schema.py              # roles, projections, invariant/rank schema data
    instantiate.py         # target predicate/policy construction and export
    runner.py              # one absolute deadline, subprocess groups, solve/check
    evidence.py            # stage events, immutable identities, outcome JSON
    diagnostics.py         # optional S0 counters/timers
    buddy_veccompose.py    # Python ABI/validation for the native helper
    native/
        buddy_veccompose_adapter.cc
    data/
        capabilities-v1.json
scripts/acacia-lift-portfolio.py   # production Acacia-compatible scheduler from section 3
```

Map `request.py` to `capabilities.py`, `tool_config.py` to `tools.py`, and `s0_diagnostics.py` to
`diagnostics.py`; split the generalizer along artifact/kernel/schema/instantiate/runner/evidence
ownership. Keep `benchmarking/param-lift-20260922/generalize_gr1.py` as the dated forwarding CLI with
its old argv, and keep `param-lift-campaign.py` as the dated cold/reproducer/evidence driver importing
the package. Thin forwarding copies of the other dated modules may remain until all historical
commands migrate. The production wrapper imports the same package; it must not reimplement lifting.
This keeps Acacia portfolio policy in Acacia and does not create the prohibited third plugin
framework (`benchmarking/gr1-par2-20260923/plan.md:551-563`).

Current runtime TSV dependencies are exactly:

| File | Runtime use | Extraction disposition |
|---|---|---|
| `m4-alignment.tsv` | `stable_from` and role-class count | Move the required fields into validated, versioned `capabilities-v1.json`, bound to source/template hashes. |
| `m4-invariant-separability.tsv` | measured invariant arity | Same; retain the original TSV as dated evidence. |
| `m4-move-separability.tsv` | measured move arity; also read by `prove_all_n.py` | Same; retain the original TSV and teach the proof driver to use the package data. |
| `m4-results.tsv` | If present, `_write_result` reads, replaces and rewrites the ledger | Make this an explicit `--results-out` evidence artifact, never installed semantic input. |

The bindings are visible at `benchmarking/param-lift-20260922/generalize_gr1.py:41-54`, the three
schema reads at `benchmarking/param-lift-20260922/generalize_gr1.py:649-687`, and the result-ledger
read/write at `benchmarking/param-lift-20260922/generalize_gr1.py:3806-3828`; `prove_all_n.py`
separately reads the two separability tables at
`benchmarking/param-lift-20260922/prove_all_n.py:37-39`. The other present files
`m4-alignment-2param.tsv`, `m4-invariant-separability-round2.tsv`, and `m4-spec-arity.tsv` have no
current runtime reads. Do not delete any of them during extraction: first replace the dependencies,
add regression fixtures proving identical schema/capability decisions, then archive the originals as
required by `benchmarking/gr1-par2-20260923/plan.md:567-581`.
