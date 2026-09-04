# Sprint code review: portfolio arms and forward safety backend

Review target: `sprint/p5-portfolio-arms` at `1d506d43`.

This was a read-only static review.  Per the active benchmark constraint, I did
not configure, build, compile, run Meson, execute tests, or execute any project
binary.  The only workspace change is this report.

## Executive summary

The runtime-per-arm backend change has a good existing template seam.  Both the
forward and backward implementations already consume the same concrete
`SetOfStates`, IO-precomputer maker and actioner maker in
`solve_with_downset`; selecting between them at runtime needs two solver
instantiations for that one component tuple, not an instantiation per arm.  The
main work is to stop using the same preprocessor symbols for both *availability*
and *selection*, and to make the arm policy an explicit value passed down the
existing call chain.

The first refactor I would do is introduce an owned `solver_arm` /
`game_solver_options` value containing at least `game_backend`,
`local_certificate`, `k_schedule`, and the equivariant policy, construct it in
`acacia-bonsai.cc`, and thread it without changing behaviour through
`run_ltl -> run_one_ltl -> solve_game -> solve_game_vector ->
solve_with_downset`.  This establishes one policy seam before any `#if` is
removed and prevents backend policy from continuing to be inferred from worker
polarity in several layers.

I found no path by which the P1 checker can report UNREALIZABLE for a realizable
formula.  Its deliberate restriction is intact.

The highest-value concrete issues beyond that refactor are:

1. Solver wrapper objects retain references to temporary maker objects.  The
   current makers happen to be empty types with static `make` functions, but the
   references are already dangling and make stateful runtime policy unsafe.
2. Enabling the forward backend does not necessarily run it: the equivariant
   solver is tried first unless a separate flag is disabled.
3. Every forward result deep-copies the proof log and all environment ranks,
   including resource-limit and production results, after the byte cap has been
   evaluated.  This can defeat the solver's graceful memory limit.
4. The lazy controller hot path repeatedly walks `std::list`s and repeats hash
   lookups for the same successor.
5. The portfolio selector silently hides cross-arm verdict conflicts by keeping
   whichever conflicting answer is faster.

## 1. Preparing for runtime, per-arm backends

### What is selected where today

| Decision | Mechanism today | Consequence for flexible arms |
|---|---|---|
| Worker polarity, unreal transform and translation preference | Runtime values in `acacia-bonsai.cc:67-110`, stored by `run_one_ltl` at `src/solver/solver_invoker.cc:264-308` | Already value-level, but the data model is split across `real_strategies`, `unreal_strategies`, and one shared `primary_translation_pref` (`src/arg_parser.hh:38-40`).  It cannot describe arbitrary per-arm combinations cleanly. |
| Automaton preprocessor and Boolean-state implementation | Type macros invoked before `solve_game` at `src/solver/solver_invoker.cc:510-570` | They can remain build-wide if only the game backend varies per arm. |
| Downset implementation | Concrete `Downset` chosen once in `src/solver/solve_game_vector.cc:17-21` | Helpful: runtime backend dispatch remains inside one downset specialization. |
| IO precomputer, actioner and input picker | Type macros converted to maker template parameters at `src/solver/solve_game_impl.hh:175-201` | Helpful for backend selection.  Forward and backward already share the IO/actioner types.  The input picker is backward-only and should be removed from the forward template. |
| Equivariant solver | Include and whole attempt under `#if ACACIA_ENABLE_EQUIVARIANT_SOLVER` at `src/solver/solve_game_impl.hh:11-13,164-173` | Availability and selection are conflated; it also pre-empts the requested backend. |
| Forward versus backward solver | Include and branch under `#if ACACIA_FORWARD_SAFETY_SOLVER` at `src/solver/solve_game_impl.hh:14-16,178-203` | This is the primary gate to replace with runtime dispatch. |
| Local-certificate probe | Declarations, state, and loop body under `#if ACACIA_LOCAL_CERTIFICATE` at `src/solver/k_bounded_safety_aut.hh:37-121,199-277,325-328,381-384` | Must become a value on the backward backend configuration. |
| K schedule | Enum exists, but `ACACIA_K_SCHEDULE` is passed as a compile-time constant at `src/solver/k_bounded_safety_aut.hh:355-358` and `src/solver/forward_k_bounded_safety_aut.hh:139-142` | Straightforward runtime field; it does not need another solver instantiation. |
| Eager comparison mode | Non-type template parameter, defaulted from a macro at `src/solver/forward_reachable_safety.hh:1229-1244` | Runtime selection would instantiate both branches.  This is comparison tooling and need not be part of the first flexible-arm API. |
| Conditional covering | Structural `#if` in node layout at `src/solver/forward_game_nodes.hh:26-29,39-43` and throughout the search | This is not currently a runtime policy.  Given its mixed result and default-off status, keep it out of the first arm enum rather than doubling node/search variants. |
| Component header availability | `ACACIA_COMPILE_*` include gates in `src/solver/configured_components.hh:5-80` | Not a blocker for backend-only dispatch.  These gates matter only if component *types* also become per-arm choices. |

### Recommended first refactor and dispatch shape

Create a small header independent of `configuration.hh`, for example:

```cpp
enum class game_backend { backward, forward };

struct game_solver_options {
  game_backend backend;
  bool use_local_certificate;
  bool allow_equivariant;
  acacia::k_schedule::kind schedule;
};

struct solver_arm {
  std::optional<UNREAL_X_T> unreal_transform;
  TRANSLATION_PREF_T translation_preference;
  game_solver_options game;
};
```

Construct `solver_arm` before `start_proc` rather than passing the two unrelated
arguments currently accepted at `src/acacia-bonsai.cc:67-68`.  Pass the immutable
game options through the public `run_ltl` declaration
(`src/solver/solver_invoker.hh:39-44`), store them beside the other runner values,
and add them to the non-template `solve_game` and `solve_game_vector` interfaces
(`src/solver/solve_game.hh:11-15`, `src/solver/solve_game_branches.hh:13-16`).
The only actual dispatch then lives at `src/solver/solve_game_impl.hh:158-203`.

This does not cause a per-arm template explosion.  For the currently selected
component tuple, the translation unit will instantiate one backward class and
one forward class.  Every arm calls one of those same functions by runtime
value.  Do not turn the backend itself into a template argument: it would move
the selection back to compile time and make every policy combination a new
instantiation.

Split compilation availability from runtime selection.  A unified portfolio
build should compile both implementations (or use a separate
`ACACIA_COMPILE_FORWARD_BACKEND` capability gate), while the old
`ACACIA_FORWARD_SAFETY_SOLVER` value can temporarily supply a legacy default.
Likewise, compile the local-certificate implementation once and guard the probe
with `options.use_local_certificate`; `local_certificate.hh` is already included
unconditionally at `src/solver/k_bounded_safety_aut.hh:13`.

Make the dispatch order explicit.  At present the equivariant attempt precedes
both ordinary backends (`src/solver/solve_game_impl.hh:164-178`).  A request for
`game_backend::forward` should not silently run equivariant first.  Either make
equivariant a third backend or make `allow_equivariant` an explicit backward
pre-pass.  Similarly, define what `forward + do_synthesis` means: current code
silently uses backward for synthesis (`src/solver/solve_game_impl.hh:179-196`).
Rejecting an invalid combination at arm construction is clearer than silently
changing it.

Finally, replace the forward wrapper's `optional result +
should_fallback_to_backward()` side channel
(`src/solver/forward_k_bounded_safety_aut.hh:170-189`) with a typed outcome such
as `won`, `lost_at_kmax`, `resource_limit`, or `certificate_rejected`.  Runtime
dispatch should be able to decide fallback from the returned value without
having to know which internal boolean the backend set.

### [High] Maker references already dangle and block stateful runtime policy

Both K wrappers accept makers by `const&` and store those references
(`src/solver/k_bounded_safety_aut.hh:131-145,322-324` and
`src/solver/forward_k_bounded_safety_aut.hh:36-51,225-230`).  Their callers pass
temporary makers, then call `solve()` in a later statement
(`src/solver/solve_game_impl.hh:184-202`; the Python path repeats this at
`src/python/python_interface.cc:138-142`).  The temporaries die at the end of the
construction full-expression, so the stored references dangle.

The current maker APIs use static `make` functions, which is why this has not
shown up as an observed read of maker state.  It becomes an immediate lifetime
bug as soon as a maker carries per-arm options.  Accept and own makers by value;
`[[no_unique_address]]` keeps empty makers free.  Alternatively, remove maker
objects and invoke the maker types statically, but value ownership is the more
future-proof interpretation of the existing maker abstraction.

The forward wrapper's `InputPickerMaker` is entirely unused
(`src/solver/forward_k_bounded_safety_aut.hh:29-42`).  Remove that template
parameter and constructor argument instead of carrying backward-only policy
through the forward backend.

### [High] “Forward backend” is not an exclusive backend selection

`solve_with_downset` tries the equivariant solver before it reaches the forward
branch (`src/solver/solve_game_impl.hh:164-178`).  Meson's forward option does
not disable the equivariant option: their defaults and definitions are
independent (`meson.options:80-83`, `meson.build:185-193`).  Therefore a user who
sets only `-Dacacia_forward_safety_solver=true` can run equivariant on eligible
instances despite the option description saying forward is used “instead of”
backward.  The checked-in forward preset avoids this by separately setting
`enable_equivariant_solver=false` (`config/acacia-presets.json:123-127`), but the
backend option itself remains composition-dependent.

This is a current configuration correctness issue and must be resolved before
runtime arm selection; an enum value should name the backend that will actually
run.

### [Medium] Backend defaults disagree between the two configuration frontends

`meson.options:78` defaults `acacia_local_certificate` to `true`, while
`config/acacia-options.json:127` defaults `local_certificate` to `false` and the
fallback in `src/configuration.hh:17-19` is also false.  The comment in
`src/solver/k_bounded_safety_aut.hh:82-88` says the Meson option is default-off,
which is no longer true.  Builds made directly with Meson and builds made via
`scripts/acacia-config.py` therefore do not have the same backward backend.

Choose one default and generate all frontends from it before using that default
to initialize per-arm runtime options.  Otherwise the “real worker uses local
certificates” rule will depend on how the binary was configured.

## 2. Forward solver: refactor and optimisation findings

### [High] Result construction defeats the byte cap and deep-copies test-only data

`make_result` is called for win, loss, and resource-limit exits
(`src/solver/forward_reachable_safety.hh:274-297`).  It then:

- deep-copies the proof vector, including each dependency vector
  (`src/solver/forward_reachable_safety.hh:1188-1189`);
- copies every environment rank (`src/solver/forward_reachable_safety.hh:1190-1192`);
- builds a controller-parent vector (`src/solver/forward_reachable_safety.hh:1193-1195`);
- copies all losing-antichain ranks (`src/solver/forward_reachable_safety.hh:1196-1200`);
- and, for wins, also copies the reachable strategy ranks
  (`src/solver/forward_reachable_safety.hh:1210-1211`).

Those copies occur after `result.total_bytes` has been set from the search's
logical accounting (`src/solver/forward_reachable_safety.hh:1170-1187`).  A
search that reaches its configured memory limit can therefore allocate a second
copy of its largest structures while attempting to return `resource_limit`.
The reported byte count also understates peak live memory during result
construction.

Production only consumes the scalar metrics and `strategy_ranks`; proof replay
data is consumed by `tests/forward_safety_game_test.cc:177-192`.  Add an explicit
`capture_replay_proof` test option or split the result into a lightweight
production outcome and an optional replay artifact.  On losing results, moving
owned data is preferable to copying it.  On winning and resource-limit results,
do not materialize replay data at all.

This cleanup is made easier by removing `loss_evidence::have_certificate`: the
field is initialized from `result.losing_proofs` at
`src/solver/forward_k_bounded_safety_aut.hh:131-138`, but is never read by
`is_cheap` or `next` (`src/solver/k_schedule.hh:11-27,44-77`).

### [High] The lazy hot path repeats linear list walks and interner work

Every call to `advance_lazy_controller` starts at the beginning of the input
range and advances by `input_index` (`src/solver/forward_reachable_safety.hh:877-884`).
It then starts at the beginning of that input's action range and advances by
`next_action_index` (`src/solver/forward_reachable_safety.hh:964-971`).  The
configured actioner stores both levels in `std::list`
(`src/actioners/standard.hh:26-39`), so repeated loss/reselection makes the
cursor work linear, and potentially quadratic over a controller's action list.

Build a stable random-access view once per fixed-K search, for example a vector
of pointers to each input's actions and a vector of action pointers per input.
Then `input_index` and `next_action_index` remain valid O(1) cursors without
changing semantic action order or the backward picker's splice behaviour.

The same successor is also looked up twice.  The lazy loop calls `find_env`
(`src/solver/forward_reachable_safety.hh:979`), then `intern_env` recomputes the
coordinate hash and repeats the bucket scan
(`src/solver/forward_reachable_safety.hh:658-664,1032`).  Conditional covering
adds another exact lookup in `find_cover`
(`src/solver/forward_reachable_safety.hh:535-542`).  Compute the hash once and
use one `find_or_intern` operation that accepts the already-known lookup result.

For controllers that reject many duplicate successors,
`std::ranges::contains(ctrl.tried_env_ids, ...)` is another linear scan in the
per-action loop (`src/solver/forward_reachable_safety.hh:983-996`).  Keep the
vector for deterministic proof order, but add a small-set/hashed-set threshold
if profiles show large tried sets.

### [High, known algorithmic target] Invalidation still scans all visited nodes

Every successful losing-antichain insertion scans every interned environment
node and performs a partial-order comparison on every still-live candidate
(`src/solver/forward_reachable_safety.hh:602-634`).  This is the structure the
O3 numbers identify; indexing the much smaller generator list would not address
it.  The next optimisation should index the visited, non-losing environment
nodes for upward-orthant queries, with the exact Posets comparison retained as
the final check.  Even a coordinate-sum bucket prefilter can be shared with the
covering experiment, although a proper visited-node dominance index is the real
fix.

The O3 counters still earn their place while this issue is open: `nodes_checked`
and `nodes_invalidated` quantify the waste, and one scan count supplies context.
However, `losing_insertions` and `invalidation_scans` are necessarily identical:
the only successful `minimal_losing_antichain::insert` increments `insertions`
(`src/solver/minimal_losing_antichain.hh:56-82`), and the caller unconditionally
increments `invalidation_scans` immediately afterward
(`src/solver/forward_reachable_safety.hh:602-618`).  Remove one of those two
counters and shorten the fragile positional diagnostics API at
`src/solver/diagnostics.hh:673-721` by passing a named metrics struct.

O5 left no code scaffolding behind; its abandoned generator index is absent.

### [Medium] Conditional covering treats exact interning hits as covers

`find_cover` deliberately returns an exact live state first
(`src/solver/forward_reachable_safety.hh:535-542`).  The caller then stores a
second full copy of that exact successor, registers a `covered_by` dependency,
and enters cover-resolution machinery
(`src/solver/forward_reachable_safety.hh:1013-1028`).  An exact hit needs none of
that: it can be selected directly through the normal `selected_by` path.

Handle `known_env` first and restrict `find_cover` to strict dominators.  This
removes one rank allocation and one parallel dependency path for a common case,
and also eliminates the repeated exact lookup noted above.  This affects only
the default-off O4 mode.

### [Medium] P3's default linear mode still pays for adaptive evidence

The backward solver updates peak/loop evidence on every fixed-point iteration
and reads the clock on bound changes (`src/solver/k_bounded_safety_aut.hh:203-210,329-358`).
The forward wrapper similarly timestamps every attempt and constructs evidence
(`src/solver/forward_k_bounded_safety_aut.hh:100,131-142`).  Linear, geometric,
and direct-max schedules do not inspect that evidence; only
`cheap_loss_adaptive` does (`src/solver/k_schedule.hh:50-51`).

Once schedule becomes a runtime field, collect timing/frontier evidence only
for the adaptive schedule, or make `next` accept a lazy evidence callback.  The
kept P3 tooling should not impose measurement overhead on the default linear
path.  Also remove the unused `have_certificate` field described above.

### [Low] End-of-search and comparison-only state can be trimmed

- `losing_antichain_ranks` is written at
  `src/solver/forward_reachable_safety.hh:1196-1200` and has no reader anywhere
  in the tree.  It is dead and potentially large.
- `forward_ctrl_node::selected_action_index`
  (`src/solver/forward_game_nodes.hh:44-45`) is not used to build the returned
  certificate or strategy.  Outside O4 it is only assigned/reset; inside O4 it
  is round-tripped without affecting the cursor or proof.  Remove it unless
  forward strategy synthesis is about to consume it.  The eager reducer's
  `representative_action_index` is currently meaningful only to unit tests.
- `build_strategy` allocates `seen_ctrl` at
  `src/solver/forward_reachable_safety.hh:1113-1130`, but each controller node
  belongs to exactly one environment via `parent_env` and each environment is
  visited once.  `seen_ctrl` is redundant.
- `forward_minimal_successors` is not a meaningful reduction counter on the
  shipped lazy path: that path increments `distinct_successors` and
  `minimal_successors` together (`src/solver/forward_reachable_safety.hh:1008-1009`).
  Keep it explicitly eager-only or remove it from normal diagnostics.

## 3. Dead or inert sprint code

P2 is not unreachable: the preset at `config/acacia-presets.json:115-118` and
the direct tests still exercise it.  It is correctly default-off.  Its only
default-build cost is source/compile coupling: `actioners/standard.hh:3-5`
includes the 268-line dominance implementation even when the flag is false.
Guard that include with `ACACIA_PROFILE_DOMINANCE` and keep the direct test
including the helper itself.  The unbudgeted and budgeted dominance comparisons
also duplicate the same merge walk at `src/actioners/profile_dominance.hh:69-87`
and `src/actioners/profile_dominance.hh:132-163`; one comparator with an optional
budget observer would reduce proof-maintenance risk.

P3 is also reachable through presets/options and is intentionally retained as
tooling, but `loss_evidence::have_certificate` is dead and its default-path
evidence collection should be removed as described above.  The schedule code
itself is small and does not need deletion.

The eager forward comparison path is reachable from a build option and heavily
used by the differential test, so it is not dead.  Its metrics should be named
as comparison metrics rather than emitted as if the lazy production path were
performing Pareto minimisation.

Two older leftovers are worth removing while this area is open:

- `MAX_CRITICAL_INPUTS` is defined at
  `src/solver/k_bounded_safety_aut.hh:3-4` and referenced nowhere else.
- `k_bounded_safety_aut_detail::gen` is initialized and stored at
  `src/solver/k_bounded_safety_aut.hh:142,321` but never read.

## 4. P1 forced-output checker: soundness review

I found no false-UNREAL path.

- **Top-level implication:** `try_direct` explicitly declines it at
  `src/solver/forced_output_contradiction.hh:229-233`.  Independently, candidate
  collection does not descend through `Implies` or `Or`, so a missed syntactic
  spelling of implication would be a false negative, not an unsound match.
- **Direct-consequence traversal:** `collect_candidates` descends only through
  conjunctions, accepts the body of a direct `G`, and turns an immediate
  `G(A & B)` into separate `G A` / `G B` work items
  (`src/solver/forced_output_contradiction.hh:155-175`).  It never descends under
  `Or`, implication, temporal binary operators, or negation.  Translating an
  already-selected output-only Boolean formula may of course evaluate its
  internal Boolean `Or`; that does not weaken the consequence traversal.
- **Empty invariant set:** `chi_all` starts at `bddtrue`
  (`src/solver/forced_output_contradiction.hh:250-258`).  With no invariant, a
  response can match only when `beta` itself is false.  `G(alpha -> X^d false)`
  with a forceable bounded input trigger, and unconditional `G(F false)`, are
  genuinely unrealizable.  `And(empty)` is used only to format the proof
  (`src/solver/forced_output_contradiction.hh:260`).
- **Vacuous trigger:** the no-implication branch sets the trigger to `true` but
  still accepts only `F beta` or a finite `X` chain ending in output-only
  Boolean `beta` (`src/solver/forced_output_contradiction.hh:193-218`).  Because
  the candidate came from a direct `G`, the obligation is unconditional; the
  reasoning in the comment is valid.
- **Triggered response:** an implication trigger is accepted only when
  `bounded_input_pattern` can translate it using input APs, Boolean
  conjunction/disjunction, literal negation, and finite `X` offsets
  (`src/solver/bounded_input_pattern.hh:22-63`).  A satisfiable such finite input
  pattern is environment-forceable; unsupported syntax declines.
- **Contradictory invariants:** every member of `invariants` is an output-only
  body of a direct global consequence.  Therefore the early return when their
  propositional conjunction is false
  (`src/solver/forced_output_contradiction.hh:260-269`) proves the whole formula
  unsatisfiable, which is stronger than unrealizability.

The checker can be cleaned up without changing that surface.  It currently
creates one BDD dictionary in `try_direct` before even checking the top-level
guard (`src/solver/forced_output_contradiction.hh:226-233`), creates another
dictionary for every trigger satisfiability call
(`src/solver/bounded_input_pattern.hh:86-95`), and allocates BDD variables for
every declared output up front (`src/solver/forced_output_contradiction.hh:71-77`).
Use one checker-owned BDD context, one reusable `(input AP, offset)` translator,
and lazy output-variable allocation.  The input and output translators also
duplicate the same Boolean AST-to-BDD walk; sharing a conservative Boolean
encoder with pluggable variable lookup would reduce the chance that their
accepted operator sets drift.

## 5. Tooling findings

### [High] Portfolio selection masks verdict conflicts

When two arms decide the same instance, `evaluate` retains only the faster
answer without comparing the verdicts
(`benchmarking/select-portfolio-arms.py:53-60`).  A REAL/UNREAL conflict can
therefore increase the apparent union and enter the selected portfolio without
any warning.  Current campaign notes say there were zero conflicts, but the
selector should enforce that invariant itself: collect all verdicts per
instance, fail loudly if the set has size greater than one, then apply the
minimum-time rule.

### [Medium] Resume markers are not tied to campaign inputs

`run-portfolio-arms.py` skips an arm solely because `<arm>.done` exists
(`benchmarking/run-portfolio-arms.py:77-81`) and writes that marker after either
a clean run or a conflict-collecting exit (`benchmarking/run-portfolio-arms.py:105-113`).
The marker does not encode the binary, corpus/list, flags, cap, memory settings,
or `--limit`, and the code does not require the output/summary files to still
exist.  Reusing an output directory with changed parameters can silently mix or
skip campaigns.  Store a small manifest/fingerprint beside each marker and
validate it on resume.

### [Low] Backend-race output calls ties “slower”

`compare-backend-race.py:86-89` counts only strict `<` as faster and reports all
remaining cases as slower, so equal timings are mislabeled.  Report faster,
equal, and slower separately.  Also reject an empty `decisive_seconds` field
instead of coercing it to `0.0` at `benchmarking/compare-backend-race.py:32-39`,
which would make malformed data look maximally fast.

## 6. Other correctness conclusions

I did not identify another game-verdict bug in O1, O2, or O4 by static
inspection.  The lazy cursor advances monotonically, losing-antichain decisions
are confirmed by exact partial-order checks, hash collisions are resolved by
exact rank comparison (`src/solver/forward_reachable_safety.hh:511-523`), and a
lost O4 dominator correctly causes the saved smaller successor to be reconsidered
(`src/solver/forward_reachable_safety.hh:886-933`).

The findings above that are genuine current correctness/reliability defects are
the non-exclusive forward/equivariant option composition, inconsistent local
certificate defaults, hidden selector conflicts, and unfingerprinted resume
markers.  The dangling maker references are a latent lifetime defect with the
current static empty makers and become observable as soon as runtime state is
put into a maker.  The forward result-copy issue is a resource-limit defect: it
can allocate beyond the limit while constructing the result that is supposed to
report that limit.

No dynamic verification was performed because doing so would violate the active
benchmark campaign constraint.
