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

## W2 — attribution (confirmation campaign complete: REJECT at both caps)

Confirmation queue frozen (`opening/confirmations-queue.tsv`, 315 rows: 196 at 120s, 119 at 17s).
Runner built, reviewed, launched detached at 2026-09-20T23:32:44, completed 2026-09-21T18:43:14
(120s: 18.2h; 17s: 1.24h -- much faster than the initial extrapolation, since the 17s queue's
instances are mostly ones the discovery data already knew resolve quickly one way or the other).
Zero conflicts across all 10 rounds at both caps.

**Admission evaluation (plan section 8), run for real via `paired-admission.py` with a properly
constructed `--benefit-targets` list** (the 5 P4 gain targets plus any instance the discovery data
flagged `gain` -- explicitly excluding `workstation_resupply_pb_3_pe_` and other near-cap-control/
memory-only/timing-only queue entries from benefit credit, since those are risk checks, not declared
benefit claims):

**REJECT at both caps** (correctness PASS, no_regression FAIL, improvement FAIL at 120s and 17s).

This is a real, validated result, not a repeat of the single known workstation knife-edge:

- **120s: a systematic TIMEOUT-to-MEMOUT transition under S**, confirmed across ~44+ instances
  spanning multiple families -- `infinite-race-u15..25`, `lift_pb_{4,5,6}_pe_`,
  `lift_unary_enc_pb_{4,5,6}_pe_`, `load_balancer_unreal1_pb_4_{10,11,12}_pe_`,
  `round_robin_arbiter_unreal1_pb_{3,4}_*`, `arbiter_on_inpchange_pb_{5,6,7}_pe_`,
  `robot_grid_pb_6_6_pe_`, `robot-to-target0/14`, `reversible-lane-r-real`, `square5x5-real`.
  **Cross-checked against the opening campaign's own discovery data before trusting it**: the
  identical TIMEOUT->MEMOUT pattern already appears in `gains-losses.tsv`, 88 rows across both
  discovery epochs (44 each) -- this confirmation independently reconfirms it with 5 fresh rounds,
  it is not new noise introduced by this run. **PAR-2 scores TIMEOUT and MEMOUT identically** (both
  pay `2*cap`), so this cost was completely invisible in W1's headline PAR-2 numbers despite being a
  real, different, and arguably worse failure mode -- hitting the 8 GiB memory ceiling rather than
  gracefully exhausting the time budget. This is exactly the kind of loss the plan's PAR-2-alone
  framing can hide and paired confirmation is designed to surface.

- **17s: `workstation_resupply_pb_3_pe_` is still genuinely mixed/unresolved under 5 fresh rounds**
  ("mixed paired verdict losses"). The full-corpus discovery data's single epoch-1 win does not
  generalize under fresh repetition -- this confirms near-cap noise on this instance, it does not
  clear it. Also surfaced a second, previously-unflagged near-cap knife-edge instance,
  `GF-G-contradiction7.ltl`. Two clean, unambiguous coverage regressions: `lift_pb_4_pe_.ltl` and
  `lift_unary_enc_pb_4_pe_.ltl` go from 5/5 solved under B to 0/5 under S.

**Disposition (plan section 5.3, "broad benefit plus losses"): S is not admitted at either cap.**
W1's broad PAR-2/coverage benefit stands as real and worth continuing research on -- this
confirmation does not erase it -- but the losses are now concrete and validated rather than one
disputed instance on a 10-target screen. The natural next step, not undertaken here, is diagnosing
*why* S's backend choice produces a memory blowup on this specific cluster of instances instead of
degrading gracefully to a timeout the way B does -- that diagnostic is the "at most one
mechanism-supported repair/routing refinement" the plan allows before reconsidering admission.

`binary-attribution.md` (the separate question of whether B_arch vs current B shows build drift on
the workstation instance specifically) was not produced -- superseded by the more informative
finding above; the workstation instance's own behavior is already fully characterized by the mixed
5-round confirmation result, and chasing a separate binary-drift explanation for it adds nothing
once the pattern generalizes to a whole cluster of other instances at 120s.

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

## Post-campaign follow-on: UNREAL-adapter build attempt (blocked, two negative results)

W3 (above) identified the safer in-repo path for `round_robin_arbiter_unreal2` — extend Acacia's
own frozen-graph solve path to accept `-u formula -s FILE` together, since `run_one_ltl::synthesis()`
already operates generically on `strats`/`out_part` independent of `check_unreal` — rather than
reconstruct the dualization externally, given the demonstrated real risk of getting the polarity
wrong. This note records the successful attempt (via `codex-task`, ten rounds plus one independent
review) to actually build that
path, on the tiny hand-verified game `G(o <-> X(i))` (`INPUTS{i} OUTPUTS{o}`, confirmed UNREALIZABLE
via `-u formula`) before touching `round_robin_arbiter_unreal2`'s real seeds. Every round stopped on
a clean result rather than proceeding on an ambiguous one, and reverted its own source edits; no
unsound witness was produced or accepted at any point, and `round_robin_arbiter_unreal2`'s real
seeds were never reached. The rounds are recorded in order below because each one's negative result
is what located the next gate.

**Round 1**: relaxed the single assert this note originally named
(`solver_invoker.cc`, `assert (not synth_fname.has_value () or not check_unreal.has_value ());`,
main frozen_graph solve path) to also permit `*check_unreal == UNREAL_X_FORMULA`, in a fresh
`build_unreal_test` (debugoptimized, assertions enabled, isolated from `build_w1_B`/`build_w1_S`).
`-f 'G(o <-> X(i))' -i i -o o -u formula -s /tmp/unreal_test.aag` completed cleanly (`UNREALIZABLE`,
exit 1, no abort) but produced no AAG file. Root cause found by reading the surrounding code:
`try_unreal_safety_core_witnesses` — a decision-only fast path invoked after the I/O swap, distinct
from `try_degenerate_io`/`try_syntactic_bypass` — answers small unreal checks via a "safety core
witness" search and returns immediately on a match, without ever populating `strats` or knowing
`synth_fname` exists (its signature doesn't even take it). Its two siblings both already guard their
own shortcuts with `if (not synth_fname.has_value ())` for exactly this reason; this one was missing
that guard. This tiny formula was being answered entirely by that shortcut.

**Round 2**: added the missing guard (`try_unreal_safety_core_witnesses` now takes `synth_fname` and
returns `std::nullopt` immediately when it is set, falling through to the full runner path, matching
its siblings byte-for-byte for the unset case), on top of the same assert relaxation, in a fresh
rebuild. Same command still printed `UNREALIZABLE`/exit 1 correctly, but **still produced no AAG
file — and, notably, `run_one_ltl::synthesis()`'s own `assert (strats.size () > 0);` did not fire
either**, meaning `synthesis()` was never entered at all (not entered-with-empty-strats, which the
assert would have caught). Since `try_degenerate_io`, `try_syntactic_bypass`, and now
`try_unreal_safety_core_witnesses` are all confirmed not taken for this input (synth_fname set,
input/output APs both non-empty), some further gate or early return between there and the
`DECOMPOSE_SPEC`-dispatched `if (runner (...)) { ... runner.synthesis (...); }` call is still
intercepting the check_unreal+synth_fname combination for this trivial formula. Not yet identified;
would need direct tracing inside `run_one_ltl::operator ()` itself (not just its call sites in
`run_ltl`/`solve_decomposed`) to find it.

**Round 3** found and fixed a third gate by reading the rest of `run_one_ltl::operator ()`:
`const bool want_controller_strategy = synth_fname.has_value () and not check_unreal.has_value ();`
starves the *Spot NBA fast path*'s own strategy extraction whenever `check_unreal` is set, separate
from `solve_game`'s strategy population further down (which was already correctly generic on
`synth_fname` alone). Fixed to also allow `UNREAL_X_FORMULA`. Round 3's own validation run, however,
used a corrupted formula string (`G(o <-> Xi)`, where `Xi` parses as one bareword atomic proposition,
not `X` applied to `i`) — a test-construction bug, confirmed by its own trace (`Xi` appears verbatim,
unshifted, through every stage) — so round 3's negative result did not actually test the fix.

**Round 4** re-ran the identical three-gate fix with the corrected formula (`G(o <-> X(i))`, verified
byte-exact and confirmed by its own trace to parse with a genuine unary `X` applied to `i`) and got
the same negative result: exit 1, `UNREALIZABLE` printed correctly, no AAG file. The verbose trace
shows the classification path directly: `Spot NBA fast path classification: deterministic` →
`Spot NBA fast path returning 1`, i.e. `deterministic_forbidden_fast_path`
(`src/solver/spot_nba_fastpath.hh`) resolved it, exactly the code this round's fix targeted. Reading
that function's own logic (`if (p_out_wins and want_strategy) res.strategy = ...`), it should have
populated a strategy given `p_out_wins=true` (matching the printed `1`) and `want_strategy=true`
(post-fix) — and `run_one_ltl::synthesis ()`'s own `assert (strats.size () > 0);` should fire if it
were entered with an empty `strats`, since this build's assertions are confirmed genuinely active
(`meson.build`'s `-DNDEBUG`/`-DNO_VERBOSE` only attach under the `release`/`lowmem`
`acacia_compiler_profile`, and this build's own verbose tracing printed successfully, ruling that
out). Neither the file nor the assert appeared. This is a real, currently unresolved contradiction
between a plain reading of the code and its observed behavior, not explainable by any test-harness
issue found so far — settling it needs actual print-based instrumentation (e.g. at the top of
`synthesis ()` and at its call site) rather than further code reading, a meaningfully different
(and more open-ended) level of effort than the three targeted one-line/few-line gate fixes tried so
far.

Also worth recording precisely, since it cost real time this round: shelling out to `git apply
<patchfile>` inside the `codex-task` sandbox hung indefinitely (its git-mutation PATH shim appears
to drop the patch-file argument, leaving `git apply` blocked reading an empty stdin) — worked around
by having codex make the same edits directly with its own file-editing tool instead, which behaved
normally. Worth remembering for any future round that considers using `git apply` inside this
sandbox.

**Round 5 (instrumentation) resolved the contradiction and found the actual root cause.** Temporary
`[[INSTR]]` prints at `synthesis ()`'s entry, its two dispatch call sites, and inside the fast-path
block (all reverted afterwards) produced, on the same tiny game with all three gate fixes applied:

```
[[INSTR]] fastpath conclusive wants_strategy=0 has_strategy=0 wins=1 strats_after=0
[[INSTR]] dispatch branch=1 runner_result=1 synth_fname_set=0
```

`synth_fname_set=0` — inside the unreal child, `synth_fname` is **empty**, despite `-s FILE` being
given on the command line. `synthesis ()` is therefore never called (no `synthesis() ENTERED` line,
no "about to call synthesis()" line), which is why no file appeared and why its
`assert (strats.size () > 0);` never fired. `want_controller_strategy` computes to 0 for the same
reason, not because of the `check_unreal` term that round 3 fixed.

The root cause is one line in `src/acacia-bonsai.cc` (~line 112), in the `start_proc` lambda's call
into `run_ltl`:

```cpp
unreal_x.has_value () ? std::nullopt : arg_values.synth_fname,
```

The `-s` filename is explicitly discarded for **every** unreal child, before any of the
`solver_invoker.cc` logic runs. This is the first gate in the chain, and it makes the other three
unreachable rather than wrong: the assert (gate 1) could never trip because `synth_fname` was always
empty there; `try_unreal_safety_core_witnesses`'s missing `synth_fname` guard (gate 2) was a genuine
inconsistency with its two siblings but never observable; and `want_controller_strategy`'s
`check_unreal` term (gate 3) was masked by `synth_fname.has_value ()` already being false. All three
remain real prerequisites — they just sit downstream of the one that actually blocks everything.

A complete fix is therefore four coordinated changes: pass `arg_values.synth_fname` through for
`UNREAL_X_FORMULA` children at `acacia-bonsai.cc:112`, plus the three already-written
`solver_invoker.cc` changes. Note `run_ltl` already forces the synthesis backend/provider itself
when `synth_fname` is set (`backend = acacia::synthesis_backend (backend, true);`), independently of
`check_unreal`, so the unreal child would pick that up without further changes; the separate
backend-forcing in `acacia-bonsai.cc` (~line 142, guarded by `not arm.unreal`) is a duplicate of
that and may not need touching.

**Round 6 landed the four-part fix and got past the block** — `synthesis ()` is genuinely reached
for the unreal child for the first time — and failed with one precise, diagnostic error:

```
Exception caught: Atomic propositions appears in input and output propositions: o
```

(exit 3, no file). The real-side regression check in the same round passed: `G(i -> X(o))` still
prints REALIZABLE, exits 0, and writes a correct 31-byte AAG with symbol table `i0 i / o0 o`, so the
shared code path is not disturbed.

The cause is a **fifth gate**, visible directly in the verbose trace (`with output set: o`). In the
dualized frame the runner holds `input_aps={o}` and `output_aps={i}`, so the dual game's controllable
AP is `i`. But `solve_decomposed ()` builds its partition from

```cpp
spot::split_independent_formulas (spot_formula, check_unreal.has_value () ? input_aps : output_aps);
```

— splitting over `input_aps` ({o}) for the unreal case. That is the right choice for decomposition
bookkeeping, but the resulting `out_part` is then handed unchanged to `runner.synthesis ()`, where it
is read as the controllable partition. Synthesis therefore sees inputs `{o}` and outputs `{{o}}`, and
Spot rejects the overlap. The `DECOMPOSE_SPEC == 0` monolithic branch does not have this bug — it uses
`out_part = {output_aps}`, correctly `{{i}}` — but this build compiles `DECOMPOSE_SPEC == 1` (round
5's instrumentation printed `dispatch branch=1`), so `solve_decomposed` is the live path.

**Round 7 completed the mechanical path and hit the genuine conceptual question.** With the fifth
gate fixed (decline decomposition for the unreal+synthesis case and use `out_part = {output_aps}`,
mirroring the already-correct monolithic branch), synthesis ran to completion for the first time: it
solved the dual game, built a Mealy machine, and printed it —

```
HOA: v1
States: 3
AP: 2 "o" "i"
controllable-AP: 1
--BODY--
State: 0
[!1] 1
State: 1
[!0&1 | 0&!1] 2
State: 2
[!1] 2
--END--
```

— i.e. a 3-state machine over `{o, i}` whose **controllable AP is `i`**, which is the correct
polarity for an environment strategy (it reads `o`, it drives `i`). The real-side regression in the
same round still passed unchanged.

It then aborted inside `run_one_ltl::synthesis ()`'s own `#ifndef NDEBUG` model check:

```
Model checking result by checking intersection with !G(o <-> Xi)
solver_invoker.cc:407: Assertion `not aut->intersects (mealy_aig->as_automaton (false))' failed.
```

(exit 2, `UNKNOWN`, and a 0-byte file — the `ofstream` truncates before `print_aiger` runs, so the
abort leaves an empty artifact rather than a bad one.)

**This assert firing is the correct and expected behaviour, not a new bug, and it is the safety net
working.** That check encodes the *real-side* soundness criterion: for a controller `C` and spec
`phi`, require `L(C) ⊆ L(phi)`, i.e. `L(C) ∩ L(!phi) = ∅` — exactly what line 407 asserts. An
environment witness has the opposite obligation: every play consistent with it must *violate* the
spec, i.e. `L(E) ⊆ L(!phi)`, i.e. `L(E) ∩ L(phi) = ∅`. So the existing check asks precisely the
wrong question of a dualized strategy, and a dualized strategy that *passed* it would be the broken
one.

Two candidate readings of the failure remain, and they are not distinguishable without care:
(a) the criterion is simply inverted, and the witness is fine; or
(b) there is a frame mismatch — the machine lives in the X-shifted dual frame while the check
    compares it against the *un-shifted* original `G(o <-> X(i))` (the trace confirms it negated the
    original formula, not the transformed `G(Xi <-> Xo)`), so the comparison is not apples-to-apples
    in either direction.

Deciding between them, and fixing the check correctly, is exactly the conceptual question this
sprint deliberately refused to answer by guesswork — see the W3 near-miss above, where the obvious
reinterpretation of a dualized circuit produced a witness of the *wrong polarity* that looked
entirely plausible. Making an assert stop firing is trivially achievable and proves nothing; the
work here is to establish the correct soundness criterion *and frame* for a dualized witness, then
implement that. **Not attempted this session** — deliberately, and this is the right stopping point
rather than a failure to push further.

**What is now settled**: the entire mechanical path from `-u formula -s FILE` through to a produced
strategy is solved and understood — five gates, each precisely located and individually verified:
`acacia-bonsai.cc`'s discarded `synth_fname`, the `solver_invoker.cc` assert, the
`try_unreal_safety_core_witnesses` bypass, `want_controller_strategy`, and `solve_decomposed`'s
dual-frame partition. The strategy it produces has the right controllable AP. What remains is one
well-posed question (the verification criterion and frame), not a search.

**The owner supplied the resolution directly** (2026-09-22): the criterion is a polarity flip, not a
frame change — do not touch how `aut` (`= L(!phi)`, `phi` in the original, untransformed frame) is
built; just require *nonempty* intersection instead of empty, since "the strategy of the adversary
witnesses the nonempty intersection, that's it." The owner also flagged, as a separate, unresolved
concern to verify empirically rather than assume: "the Mealy/Moore duality of the strategies for the
adversary with respect to the semantics of the specification" — i.e. whether the produced machine's
output at its own cycle `t` corresponds to the original game's environment move at round `t` or at
round `t+1`, given Acacia's own internal "Mealy-to-Moore" transform on the worker formula (visible in
the verbose trace) already does *something* to reconcile the dual solver's timing convention with the
original one, and it is not safe to assume without checking which.

**Round 8** applied the criterion flip literally as specified —
`assert (not aut->intersects (...))` → `assert (aut->intersects (...))`, nothing else touched — and
correctly ran the real-side regression *first*, per the discipline established every round. That
regression (`G(i -> X(o))`, REALIZABLE) **failed**: the same assert on line ~407 fired for the real
child too, because `run_one_ltl::synthesis ()` is one function shared by both real and unreal
children, and the check at that line was unconditional. The owner's instruction was correct for the
adversary case specifically; the brief that translated it into a literal, unconditional code edit
was the gap — a planning error on this session's part, not a wrong instruction. Round 8 caught it
exactly as designed (a well-evidenced negative result, not a wasted round) and reverted cleanly
before touching the unreal case or the timing question at all.

**Round 9 applied the corrected, `check_unreal`-conditional criterion** —
`assert (check_unreal.has_value () ? aut->intersects (...) : not aut->intersects (...))` — and it
worked cleanly through every check: the real-side regression still passes unchanged, and the unreal
case on the tiny game produced a genuine AAG. Its symbol table declares `i0 o / o0 i` — the correct
environment-strategy polarity.

**The circuit was independently re-derived by hand from the raw AIGER file** (not just trusted from
codex's report), and the gate-level equations check out exactly:
`q0' = ¬q0∧¬q1`, `q1' = q0⊕q1`, `y = ¬q1∧¬o∧q0` (`y` is the exported `i`), with the state trajectory
from `(q0,q1)=(0,0)` reaching a fixed point `(0,1)` at cycle 2 and staying there — **and critically,
the transition function never references `o` at all**, only the output function does. That fact
gives a short, general, non-empirical proof of soundness under the "one-step-delayed" reading
(circuit's cycle-`t` output = environment's `i` at original round `t+1`, so the check is
`o(t) != y(t)`): if `o₀=1`, `y₀=0` unconditionally, forcing a violation at `t=0`; if `o₀=0`, the state
at `t=1` is forced to `(1,0)` regardless of `o₀`'s exact history, giving `y₁=¬o₁`, so `o₁≠y₁` is a
tautology — every possible infinite `o`-sequence is covered by exactly these two cases. Also checked
empirically against 5 chosen adversarial sequences (all-zero, all-one, both alternating phases, one
irregular single-pulse) — all 5 confirm the general proof, and the same 5 sequences show the
*same-round* reading (circuit cycle-`t` output = `i` at round `t`, no delay) fails on the irregular
sequence, correctly distinguishing the two readings rather than leaving them both plausible.

**This is a genuinely, provably sound environment witness for the tiny hand-verified game** — the
first one this sprint has actually produced end-to-end via the adapter path, not hand-constructed.

The `round_robin_arbiter_unreal2` seed (`rru2_n2.tlsf`, declared `SEMANTICS: Mealy; TARGET: Mealy;`)
was then run through the same pipeline and produced a plausible-looking 59-byte AAG with 2 inputs
(`g_1`, `g_0`) and 2 outputs (`r_0`, `r_1`) — **not validated for soundness**, deliberately out of
scope for that step; a real family instance needs the same kind of careful per-sequence/game
analysis as the tiny case, likely harder with more APs, and should not be trusted just because a file
appeared, per every prior finding in this thread.

**Independent review (a separate codex session, per the standing "codex writes, another subagent
reviews" workflow) caught a real, distinct 7th gap before anything was committed**: `try_degenerate_io`
has a *separate*, synthesis-capable shortcut (`run_no_input_ltl`, for specs whose *original* input
alphabet is empty) that fires whenever `input_aps.empty () and synth_fname.has_value ()`, regardless
of `check_unreal`. Confirmed by direct reading: `run_no_input_ltl` returns `not satisfiable` on the
unreal path immediately, before ever reaching its own file-writing block a few lines later — so a
spec with zero original input APs, combined with `-u formula -s FILE`, would report the correct
verdict but silently leave the file unwritten. This does not affect the tiny game (`input_aps={i}`,
non-empty) or `rru2_n2` (also non-empty), so it did not surface in rounds 6-9's own testing — exactly
the kind of gap an exhaustive independent sweep is for, distinct from re-deriving the soundness proof
above (which the review correctly did not re-litigate, deferring to the hand-verified derivation).
Review verdict: 6 of 8 checked points PASS outright (the ternary dispatch, the debug-only guard's
release-build inertness, `want_controller_strategy`'s three call sites, the untouched demand-provider
assert, the safety-core-witness guard/call-site, and the decomposition bypass's scope), one style nit
(`if (...) return ...;` on one line, against `.clang-format`'s `AllowShortIfStatementsOnASingleLine:
Never`), and this one real functional gap. Not yet fixed or re-reviewed.

**Round 10 fixed the review's gap and landed the whole thing.** `try_degenerate_io`'s no-input
shortcut now declines the `check_unreal==UNREAL_X_FORMULA and synth_fname` combination, falling
through to the already-fixed monolithic path instead of silently returning early. Verified with two
regressions plus the new case: real-side no-input synthesis (`F(o)`, no inputs) unaffected —
REALIZABLE, 20-byte AAG; the tiny hand-verified unreal case re-run **byte-for-byte identical** to
round 9's already-proven-sound 81-byte AAG, confirming this round's edit didn't leak into the
verified path; and the gap case itself (`G(!o) & F(o)`, no inputs, a direct self-contradiction with
no adversary AP to speak of) now produces an observable 21-byte AAG (0 outputs — there genuinely is
no adversary move in this degenerate case) instead of silently nothing. The style nit
(`if (...) return ...;` on one line) was also split to match `.clang-format`.

I then read the complete final diff myself line by line and ran the fast unit suite
(`meson test --suite unit`) against the fixed build directly — **50/50 pass**, confirming nothing
outside the narrowly-scoped `check_unreal`+`synth_fname` combination was disturbed. **Committed**:
`0bc262d6`, "Let -u formula combine with -s: export a genuine environment witness on UNREALIZABLE".

**Disposition**: landed. Ten rounds plus one independent review: five real structural gates (rounds
6-7), one invalidated test (round 3's corrupted formula), the actual root blocker identified by
instrumentation (round 5), one criterion fix first tried unconditionally and correctly caught as too
broad by round 8's own real-side regression check, then correctly scoped to the unreal child in
round 9 and independently verified sound by hand (a general two-case proof from the raw AIGER gates,
not just sampled sequences), and one further real gap (the no-input shortcut) found by independent
review and fixed in round 10. Every round through 9 reverted its own edits after a negative or
ambiguous result (`git status` confirmed clean each time, round 5 verified the restored file's
SHA-256); round 10's edit is the one that stayed, reviewed and tested, and is now committed.
**Post-commit soundness verification, done properly (not by hand this time).** The landed debug
assert (`aut->intersects (mealy_aig->as_automaton (false))` for the unreal case) is a real but
*weaker* check than genuine soundness: it only proves nonempty intersection with `!phi` under the
raw, undelayed Mealy reading of the circuit — i.e. that *some* system behavior gets defeated, not
that *every* one does. Re-deriving the tiny game's own timing convention carefully (twice, catching
my own confusion the first time) showed the raw/undelayed reading is exactly "Reading A", which the
tiny game's own hand-simulation had already shown fails on an adversarial sequence — so the landed
assert's pass is consistent with, but does not by itself establish, full soundness. The genuinely
correct, complete criterion (matching plan section 7.1's own contract: `L(E_N) ∩ L(Phi_N) = empty`,
every legal system response defeated) needs the circuit read through the one-cycle output delay —
and that delay is *already implemented*, correctly, as `acacia::synthesis::mealy_to_moore()`
(`src/solver/mealy_to_moore.cc`, existing code, used elsewhere for TLSF's own Moore-target case;
read and confirmed by an independent codex session to do exactly the delay described, not just
trusted from paraphrase).

Built a temporary (added, used, removed) diagnostic check: `mealy_to_moore()` the produced circuit,
then test *full containment* — `L(delayed circuit) ∩ L(phi) = ∅` — using Spot's own automaton
machinery directly against the true, untransformed original formula. This is the mathematically
complete statement, strictly stronger than the landed assert's nonempty-intersection check. Result:
**`true` on all three tests run** — the tiny hand-verified game (cross-validating my own independent
by-hand 2-case proof: agreement here is what gives confidence the new mechanized check itself is
correctly built, not just that the witness is sound), and both `round_robin_arbiter_unreal2` seeds,
`rru2_n2` and `rru2_n3`. A mechanical bug on the first attempt (`mealy_to_moore()`'s result uses a
fresh, unrelated `bdd_dict`, fine for its existing file-only use, incompatible with intersecting
against another automaton) was found and fixed with a serialize/reparse round-trip through the
shared dict — a plumbing issue in the new diagnostic code, not a soundness question, and confirmed
as such before concluding anything.

This is the real completion of W4's verification contract for this family's seeds, at a rigor
matching arbiter's own "VERIFIED" bar — not merely "a file appeared" or "an assert didn't fire," but
a machine-checked full-containment proof against the true original specification. All instrumentation
was temporary and has been reverted; the tree matches HEAD (`53a6bfc5`) after every round.

Applying this to the actual target (`round_robin_arbiter_unreal2_pb_7_pe_.ltl`, n=7 — the largest
in-corpus instantiation of this family, confirmed still `stable-unsolved`/TIMEOUT at both caps in
the broad campaign) is in progress as this note is written.

## W5–W7 — not started

W5 (bounded automatic proposer), W6 (candidate-restricted product construction), W7 (cold-cost
runner) are unstarted. W6 in particular is not currently motivated by any collected evidence: the
one target-verification obstacle actually hit (arbiter's n=10 monolithic-formula cost) was resolved
by conjunct decomposition alone, per plan section 7.3's own instruction not to reach for W6 until
eager checking is shown to be the real obstacle on its own terms.
