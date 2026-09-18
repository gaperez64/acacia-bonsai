# B1/B2: selective sparse-real dispatch

## B1: does the verify-all contrast survive without hints?

**Yes, and more cleanly than the hint-enabled P4 screen.** Control: the
shipped four-arm portfolio unchanged. Treatment: the same binary/options/
other slots, only `real:small:forward` replaced with
`real:small:spot-guarded-sparse`. Both sides use the same binary family
(commit `31144e84`, preset `otf_sparse_formula`, release profile — see
`commands.sh`), verify-all only (this recovery never carries P1a's
scheduling-hint code at all, so there is no flag to accidentally leave on).

Five alternating paired rounds on the frozen P4 list
([`targets/p4.list`](targets/p4.list), SHA-256
`9eab657c8c936403b5a2102f45beb868bb292a3acef49dfa5d12ddaf3b4e7e2a`, matching
the previous sprint's hash byte for byte). Raw rounds:
[`screens/p4-verify-all/`](screens/p4-verify-all/); evaluator output:
[`screens/p4-verify-all/admission.md`](screens/p4-verify-all/admission.md).

**`paired-admission.py` verdict: UNRESOLVED** (correctness PASS, improvement
PASS, no-regression UNRESOLVED):

| Instance | Control | Treatment | |
| --- | --- | --- | --- |
| SPIPureNext.ltl | 0/5 (TIMEOUT) | **5/5 REALIZABLE** | confirmed gain |
| heim-double-x-real.ltl | 0/5 (TIMEOUT) | **5/5 REALIZABLE** | confirmed gain |
| ordered-visits-choice-real.ltl | 0/5 (UNKNOWN) | **5/5 REALIZABLE** | confirmed gain |
| robot_grid_pb_5_5_pe_.ltl | 0/5 (TIMEOUT) | **5/5 REALIZABLE** | confirmed gain |
| thermostat-F-real.ltl | 0/5 (TIMEOUT) | **5/5 REALIZABLE** | confirmed gain |
| workstation_resupply_pb_3_pe_.ltl | **3/5 REALIZABLE** | 0/5 (TIMEOUT) | confirmed regression, lost in rounds [1,2,5] |
| sort50.ltl, amba_decomposed_arbiter_pb_5_pe_.ltl, collector_v2_pb_12_pe_.ltl | 5/5 | 5/5 | unaffected (solved by backward/unreal arms, not the substituted arm); cohort runtime ratio 1.008, no benefit |
| Alarm_a5f99bc6.ltl | 0/5 (TIMEOUT both) | 0/5 (TIMEOUT both) | unaffected, unchanged from baseline |

All 5 gains are unanimous (5/5 vs 0/5) across every round — a stronger,
cleaner signal than the previous hint-enabled screen. The workstation loss
is validated (lost in 3 of 5 rounds, not a single-round fluke).

**§8.3 longer-cap (51 s) adjudication of workstation**
([`screens/wsr-51s/`](screens/wsr-51s/)): 3 alternating rounds, same binary
family. Control (forward): 15.7 / 16.8 / 18.2 s, all REALIZABLE. Treatment
(sparse): 49.3 / 51.2 (TIMEOUT) / 50.8 s. This is a **systematic slowdown**,
not near-cap noise — even more pronounced than the previous sprint's
hint-enabled finding (15.8–17.8 s vs 38.5–42.6 s there).

**Conclusion:** a blanket arm substitution is not admissible (workstation
regresses, and coverage is a set requirement — five gains cannot buy it
back), but a real, confirmed positive contrast survives without hints. This
justifies B2.

## B2: one small structural rule

### Feature capture

Only already-cached counts from the final, preprocessed frozen automaton,
captured via one `-v` boundary print added at the existing "before-solve"
point in `solver_invoker.cc` (§8.2 step 1; no fresh SCC computation,
language test, or per-edge BDD traversal). Real per-target values, not a
proxy:
[`targets/b2-structural-features.tsv`](targets/b2-structural-features.tsv).

**Raw states (N) does not separate gains from the regression** — the 5
confirmed gains span N ∈ [25, 2577], and workstation's N=151 sits squarely
inside that range. An early attempt calibrated a states threshold against
an *independent* Spot `ltl2tgba` proxy translation instead of Acacia's own
N (the CLI verbosity needed to read Acacia's own N was mistakenly measured
from a release binary, which compiles `-DNO_VERBOSE` and so never printed
anything, misread as a race condition rather than the wrong binary); that
threshold did **not** replicate against the real numbers and was discarded
before being deployed anywhere. Two candidates were considered before
settling on the boolean-state fraction:

1. Raw N threshold — rejected: does not separate the confirmed cases (see
   above).
2. Raw E (edges) threshold — rejected: workstation's E=1685 is smaller than
   3 of the 5 confirmed gains (3894, 42266, 27205).
3. **B/N (boolean-state fraction) threshold — selected.**

| Target | N | B | B/N | B1 result |
| --- | ---: | ---: | ---: | --- |
| SPIPureNext | 295 | 2 | 0.7% | gain |
| heim-double-x-real | 669 | 16 | 2.4% | gain |
| ordered-visits-choice-real | 25 | 6 | 24.0% | gain |
| robot_grid_pb_5_5_pe_ | 2577 | 626 | 24.3% | gain |
| thermostat-F-real | 1533 | 152 | 9.9% | gain |
| workstation_resupply_pb_3_pe_ | 151 | 99 | **65.6%** | LOSS |

Every confirmed gain has B/N ≤ 24.3%; the confirmed regression has
B/N=65.6% — a wide, clean margin. Mechanistically this is a reasonable
story: `spot-guarded-sparse` trades a smaller upfront cost for slower
per-step BDD-guarded evaluation, which wins when there is little to
explore and loses once the search must cover many states with few
already-boolean-decided outcomes (workstation's B/N=65.6% means two-thirds
of its states are already boolean-degenerate, where the plain forward
action table's fixed per-step cost apparently wins).

**Frozen candidate: `choose_real_backend` picks `spot-guarded-sparse` when**
**100·B/N ≤ 30, else keeps `forward`.** (`src/solver/real_backend_selector.hh`/`.cc`)

### Holdout

Family-grouped, using `workstation_resupply`'s untested siblings (existing
corpus metadata; same family as the known forward-sensitive control, kept
in the same group per §8.2 step 2):

| Target | N (Acacia) | Timing check (control vs treatment, single round) | Rule verdict |
| --- | ---: | --- | --- |
| `pb_1_pe_` | 28 | both REALIZABLE (both fast) | uninformative for correctness either way (both backends already fine) |
| `pb_2_pe_` | 66 | both REALIZABLE (both fast) | uninformative for correctness either way (both backends already fine) |
| `pb_4_pe_` | 372 | both TIMEOUT at 30 s | uninformative — neither backend solves it within a useful cap |

The holdout's own B/N wasn't separately computed (both directions were
already safe/uninformative on the *timing* dimension, which is what
matters for admission); the rule's B/N logic was validated end to end
through the actual compiled selector on the real 6-target evidence table
above and confirmed live via two representative cases (see below), not
inferred from a proxy.

### Live confirmation of the compiled wiring

Via `ACACIA_SPOT_CAPTURE_DIR` and `-v` on the debugoptimized build with the
selector compiled in (`build_check`, `-Dacacia_real_backend_selector=true`):

- `ordered-visits-choice-real` (B/N=24.0%): captured record shows
  `real_backend_selector_requested=forward`,
  `real_backend_selector_effective=spot-guarded-sparse`. REALIZABLE.
- `workstation_resupply_pb_3_pe_` (B/N=65.6%): every verbose line through
  the whole K-schedule (K=2→5→8→11→…) is tagged
  `[real=small,backend=forward]` — the selector never switched it.

### Dispatch point and code

`src/solver/real_backend_selector.hh`/`.cc`: a pure, `noexcept`,
side-effect-free function (`tests/real_backend_selector_test.cc`: 12
assertions covering threshold boundaries at both edges, disabled
selection, unavailable/zero/inconsistent features, and determinism across
repeated calls with identical inputs). Wired once in `solver_invoker.cc`,
immediately before the existing `solve_game(...)` call, guarded by
`not check_unreal.has_value() and not synth_fname.has_value() and
effective_backend == forward and provider == frozen_graph` — real-forward
slot, decision-only, frozen graph, after Boolean-state renumbering, before
any action-table construction, matching §8.3 exactly. Everything else
(unreal workers, synthesis, an already-non-forward backend, a research
provider) bypasses the selector untouched;
`tests/check-game-backend-cli.sh`'s new section confirms this behaviorally
(unreal/synthesis/backward verdicts unaffected) on top of the pure-function
unit tests. Compile-gated on `ACACIA_REAL_BACKEND_SELECTOR &&
ACACIA_SPOT_GUARDED_BACKEND`; registry option
`acacia_real_backend_selector` (default **false**) plus
`acacia_real_backend_selector_max_boolean_percent` (default 30), both
present in `meson.options`, `config/acacia-options.json` and the
`acacia_build_config.hh.in` fallback, validated by `acacia-config.py
validate`/`check-config-frontends.py`. (One easy-to-miss step: adding a
registry option also needs an explicit `build_conf.set(...)` line in
`meson.build` itself — the frontend-agreement checks do not catch a
missing one, and the macro silently expands to nothing without it,
producing a `#if X && Y` "operator has no left operand" compile error.)

### Deployment-candidate confirmation (§8.4)

Not a static arm substitution this time: control is the plain candidate
binary (`build_sr_candidate`, SHA-256 truncated `436e1a78…`, selector
compiled out), treatment is the same binary family with the selector
**compiled in and left to decide dynamically**
(`build_sr_selector`, SHA-256 `147782251aac9556…`) — no `--arms` override,
exactly the shipped four-arm portfolio with one add-on decision point.
Five alternating rounds, same P4 list:
[`screens/p4-selector/`](screens/p4-selector/), evaluator output
[`screens/p4-selector/admission.md`](screens/p4-selector/admission.md).

**Result: `paired-admission.py` UNRESOLVED again — but not because the**
**selector chose wrong.** The 5 gains reconfirm 5/5 vs 0/5 exactly as in
B1. Workstation:

| Round | Control | Treatment |
| --- | --- | --- |
| 1 | TIMEOUT (17.05 s) | TIMEOUT (17.05 s) |
| 2 | REALIZABLE (16.45 s) | TIMEOUT (17.05 s) |
| 3 | TIMEOUT (17.05 s) | TIMEOUT (17.05 s) |
| 4 | REALIZABLE (16.53 s) | TIMEOUT (17.05 s) |
| 5 | TIMEOUT (17.05 s) | TIMEOUT (17.05 s) |

Control: 2/5 (down from B1's own 3/5 for the *same, unmodified* candidate
binary — see below). Treatment: 0/5.

**Directly verified the selector is not the cause of this specific loss.**
Capturing `real_backend_selector_requested`/`effective` for workstation on
`build_sr_selector` gives `requested=forward, effective=forward` — the
selector correctly keeps it on forward (B/N was 117/169=69.2% in that
capture; the small difference from the 151/99 measured via the
debugoptimized build is the default four-arm portfolio construction vs. an
isolated single-arm run, not a rule error, and does not change which side
of the 30% threshold it lands on). Since treatment uses the *identical*
backend as control for this instance, the extra 0/5-vs-2/5 gap cannot be
the rule choosing wrong.

**What changed instead: the control binary itself drifted from B1.**
Adding `real_backend_selector.cc` to `ab_sources` (needed so the
production binary can link it) changed `build_sr_candidate`'s own object
code even though the feature stays compile-time disabled there — its
SHA-256 changed from B1's `e387ad22…` to this screen's `436e1a78…`. B1's
control solved workstation 3/5 (16.31–16.55 s); this screen's nominally
"same" control solved it only 2/5 (16.45–16.53 s on its wins, 17.05 s
flat on its losses). This matches, almost instance-for-instance, the
previous sprint's own documented finding on this exact target: P0's
worker-record instrumentation — never called on this hot path either —
still cost +1.0–4.6% cycles on `workstation_resupply_pb_3_pe_`
specifically, attributed via same-path rebuild and disassembly diff to
`-Ofast -march=native` LTO moving a hot loop's alignment, not to added
work (`benchmarking/demand-sparse-20260916/diag-perfdiff/ATTRIBUTION.md`,
captured here as `[[lto_code_placement_noise]]`). Workstation is exactly
the kind of ~17 s knife-edge instance that memory names as sensitive to
this. A matching same-path rebuild / perf-stat / disassembly attribution
for *this* change was not performed this sprint (time-bounded; §8's
`Do not run twelve full timing campaigns` spirit extends to not chasing
every plausible regression to a instruction-level cause), so this is a
strong, evidence-matched hypothesis, not a certified root cause.

**Disposition: not admitted, regardless of mechanism.** §10.1 is explicit
that an unresolved near-cap loss blocks activation and is never noise by
default. Whether the cause is the rule (it is not, per direct
verification) or code placement from merely linking the selector in (the
evidence-matched hypothesis), the deployed candidate still shows
workstation losing coverage it had before, and gains elsewhere cannot buy
that back. `acacia_real_backend_selector` stays **false** by default;
nothing in this package changes `docker_default`. The selector's own
decision logic is retained, tested and correct — should this be picked up
again, the next step is the same disassembly-level attribution P0 already
has a documented recipe for, on `real_backend_selector.cc`/its call site
specifically, before reconsidering deployment.
