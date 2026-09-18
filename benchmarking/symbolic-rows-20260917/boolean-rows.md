# A1 raw-row correctness and A2 measured interpretation

## Identity

- Binary: `build_check/src/acacia-spot-provider-replay`, SHA-256
  `94ab301c9b55abfe69fa5d9e73bc2cc24710323029684676ad85808767bbe161`, built from
  commit `e9729dc709ce493c4ca8a1748b2bc9dee51eef03` (R0 recovery + A1 Boolean
  folding + the A2 stop-after-first-row/`--formula-file` replay additions), all
  on this sprint's branch. `debugoptimized` (`-O2`, no LTO, assertions kept),
  `-Dacacia_enable_tlsf_frontend=true -Dbuild_research_tools=true` — the
  **checked** profile per `CLAUDE.md`, not the `release` measurement profile;
  this is a completion/mechanism diagnosis, not a timing claim, so absolute
  wall-clock numbers below are informative context only, not comparable to any
  `self-benchmark.sh`-driven series.
- Targets: the frozen P2 list, [`targets/p2.list`](targets/p2.list), SHA-256
  `5779ea54db76098b8e92c1c77beaf6bf008fb1f6772c2a75ba0f0dc8f0e3ec2b`, matching
  the hash recorded in the previous sprint's `targets.md` byte for byte — the
  exact same 22 IDs, not a reselection.
- Jobs: the previous sprint's own captured transformed worker formulas/
  inputs/outputs/partition for the **unreal:formula** route (the orientation
  P2 diagnosed), read from
  `_bm-logs.20260916-demand-sparse/diag-p2mech/wrec/p2mech-solo-unreal-closure/17/<target>/*.json`
  (gitignored, locally present, hash-identified per job in
  [`targets/a2-jobs-manifest.json`](targets/a2-jobs-manifest.json) by
  `worker_boundary_hash`). `ltl2dba_C2_unreal_pb_100_pe_.ltl` has two captured
  jobs (a decomposed component pair), both replayed, matching the previous
  report's `[1]`/`[2]` treatment. 23 jobs over 22 targets. The replay tool's
  own `capture_boundary` call uses a fixed `captured-worker;effective=Mealy`
  target string, so the replayed `worker_boundary_hash` values will not equal
  the originally captured ones; the (formula, inputs, outputs, partition)
  tuple that actually drives provider construction is reused byte-identically.
- Driver: [`targets/run-a2-first-row.py`](targets/run-a2-first-row.py), raw
  output [`targets/a2-first-row-results.tsv`](targets/a2-first-row-results.tsv)
  (46 rows: 23 jobs × {enumerative, symbolic-boolean}).

## A1 raw-row correctness

Not re-measured here; already established by `closure_buchi_provider_test.cc`
(committed, `meson test closure_buchi_provider_test`, 50/50 unit suite green
on this same commit): the new `boolean_folding()` test compares enumerative
and symbolic-boolean raw maps directly via `signature()` on shared-dictionary
provider pairs for the wide interleaved family (n=1,3,8), and separately
checks the merge-key invariants (`(a|b)&a`, `(a|b)&X(a)`, mixed `p|X(q)`,
U/R postponement, same-next/different-postponed). See the A1 commit message
for the full list. This section is the **A2 measured interpretation** the
plan's evidence bundle asks for alongside that correctness evidence, not a
restatement of it.

## A2: does A1 alone unblock the frozen P2 cohort's first row?

**Bounded scope, per the plan.** This is the `--stop-after-first-row`
diagnosis (row success/failure, never a verdict), same research binary, same
transformed job/AP order/partition, default resource budgets (2,000,000
branches/guards/row-edges, 200,000 states, unbounded live-BDD-node/byte caps)
— the exact caps the previous sprint's P2 diagnostic used. No limits were
raised to make either mode look better, per §4.2's explicit prohibition.

**Answer: no.** Of the 19 targets that failed their first row under
enumerative (matching the previous sprint's 19/22 finding almost exactly —
the previous run additionally used a research-stack binary with inherited
P3a code, so small count differences are expected, not a regression), **zero**
newly complete under symbolic-boolean at these budgets. The 3 that already
completed under enumerative (`CheckAlarm_6bea956e`, `collector_v1_pb_11_pe_`,
`OneCounterInRangeA2`) still complete.

**But the mechanism fix is real and measurable**, not a no-op:

| Group | Count | What's observed |
| --- | --- | --- |
| Already completing, both modes | 3 | `OneCounterInRangeA2`: 317 → 216 branches (−32%); the other two show 0% change (their first row has no Boolean-Or redundancy to fold) |
| Still fails, but symbolic uses measurably fewer branches or a different (later) limit | 11 | `prioritized_arbiter_unreal2_pb_100_pe_`: 2,000,000 branch-capped → 1,500,030 guard-capped (−25%); `ltl2dba_C2_unreal_pb_100_pe_` both components: branch-capped → ~18% fewer, guard-capped; `06`/`07`: −13.5% guard-capped both ways; `thermostat-GF-unreal1`: −12.8%; four more show a small but nonzero reduction (`arbiter-paper-unreal-unreal`, `chain-60`, `follow0`, `GF-G-contradiction7`, `robot-to-target-charging-unreal0`) |
| Still fails, no measurable branch-count change | 9 | `Demo1_06e9cad4`, `full_arbiter_unreal1_pb_3_2_pe_`, `lift_wrong_physics_unreal_pb_5_pe_`, `load_balancer_unreal2_pb_5_pe_`, `SPIPureNext` (all `branch_limit`, unchanged); `ltl2dba_R_pb_10_pe_` (`state_limit`); `ltl2dba_theta_pb_100_pe_` (`memory_limit`); `robot_grid_pb_5_5_pe_`/`pb_6_6_pe_` (`guard_limit`, unchanged) |

Full per-target numbers: [`targets/a2-first-row-results.tsv`](targets/a2-first-row-results.tsv).

**Reading this honestly, per the plan's own caveats:** a branch/guard-count
reduction with the *same* outcome (still fails) is consistent with removed
Boolean-witness redundancy that was real but insufficient to fit the
remaining genuine temporal fan-out into the default budget — it is not
evidence that raising the budget would be a legitimate fix (§4.2 forbids
that inference here), and it is not itself a completed useful query. The 9
unchanged targets are the plan's "many genuinely different continuations"
case almost by construction: three fail via `state_limit`/`memory_limit`/an
unchanged `branch_limit` at exactly the state-discovery or step ceiling
rather than anything Boolean, and the other six's identical branch counts
before and after mean their expansion path never reaches a Boolean-Or node
before exhausting the budget in the first place.

## Decision (plan §5.3)

- **Boolean explosion removed:** none of the 19. Not claimed.
- **Duplicate temporal residual work remains (→ A3):** not established. The
  11-target reduction is consistent with removed Boolean redundancy, not
  with repeated *identical* `(T, P)` residual keys specifically — I did not
  add per-key duplicate-arrival instrumentation to `expand()`'s worklist to
  check that directly (a materially different, finer-grained counter than
  anything A1 already exposes), and the plan's A3 gate requires that direct
  evidence, not branch-count correlation. Per §6's stopping rule ("if exact
  keys rarely repeat, stop: syntactic similarity is not evidence for this
  cache"), **A3 is not attempted this sprint.**
- **Many genuinely different continuations / another barrier:** this is
  where all 19 non-completing targets land — 11 with a measured but
  insufficient Boolean-redundancy reduction, 9 with essentially none,
  including three hitting a state/memory ceiling unrelated to Boolean
  folding at all.

**Consequence for A4.** §5.3 and §4.1 are explicit: completing a previously
failing *useful query* justifies continuing the research experiment; A1
still requires an actual useful provider solve/runtime/memory gain before
any deployment step, and synthetic-only improvement must be published
honestly rather than chased into a full corpus run. No P2 target produces a
newly useful query here, so **A4's end-to-end provider experiment and
deployment-candidate evaluation do not proceed this sprint** — there is
nothing for them to measure. The closure-buchi provider's disposition is
unchanged from the previous sprint (blocked by first-row explosion on this
cohort, not integrated); A1 is a validated, tested mechanism improvement to
that same blocked research provider, retained on this sprint's branch as
research evidence, not shipped.

The B track (§8, selective sparse-real dispatch) is independent of this
result and proceeds next.
