# Broad re-evaluation and verified witness-lifting sprint — running decisions log

Sprint handoff: `581dfb32-acacia_broad_campaign_verified_witness_lifting_sprint_2026-09-18.md`.
This log is updated as work packages complete; it is not a final closing report (`closing/README.md`
does not exist yet — W1's campaign is still running).

## W0 — freeze (complete)

- Inspected tree matches the handoff's assumptions: `docker_default` group, selector code,
  prior evidence directories (`symbolic-rows-20260917/`, `demand-sparse-20260916/`) all present
  and read. Prior sprint's own conclusion (PR #189, merged): nothing admitted, candidate=baseline.
- Built B (`build_w1_B`, selector off) and S (`build_w1_S`, selector on, same threshold) as proper
  release/LTO measurement binaries from HEAD `50384cf6`. G0 clean on both (50/50 unit tests each).
  G1: 25/40 sentinels clean (0 verdict changes, 0 coverage losses) on both; 15/40 blocked by a
  pre-existing corpus-materialization gap unrelated to this build (documented in `manifest.json`'s
  `g1_corpus_gap`, not silently worked around — the demand-sparse sprint's own "G1 GATE PASS 40/40"
  used a corpus directory that no longer exists anywhere on this machine).
- H (clean scheduling-hint recovery): not attempted. Concrete blocker recorded in `manifest.json`
  (P1a was only ever evaluated bundled with P0 in the demand-sparse sprint; no clean P0/P3a-free
  port onto current master exists yet). Per plan section 3.1, started with B and S alone.
- Materialized the flat TLSF corpus (`python3 benchmarking/syntcomp-corpus.py materialize`,
  1586/1586 verified) and cut 24 deterministic shards from the real 1,524-instance
  `syntcomp26/all.list`.

## W1 — broad campaign (opening report complete)

Launched `benchmarking/witness-lifting-20260918/commands.sh` detached (setsid+nohup) at
2026-09-18T10:58 local. Sequence: 120s cap (epoch 1, epoch 2) → ltlsynt@120s → 17s cap
(epoch 1, epoch 2) → ltlsynt@17s, B/S alternating per-shard (even shards: epoch-base order,
odd: reversed; epoch 2 starts from the reversed epoch-base order) so within-epoch host drift
cannot systematically favor one candidate. Completed cleanly: `W1 OPENING CAMPAIGN COMPLETE` at
2026-09-20T20:29:16, ~57.5h wall time, all 8 Acacia series and both ltlsynt series at 1524/1524
rows, zero verdict conflicts anywhere.

**Host-sharing rule adopted this session, beyond what the handoff states explicitly:** no
concurrent solver-adjacent work (builds, or even brief verification-pipeline runs) once it stops
being brief — see W3/W4 below for where this actually bit.

**A real bug surfaced and was fixed before the opening report could be trusted:** `write_summary()`
(in the wrapped coverage runner) rebuilds the `-summary.tsv` sidecar from only the *current
invocation's* own `--list`, not the cumulative campaign — since `run-witness-sprint.py`'s `campaign`
calls it once per shard against a shared `--output`, every series' summary ended up reflecting only
the last shard processed (62 rows, not 1524). Fixed (post-shard-loop regeneration from the complete
raw file and the full shard-instance union) and the 8 already-committed series repaired with a new
`regenerate-summaries.py`, reusing the coverage runner's own `load_output()`/`write_summary()`
rather than reimplementing summary logic. Delegated to codex (gpt-5.6-sol, xhigh) with an
independent codex review pass that spot-checked regenerated content against raw rows by hand before
approving (after one legitimate REQUEST CHANGES round on test coverage, addressed). Raw `.tsv` files
were never touched — only the derived sidecar.

### Opening report (plan section 4.4)

Built from the corrected data with existing tools only (`export-cactus`, `cactus-report.py`, both
unmodified) — no new PAR-2 implementation. Full tables/plots:
`opening/{120s,17s}/epoch-{1,2}/three-way-par2.md` (+ PNG/PDF). Epoch 1 is the preregistered
primary; epoch 2 is shown for repeatability, not as a second primary — the two agree closely
(PAR-2 totals differ by under 22s at 17s cap, under 155s at 120s cap, well inside noise).

**Headline finding: S beats B on both PAR-2 and coverage, at both caps, consistently across both
epochs.** Arithmetic mean of the two epoch totals:

| Cap | B mean PAR-2 | S mean PAR-2 | B solved | S solved |
|---|---:|---:|---:|---:|
| 17s | 12673.9 s | 12554.0 s (−0.95%) | 1173–1174 | 1177–1178 (net +4/+5) |
| 120s | 75702.6 s | 74245.2 s (−1.93%) | 1221–1222 | 1228 (net +6/+7) |

**This is discovery-epoch evidence, not confirmation-grade** — plan section 5 requires five fresh
alternating pairs before any admission claim, and none have been run yet. No admission decision is
made here. But it is real, broad, full-corpus support for the "research continuation" disposition at
minimum (plan section 1.2's three-way distinction), which the prior sprint's 10-instance P4 screen
could not by itself establish.

**The prior sprint's documented near-cap regression on `workstation_resupply_pb_3_pe_` does not
reproduce as a loss anywhere in this full-corpus data.** At 120s cap it is REALIZABLE on both B and
S in both epochs (17.27/16.78s and 17.69/17.40s). At 17s cap it flips inconsistently across epochs —
S wins epoch-1 (16.7s vs B TIMEOUT), both TIMEOUT in epoch-2 — rather than the clean B-wins/S-loses
pattern the earlier 10-instance screen showed every time. This is consistent with the prior sprint's
own attribution (LTO code-placement noise on a genuine ~17s knife-edge instance, `[[lto_code_placement_noise]]`),
not a re-confirmed selector-decision error; it does not by itself clear this instance for admission
(near-cap losses stay unresolved until adjudicated, not waived by absence in one campaign), but it
substantially weakens the case that this is a systematic S regression rather than host/build noise.

`opening/gains-losses.py` (codex-authored, independently reviewed — APPROVE) reuses the exact
categorization from the prior sprint's `closing-tables.py` (gain/loss/verdict-conflict/unsolved-
kind-change/faster/slower, same 5%-or-50ms threshold) across all 4 legs. Flagged-row counts stay
small relative to corpus size (120s: 130/1524 and 128/1524; 17s: 54/1524 and 57/1524), zero
verdict-conflicts anywhere — consistent with S being a narrow, well-targeted structural change
rather than a broad behavioral shift.

### W1 §4.3 — closure-buchi-provider mechanism audit, job selection only (step 1 of 5 complete)

Plan section 4.3 explicitly caps this at "one broader mechanism audit, not four automatic full
solver campaigns," and step 1 is pure data selection, freezable and reviewable before any provider
execution: `opening/select-provider-audit-jobs.py`, output frozen at `opening/provider-audit-jobs.tsv`.

Reused the existing 22 P2 targets (`symbolic-rows-20260917/targets/p2.list`, untouched) and added
**64 new jobs from 39 instances across 27 previously-untouched, exact-parametric families** —
every family already present in P2 or P4 is excluded by construction, not just by convention.
"Previously unsolved" is read from the demand-sparse sprint's frozen 17s baseline
(`acacia-baseline.csv`, explicitly labelled as that stale source, not fresh W1 data — the actual W1
B/S rows aren't complete enough yet to redo this against). Within the 64-job budget, filled one
parameter point (smallest and largest available unsolved value, for a size spread) for every
eligible family *before* adding a second point to any family — 12 families got both points, 15 got
one, because the orientation rule below inflated the job count past what 27×2 instances would need.

**Orientation** (plan section 4.3 item 2 — "prefer the trusted orientation... never guess from the
filename"): read each instance's own TLSF `STATUS` line via `expected_verdict()`, the exact same
mechanism `run-syntcomp26-coverage.py` itself uses for conflict detection — not a new heuristic.
Only 14/64 jobs (11 REALIZABLE, 3 UNREALIZABLE) had a declared status; the other 50 got **both**
orientations as separately identified jobs, exactly as instructed, which is why 64 jobs cover only
39 distinct instances.

**Steps 2-5 of §4.3 (actually replaying these jobs through the closure-buchi provider — first-row
budgets, enumerative-vs-symbolic comparison, terminal-pair/merge/prune counters) need the
`build_check`-style checked/debugoptimized `acacia-spot-provider-replay` diagnostic binary and real
solver time against 64 jobs.** Not run this session: deliberately kept off the shared host while
the W1 campaign's own measurements are the priority, per the host-sharing rule above. This is a
clean, reviewable handoff point — the selection is frozen and does not need redoing once a quiet
window opens.

A `build_check` directory already exists (gitignored, right options — `debugoptimized`, `b_lto`
off, `build_research_tools`/`acacia_enable_tlsf_frontend` on, matching the prior sprint's documented
recipe for this exact tool) with a compiled `acacia-spot-provider-replay`. **It is stale**: embedded
version `31144e8-dirty`, several commits behind current HEAD (`d57f3afb`). Checked precisely how
stale rather than assuming: `git diff --stat 31144e84 HEAD -- src/solver/closure_buchi_provider.cc
src/solver/closure_buchi_provider.hh src/research/spot_provider_replay.cc` is empty — the provider
and its replay tool have not changed at all. The only `src/` delta since then is B2's selector
(`real_backend_selector.{cc,hh}`, +51 lines in `solver_invoker.cc`, all gated behind
`acacia_real_backend_selector`, default false), plus registry/build-file wiring. Given `build_check`
has LTO off, the code-placement sensitivity `selector.md` documents for `-Ofast -march=native`
release builds specifically is unlikely to apply here. Net: this stale binary is probably still
valid for a *mechanism/correctness* diagnostic (not a timing claim), but a fresh rebuild is cheap
and removes the doubt entirely — do that first, once a quiet window opens, rather than debate reuse.

## W2 — attribution (not started)

Blocked on W1 producing paired rows for the previously-problematic workstation instance and other
exposed cases. Nothing to attribute yet.

## W3 — family provenance (partial)

Regenerated `benchmarking/syntcomp26-family-instances.tsv` fresh this session (325 families, 57
formally orderable, 734/1524 exact-provenance). Selected three pilot families from actually reading
their templates (plan section 6.1), none overlapping the prior sprint's `targets/p2.list` or
`targets/p4.list`:

1. **arbiter** (`arbiters_zoo/parametric/arbiter.tlsf`) — indexed/replicated REAL. Full pipeline run.
2. **round_robin_arbiter** — bounded-counter REAL (the counter is in the expected witness structure,
   not the TLSF text, which states no explicit counter). Seeds/sanity done; target inconclusive
   (see W4).
3. **round_robin_arbiter_unreal2** — small-support UNREAL obstruction (one added pairwise poison
   clause, `i<j<n`, over the same base family as #2). Selected and justified; seed acquisition not
   started. Confirmed by reading the actual code (not just citing the handoff's caution) that `-s`
   never exports a controller for an unreal-arm win — `arg_parser.hh` keeps all unreal arms but
   drops extra real ones when `-s` is given, and `solver_invoker.cc:715` asserts `synth_fname` and
   `check_unreal` are mutually exclusive on the main solve path. **Also checked ltlsynt** (already
   installed on this host): `--aiger --verify` on a tiny confirmed-UNREALIZABLE seed prints only
   `UNREALIZABLE` and exits 1 — no AIG at all when the environment wins. Not a usable exporter
   either.

   Traced Acacia's own internal UNREAL_X_FORMULA transformation precisely (`solver_invoker.cc`
   ~1090 and ~421): swap `input_aps`/`output_aps`, X-shift every atom that was an *original* output,
   do **not** negate the objective. Before trusting a TLSF-level reconstruction of this,
   **actually ran it** (no build needed — this uses Acacia's raw `-f/-i/-o` CLI on tiny hand-written
   formulas, not TLSF, on the already-compiled `build_otf_sparse_formula` binary) against a
   hand-picked tiny unrealizable game: `G(o <-> X(i))` with `INPUTS{i} OUTPUTS{o}`. Confirmed
   `UNREALIZABLE` via Acacia's own `-u formula` check first (independent sanity check). Its only
   sensible environment strategy is the obviously-causal `i(t+1) := not(o(t))` — the environment,
   seeing the system's move `o(t)`, immediately picks the next input to violate `o(t) <-> i(t+1)`.

   Dualized per the traced recipe: `INPUTS{o} OUTPUTS{i}`, formula `G(X(o) <-> X(i))`, no negation.
   `acacia-bonsai -f "G(X(o) <-> X(i))" -i o -o i` reports **REALIZABLE**, and `-s` produces a
   3-gate, 1-latch AIGER circuit -- small enough to read directly rather than trust blindly: the
   latch resets to 0 and its next-state is the constant 1, so in the swapped game's own time index
   τ the circuit is `i(0)=0` (a don't-care -- the formula never constrains it) and, for τ>=1,
   `i(τ) := o(τ)` (immediate same-round copy). That's internally consistent for the *swapped* game
   (Mealy: new-input `o` before new-output `i`, same τ) but says nothing yet about the *original*
   game, where the causal order is reversed (`i` chosen before `o`, same round) -- reading the
   circuit's τ literally as the original round index would require the environment to see `o(t)`
   before choosing `i(t)`, which is acausal in the original game and exactly the "future
   information" plan section 7.1 forbids.

   Tried the most natural fix -- reinterpret the circuit's output at swapped-round τ as the
   environment's move at *original* round τ+1, i.e. `i(t+1) := o(t)` -- and checked what that
   actually does to the original objective: substituting into `G(o <-> X(i))` gives
   `o(t) <-> i(t+1) = o(t) <-> o(t)`, which is a **tautology** -- this reinterpretation makes the
   "environment" cooperate with the system's original objective instead of defeating it, exactly
   backwards from an UNREAL witness. **This is a decisive, empirically-confirmed near-miss, not a
   hypothetical caution**: the formula-level swap+shift alone is not sufficient, the most obvious
   one-step reinterpretation gets the polarity wrong rather than merely being imprecise, and this
   session could not derive the correct reinterpretation with confidence, so no adapter was built.

   The safer path identified instead: extend Acacia itself — remove the `solver_invoker.cc:715`
   assert and route `synth_fname` through for the unreal-formula worker, whose `synthesis()` method
   is already dualization-aware internally (it operates on `strats`/`out_part` generically,
   independent of `check_unreal`) — rather than reconstruct the dualization externally in TLSF text.
   This needs a C++ change and a rebuild, appropriately deferred until the campaign reaches a quiet
   window (plan section 14: no builds while primary measurements execute). Deferred deliberately.
   See `families/selected.json` for the full account.

See `families/selected.json` for full per-family detail including each `valid_parameter_predicate`
(in particular: `round_robin_arbiter_unreal2` at n=1 would make the poison clause a vacuous empty
conjunction — n>=2 is the real bound, not the bare n>=1 a naive read of the template would suggest).

## W4 — exact target verification (core result reached for one family)

**arbiter, n=10 (`arbiter_pb_10_pe_.ltl`): VERIFIED.** This is the sprint's stated bar for pilot
success ("at least one feasibility pilot reaches exact target verification").

Pipeline: tlsf-tools' `tlsf2tlsf --param n=K --basic` on the preserved original template → Acacia
native-TLSF synthesis (`-T ... -s ...`) for seeds n=2,3 and sanity n=4, all REALIZABLE and VERIFIED
via tlsf-tools' `verify_aiger_ltl.py` (Spot 2.15.1.dev). Direct synthesis of the actual n=10 target
timed out at 60s on the same solver — a genuine miss, not a manufactured one.

A manual schema (`proposal_origin=manual`, `families/proposals/build_arbiter_witness.py`) was
written: n pending-request latches (request survives being dropped before service, since the spec
carries no assumption that a request stays raised) plus a one-hot rotating pointer (the "bounded
cyclic increment" primitive from plan section 8.1). It agrees with the seeds' own verdicts (an
independent construction reaching the same checked verdict, not a decompilation of the seed AIGER).
Instantiated at n=10: monolithic single-formula checking did not complete in 90s. Splitting into the
formula's 41 top-level conjuncts (plan section 7.2's exact-conjunction rule) verified all 41 in
5.06s total — confirming the n=10 bottleneck was Spot translating one large conjoined formula, not
genuine product-state exploration, so W6's candidate-restricted product construction was not needed
for this target.

**A real Spot bug/limitation surfaced along the way and is documented, not hidden:** a
binary-encoded (log2 n bit) pointer with an explicit ripple-carry incrementer reliably crashed the
pinned build's `spot.aiger_circuit` parser (`aiger.cc:354`, `register_new_lit_` assertion) for every
n with a 2+ bit pointer, reproduced with the generator's own AND-gate sharing disabled too (so it is
not a bug in this session's code, and not the proposer's job to work around beyond routing past it).
Switching to a one-hot pointer sidestepped it entirely. See the docstring of
`build_arbiter_witness.py` for the full account. Not filed upstream this session.

**round_robin_arbiter, n=10: root cause now precisely identified -- exponential blowup in the**
**ASSUME antecedent's own automaton, not host contention.** Seeds/sanity verified with the same
schema unmodified. The target check needed the conjunct-decomposer extended to split an
implication's consequent while retaining the full antecedent (`A -> (G1 and G2)` → `(A->G1) and
(A->G2)`, exactly plan section 7.2's rule — implemented, and does not change the already-verified
arbiter or round_robin_arbiter seed/sanity results, which use the same code path).

An early hypothesis (recorded in a prior version of this note) attributed the n=10 timeout to
memory pressure from the concurrently-running W1 campaign. **That hypothesis is superseded**: a
retry during a measurably lighter-load moment (~750 MiB RSS per campaign arm vs ~1.1 GiB earlier,
9.6 vs 8.9 GiB available) still timed out at 60s. Bisecting further, translating the shared
antecedent `A` (the 10-way-ANDed `ASSUME` block, each conjunct containing a weak-fairness term
`F!(r_i && g_i)`) *alone*, with nothing else in the pipeline running, does not complete in 90s
either -- ruling out contention as the primary cause. Measuring `A`'s automaton size across n
confirms a clean exponential trend: n=5 → 0.828 s / 243 states, n=6 → 7.026 s / 729 states
(both exactly 3^n), n=7 did not complete in the remaining budget. This is a genuine, structural
translation cost: n independent per-client weak-fairness conjuncts on disjoint atomic propositions
force a 3^n-state product in Spot's construction (each client's own fairness tracker needs its own
independent memory), not an artifact of decomposition, host load, or this session's code. Tried
`spot.translate` with `"Buchi"`/`"BA"`/`"generic"`/`"low"`/`"deterministic"` acceptance-type
options looking for a cheaper construction; none completed within a 30 s combined budget at n=7
either. Not pursued further -- this is exactly the "eager/unrestricted checking is the obstacle"
condition plan section 7.3 describes as the trigger for W6 (candidate-restricted product
construction), which builds the product against the *candidate-restricted* letter region instead
of eagerly expanding the full antecedent automaton. Not implemented this session (W6 is a
substantial, separately-justified subsystem); the exact reproducer (n and the antecedent formula)
is preserved in `families/target-checks.tsv` for whoever picks this up.

## W5–W7 — not started

W5 (bounded automatic proposer), W6 (candidate-restricted product construction), W7 (cold-cost
runner) are unstarted. W6 in particular is not currently motivated by any collected evidence: the
one target-verification obstacle actually hit (arbiter's n=10 monolithic-formula cost) was resolved
by conjunct decomposition alone, per plan section 7.3's own instruction not to reach for W6 until
eager checking is shown to be the real obstacle on its own terms.
