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

## W1 — broad campaign (running)

Launched `benchmarking/witness-lifting-20260918/commands.sh` detached (setsid+nohup) at
2026-09-18T10:58 local. Sequence: 120s cap (epoch 1, epoch 2) → ltlsynt@120s → 17s cap
(epoch 1, epoch 2) → ltlsynt@17s, B/S alternating per-shard (even shards: epoch-base order,
odd: reversed; epoch 2 starts from the reversed epoch-base order) so within-epoch host drift
cannot systematically favor one candidate. Progress: `opening/campaign-top-level.log`.
This is a multi-day run; W2 attribution is blocked on it finishing (or at least a shard's worth
of paired B/S rows for the instances of interest) and has not started.

**Host-sharing rule adopted this session, beyond what the handoff states explicitly:** no
concurrent solver-adjacent work (builds, or even brief verification-pipeline runs) once it stops
being brief — see W3/W4 below for where this actually bit.

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
   do **not** negate the objective. Before trusting a TLSF-level reconstruction of this, ran a paper
   differential check (no execution) against a hand-picked tiny unrealizable game,
   `G(o <-> X(i))` under Mealy — its only sensible environment strategy is the obviously-causal
   `i(t+1) := not(o(t))`. The naive swap+shift, read back literally, instead gives the dualized
   game's trivial `i := o` combinational strategy, which mapped back to the original round order
   would have the environment's move at `t+1` depend on the system's move at the *same* `t+1` —
   using information not yet available, exactly the unsound "future information" witness plan
   section 7.1 forbids. **This is a real near-miss, not a hypothetical caution**: it shows the
   formula-level swap+shift alone is not sufficient — the synthesized circuit also needs a
   clock realignment this session could not derive with confidence, so it was not implemented.

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
