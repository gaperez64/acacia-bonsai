# Package decisions — symbolic rows / selective sparse-real sprint

Pinned production baseline `7e6e932a8c44a3e263ddde9ebd3c6e60ea1e0aab`. Admission rule
([CLAUDE.md](../../CLAUDE.md), carried forward from the previous sprint, §10 of the
sprint handoff): correctness **and** no validated regression **and** an independently
confirmed useful benefit, all in the actual proposed deployed configuration. Coverage
is a set requirement — gains never buy back a lost solve.

## R0 — clean recovery (research only)

**Not a production package.** Ported the closure/cursor Büchi provider core
(`c1f60fff`) and its worker/replay integration (`64b961f0`) from
`research/ds-p2-closure-buchi` onto the pinned baseline, excluding P0
(lifecycle records), P1a (scheduling hints) and P3a (move compaction).
Verified: full unit suite green on a fresh build. Commit `e4883ec3`.

## A1 — Boolean-guard folding in the closure/cursor Büchi provider

**Retained on the provider's research branch; correctness and mechanism**
**benefit demonstrated, no production change.** `expand()` forked one
branch per Boolean `Or` disjunct even for pure-Boolean subformulas;
`Impl::is_boolean`/`Impl::boolean_guard` fold a Boolean obligation into one
memoized, checked BDD before continuing, preserving the exact raw-row map
`R_S[(T,P)]` extensionally (only the processed root is marked done; next/
postponed untouched; terminal merging by `(T,P)` unchanged). Measured live:
n=8 wide Boolean family needs 257 branches under the frozen
`RowExpansion::enumerative` control vs. 2 under the fix. 20 new tests
(raw-map equality against the enumerative oracle, non-growing branch
count, shared-subDAG reuse, false/tautology handling, the `(a|b)&a`
descendant-marking hazard, U/R postponement, mixed `p|X q`, fault
injection, state lifetime); full unit suite green. Commit `182bb061`.

**Not deployed** — nothing to deploy: the closure-buchi provider has no
compiled-default preset or shipping route; A1 is a fix to research code
that stays research code.

## A2 — first-row diagnosis on the frozen P2 cohort

**Determined: A1 alone does not newly complete any of the 19 previously-**
**blocked P2 targets' first row at frozen default budgets**, though the
mechanism fix is measurably real (up to −32% branches on an
already-completing target, up to −25% on a still-failing one, with 11 of
19 blocked targets showing some reduction and 9 showing none). Full
evidence and per-target classification: [boolean-rows.md](boolean-rows.md).
Commit `31144e84` (also adds `--stop-after-first-row` and `--formula-file`
to the research replay tool, needed for this diagnosis).

## A3 — exact pending-configuration coalescing

**Not attempted.** Its gate requires A2 to show repeated *identical*
residual `(T,P)` keys; the branch-count reductions A2 measured are
consistent with removed Boolean-witness redundancy, not with that specific,
finer-grained evidence, which was not instrumented this sprint. Per the
sprint plan's own stopping rule ("if exact keys rarely repeat, stop:
syntactic similarity is not evidence for this cache"), no code was written.

## A4 — end-to-end provider experiment and deployment candidate

**Does not proceed.** A2 produced no previously-failing-but-now-useful
query anywhere in the P2 cohort, so there is nothing to attribute a
provider solve, runtime or memory benefit to. The closure-buchi provider's
disposition is unchanged from the previous sprint: blocked by first-row
explosion on this cohort (now for genuinely different reasons on most of
them — see boolean-rows.md — not the Boolean-witness redundancy A1 fixes),
not integrated.

## B1 — verify-all recheck of the sparse-real substitution

**Confirmed: a real, hint-free positive contrast survives** (previous
sprint's finding used scheduling hints; this recovery has no hint
mechanism at all, so this is a strictly cleaner replication). Five
alternating rounds, ten-target frozen P4 list, control = shipped four
arms, treatment = `real:small:forward` → `real:small:spot-guarded-sparse`
only. Result: **5/5 coverage gains** (SPIPureNext, heim-double-x-real,
ordered-visits-choice-real, robot_grid_pb_5_5_pe_, thermostat-F-real, all
0/5→5/5 unanimously across every round) and **one confirmed regression**
(`workstation_resupply_pb_3_pe_`, 3/5→0/5, lost in rounds [1,2,5]),
validated systematic at a 51 s cap (15.7–18.2 s forward vs. 49.3–51.2 s
sparse). `paired-admission.py`: UNRESOLVED (coverage is a set requirement;
the loss blocks a blanket substitution). Full evidence:
[selector.md](selector.md). Justifies B2.

## B2 — one small structural dispatch rule

**Rule derived, directly verified correct, and tested — but the deployed**
**candidate still fails admission on the same instance, for an unrelated**
**reason.** `choose_real_backend`: prefer `spot-guarded-sparse` for the
real-forward slot when `100·boolean_states/states <= 30`, else keep
`forward` — the only single-feature rule among those considered (raw N,
raw E, B/N) that cleanly separates the 5 confirmed gains (B/N ≤ 24.3%)
from the confirmed regression (B/N = 65.6%); raw N and E do not (see
selector.md's full candidate table). Pure function
(`src/solver/real_backend_selector.hh`/`.cc`), 12-assertion unit test,
wired once in `solver_invoker.cc` exactly at the §8.3 boundary (real-
forward slot, decision-only, frozen graph, after Boolean-state
renumbering, before action-table construction), compile-gated and
registry-controlled (`acacia_real_backend_selector`, default **false**).
CLI-level bypass checks added to `check-game-backend-cli.sh`. Directly
confirmed live (capture) that the compiled selector computes
`requested=forward, effective=forward` for workstation and
`effective=spot-guarded-sparse` for a confirmed gain.

The five-round deployment-candidate confirmation (control: candidate
binary without the selector; treatment: same family with the selector
compiled in and deciding dynamically, no `--arms` override) reconfirms the
5 gains but still shows workstation regressing (treatment 0/5; this
screen's own control only 2/5, down from B1's 3/5 for the nominally same
configuration). Because the selector is *directly verified* to choose
`forward` for workstation in both control and treatment, this is not the
rule choosing wrong. It matches the previous sprint's own documented
finding on this exact instance (`[[lto_code_placement_noise]]`): unrelated
code linked into the same `-Ofast -march=native` LTO release image can
shift a hot loop's alignment and flip a ~17 s knife-edge instance, with
byte-identical logic on the executed path. A matching disassembly-level
attribution was not performed this sprint (time-bounded).

**Disposition: not admitted**, regardless of mechanism — §10.1 treats an
unresolved near-cap loss as blocking by default, never assumed noise.
`acacia_real_backend_selector` ships **disabled**; `docker_default` is
unchanged. The rule and its wiring are retained, correct and tested on
this sprint's branch for a future attempt with proper code-placement
attribution.

## C — combined candidate and closing report

**Candidate = baseline.** Neither A nor B was admitted, so there is no
combined configuration to freeze beyond the pinned baseline itself,
matching §9.6/§12.4's explicit provision for this outcome. The required
closing three-way PAR-2 table and cactus plots reuse the previous sprint's
own closing rows byte-for-byte
([closing/README.md](closing/README.md)) rather than re-running a
1,524-instance campaign: this sprint's pinned baseline
(`7e6e932a`) is proven solver-source-identical to the already-measured
frozen baseline binary (`git diff --stat` between the two commits, scoped
to `src/`, the build files and the posets submodule pin, is empty). No new
full-corpus run was needed or performed.

| Series | Solved / 1,524 | PAR-2 total (s) |
| --- | ---: | ---: |
| Acacia baseline | 1,176 | 12,624.620 |
| Acacia candidate (= baseline) | 1,176 | 12,624.620 |
| ltlsynt | 1,258 | 9,504.731 |

## Summary

No solver code lands in `docker_default` this sprint. What's retained:

- A1's Boolean-guard folding on `research/`-equivalent code on this
  sprint's own branch (tested, measured, real mechanism improvement).
- B2's selector, same status (tested, measured, real and *verified-correct*
  decision logic, blocked on an unrelated code-placement effect).
- A2's and B1's measurements, both negative and positive, as evidence for
  any future attempt at either track.
- The `--stop-after-first-row`/`--formula-file` replay additions and the
  `-v` structural-feature boundary print, both narrowly useful independent
  of any solver-speed claim (§10.1 permits landing these for their own
  purpose).
