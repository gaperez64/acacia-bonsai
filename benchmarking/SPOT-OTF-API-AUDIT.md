# Spot on-the-fly API: factory, row construction, and acceptance audit

Date: 2026-09-06  
Repository reviewed: `acacia-bonsai` at `d47ca7c476fd5e833e6289ba9387323c3b339e3d`  
Spot reviewed: installed `/usr/local`, `2.15.1.dev`, supplied base `2ae6210237` plus the inert synthesis enum patch described below.  
Method: read all 411 lines of `ltl2taa.cc`, all 309 lines of `taatgba.cc`, all
410 lines of `twaproduct.cc`, and the related implementations and public
contracts cited below. Built the research tools, ran the P0 probe and additional
operator, unsupported-input, resource-limit, and CLI checks, and ran the existing
unit **and version** suites. No benchmark suite or corpus campaign was run.
No translator, solver, row adapter or Büchi wrapper was implemented. Spot was
not rebuilt or re-pinned. The supplied Spot provenance is taken as given.

## Executive call

**A — pass for the public TAA/TGBA construction on the tested LTL cases.**
The highest-value move is **admit direct `ltl_to_taa(f, dict, false)` as the
P5 research candidate, then compare its identical construction fully enumerated
and on demand**. Its factory builds the alternating skeleton eagerly; requesting
a TGBA row combines the skeleton states on demand. These are separate costs,
confirmed by source inspection and measured separately by the probe.

All 24 required formula/provider cases passed language equivalence, fixed
acceptance, and the consumer ownership exercises. This establishes an executable
candidate, not a coverage or performance win. For `GF a & GF b`, the TAA view has
5 reachable states and 29 edges; the ordinary translator reference has 1 state
and 4 edges. A conjunction of 20 eventualities never reaches a successor
request at all: at a one-second cap the TAA factory is killed by the alarm, and
at twenty seconds it dies of `std::bad_alloc` **in the factory**, while the
ordinary translator on the same formula merely runs out of time
(`signal_14@factory`). The two providers therefore fail differently, and the
eager alternating skeleton is the thing that does not fit. A PSL repetition case
throws `unimplemented` there. Retain these negative results.

Do **not** describe the factory as a lazy traversal of the formula, equate
consumer row counts with avoided translation, feed raw alternating transitions
to Acacia, or put graph-only degeneralization in a supposedly lazy path.
P1–P4 remain independent. P5 still needs ordinary Büchi normalization with an
explicit rank-increment convention; this audit implements none of it.

## 1. Version discipline and reproducibility

### Supplied provenance, preserved without re-pinning

The linked installation is `/usr/local`, with `SPOT_VERSION "2.15.1.dev"` at
installed `spot/misc/_config.h:1357` and headers dated 2026-08-04. It is **not
mainline**. Its base is upstream `2ae6210237` (2026-06-20), or `cb4a98d1cc`
(2026-06-18), which is indistinguishable on installed content, plus a local
patch to `spot/twaalgos/synthesis.hh` adding:

- `GOODSET`: “Exact-signature BDD partition (experimental HOG-inspired)”.
- `GOODSETMIN`: “Minimal-antichain HOG good-set game (experimental)”.

These enum values exist nowhere upstream. Acacia includes that header in four
places but never references `solver_type` or either value; **the patch is inert
here**.

Confirmed by the repository owner after this audit was drafted: GOODSET and
GOODSETMIN are their own work, and nothing else was changed in this Spot
installation. The base identification is therefore authoritative rather than
inferred -- upstream `2ae6210237` plus exactly this one patched header.

The 14 installed headers under `spot/ta/*`, `spot/taalgos/*`, and
`spot/twaalgos/copy.hh` are debris from earlier installations. They were not
audited and are not available-API evidence. The supplied deletion dates are
2025-10-21 for testing automata and 2026-05-16 for `copy.hh`; `make install` does
not remove withdrawn headers. Both deletions precede the pinned June development
base. There is a date inconsistency in the supplied phrase “both before the
2.15.1 tag”: 2026-05-16 follows the supplied tag date 2026-04-25. This does not
alter the debris classification for this pinned development installation.

All Spot source citations below are local `file:line-range` references relative
to this read-only checkout, already at `2ae6210237`:

```text
/tmp/claude-1000/-home-gperez-GIT-repos-acacia-bonsai/929d329c-78e3-49f9-a5e6-0f4419a23557/scratchpad/spot-src
```

The supplied comparison establishes that `spot/twa/taatgba.hh`,
`spot/twaalgos/ltl2taa.hh`, `spot/twa/twaproduct.hh`, `spot/twa/twa.hh`, and
`spot/twaalgos/degen.hh` there are byte-identical to the installed headers.
**The source citations are valid for the linked library because this is its
supplied matching base and these interfaces are byte-identical; the only
supplied local patch is the inert synthesis enum extension.** Targeted `cmp`
also confirmed equality for `ltl2tgba_fm.hh` and `twagraph.hh` while resolving
their additional public interfaces. No website or newer Spot branch supplied
API evidence.

### Actual build and execution identity

| Item | Recorded value |
|---|---|
| Acacia | `d47ca7c476fd5e833e6289ba9387323c3b339e3d`; the pre-existing untracked `otf.md` was left unchanged |
| Posets | `139e14336b7a1f0bc064022e587ea4e1b9a81427` |
| tlsf-tools | `b42d5ef4a680252e04820ac7f073f5d786a43f7c` |
| Benchmark submodule | `4105caf1f1e5fd3b76657879bfce8021d130cbde`; no corpus run |
| Spot / BuDDyX pkg-config versions | `2.15.1.dev` / `2.3a` |
| Runtime libraries (`ldd`) | `/usr/local/lib/libspot.so.0`, `/usr/local/lib/libbddx.so.0` |
| Acceptance capacity | `SPOT_MAX_ACCSETS=64`, installed `_config.h:1119-1120` |
| Compiler / linker | GCC `16.2.1 20260819 (Red Hat 16.2.1-2)` / GNU ld `2.46.1-1`, x86-64 |
| Build | Meson `1.11.2`, C++23, `debug`, `-O0 -g`, LTO false, sanitizers `[]` |
| Preset | No named preset: `acacia_preset=""`; complete normalized project options below |
| Worker arms / K | No worker or solver invoked by the probe; configured defaults: arms empty, linear K schedule, minimum 2, increment 3, maximum 99 |
| CPU affinity | Inherited CPUs `0-15`; no dedicated core |
| Cgroup | Current scope and visible ancestors: `cpu.max=max 100000`, `memory.max=max` |
| Probe limits | Each fresh child: wall alarm 10 s, address-space limit 1,024 MiB, core dumps disabled; the explicit cap check uses 1 s |
| Corpus / source maps | Not applicable to the synthetic formula list; no source-map or corpus campaign was generated |

The current cgroup was
`/user.slice/user-1000.slice/user@1000.service/app.slice/ptyxis-spawn-f9781140-dfe4-4960-ad91-943284d7b08a.scope`.
Spot's compiler flags were not reconstructed; its installed binary hashes
identify the build used. The consumer build has no sanitizers, so ownership
smoke checks below do not claim a leak-detector or sanitizer result.

```text
8014c3b6f78bb3e9f6bd7eb2a899db506db56f3e7de788dc99b2eb6443a83b20  /tmp/otf-p0/src/acacia-spot-otf-probe
3eb40696b2d3c0f42fdc94d477f7de64c140d0f4eb156bc0ea29a661dca2a89c  src/research/spot_otf_probe.cc
0d6dbbb6c985bd3001d1df5b6b562e5b403a00ec0d6bb3275fc0617b3d5faf32  /usr/local/lib/libspot.so.0
a991f2049c44e3f3cf9102b7d40d9efc2c5bb2b8a3a1af7d120f7c23c9caf44d  /usr/local/lib/libbddx.so.0
410f01c9ea95407d0c142ced34ef4af5fcdcd507ba24f3093615f2e5848d4fb7  otf.md
0be3283fd2fc66146279fe29d7507bd0479748c9ec9b2939bfedb59a2ccd3f15  config/acacia-options.json
bdaa59dfe0f5e96fc317ec646abb62f7f7c9af89bbd220c807e2b3becf07120c  config/acacia-presets.json
70649ab29245af2f67c78ad82500a56297b8dc7d918cc2cefd1015de1d35ab30  tests/ltl/realizable/OneCounterGuiA9.ltl
18c673b92660a94679a8d32fd86182b1a71c15d2af125583be0e8a9754485e4a  /tmp/otf-p0-normalized-options.json
```

The normalized project-option snapshot includes every project option from
`/tmp/otf-p0/meson-info/intro-buildoptions.json`. Solver flags are recorded for
reproducibility but do not affect this Spot-only probe:

```json
{
  "acacia_actioner": "standard",
  "acacia_aut_preprocessor": "surely_losing",
  "acacia_boolean_states": "forward_saturation",
  "acacia_compile_all_components": false,
  "acacia_compiler_profile": "debug",
  "acacia_cpre_avoid_unions": false,
  "acacia_decompose_spec": true,
  "acacia_default_arms": "",
  "acacia_default_k": 99,
  "acacia_default_kinc": 3,
  "acacia_default_kmin": 2,
  "acacia_default_spot_fast": "det",
  "acacia_default_unreal_x": "both",
  "acacia_enable_diagnostics": false,
  "acacia_enable_equivariant_solver": true,
  "acacia_enable_realizability_simplifier": true,
  "acacia_enable_syntactic_bypass": true,
  "acacia_enable_tlsf_frontend": false,
  "acacia_equivariant_exhaustive_detect": false,
  "acacia_equivariant_max_orbits": 4096,
  "acacia_equivariant_max_output_letters": 4096,
  "acacia_equivariant_max_states": 512,
  "acacia_equivariant_max_sweep_clients": 4,
  "acacia_equivariant_min_blocks": 2,
  "acacia_equivariant_min_clients": 3,
  "acacia_equivariant_validate_fast_recognition": false,
  "acacia_forced_output_contradiction": false,
  "acacia_forward_conditional_covering": false,
  "acacia_forward_eager_minimal_successors": false,
  "acacia_forward_safety_solver": false,
  "acacia_input_picker": "critical_pq",
  "acacia_ios_precomputer": "standard",
  "acacia_k_schedule": "linear",
  "acacia_local_certificate": false,
  "acacia_ltl_frontend": "baseline",
  "acacia_no_simd": false,
  "acacia_preset": "",
  "acacia_profile_dominance": false,
  "acacia_simd_is_max": true,
  "acacia_symmetry_profile": false,
  "acacia_symmetry_verbose_diagnostics": false,
  "acacia_tlsf_corpus_dir": "",
  "acacia_transition_acceptance": false,
  "acacia_translation_pref": "small",
  "acacia_vector_downset": "vector_backed",
  "acacia_vector_impl": "auto",
  "build_python": false,
  "build_research_tools": true,
  "build_tests": true
}
```

## 2. Resolve every P0 entry point

### API evidence table (otf.md 3.2 and 11.3)

All rows use linked version `2.15.1.dev`, supplied base `2ae6210237` plus the inert
enum patch. “Usable” distinguishes an eager reference, an LTL generation
candidate, and a lazy product of already constructed factors.

| Provider / entry point | Linked version | Factory does | Row request does | Acceptance | Materializing calls | Usable? | Source evidence |
|---|---|---|---|---|---|---|---|
| `translator::run` / `ltl_to_tgba_fm` | pinned `2.15.1.dev` | Simplification, construction and postprocessing return a complete explicit graph. FM drains `formulas_to_translate`, allocating states and edges. | Reads existing graph storage; no saved factory work. | FM complements promise marks and fixes generalized Büchi after exhausting its worklist; translator can subsequently change acceptance during postprocessing. | Calling either graph factory already materializes. | **Accept as eager reference.** No row-level operation on `translator::run`; the separate FM explorer below is public. | `spot/twaalgos/translate.cc:760-775,795-805,936-960`; `spot/twaalgos/ltl2tgba_fm.cc:2398-2450,2477-2480`; postprocessing at `spot/twaalgos/translate.cc:455-458` |
| `ltl_to_taa` | pinned `2.15.1.dev` | Unabbreviation and negative normal form; recursively constructs explicit alternating states, transitions, destination sets, BDD guards and acceptance inventory; installs initial skeleton set. | The returned object's generic TGBA interface combines skeleton transitions for the requested set-state. | Generalized Büchi fixed before return. | `make_twa_graph(const_twa_ptr, ...)` later enumerates the reachable TGBA. | **Accept as P5 LTL candidate**, subject to normalization and future tests. | `spot/twaalgos/ltl2taa.cc:395-409,145-235,243-305,340-389`; `spot/twa/taatgba.hh:174-218,269-291` |
| `taa_tgba::get_init_state`, `succ_iter`, `taa_succ_iterator` | pinned `2.15.1.dev` | Retains the eager alternating skeleton described above. | Initial request only wraps `init_`; `succ_iter` constructs a whole TGBA row by Cartesian combination, BDD conjunction, pruning and merging; `first`/`next` traverse that completed row. | Iterator holds a const acceptance reference; `acc()` complements stored marks against that fixed inventory. | Generic graph copy traverses successive set-states. | **Yes, reachable TGBA combination is deferred**, with row-sized work at iterator acquisition. | `spot/twa/taatgba.cc:52-64,124-231,249-287` |
| `twa_product` / `otf_product` | pinned `2.15.1.dev` | Checks shared dictionary, retains factors, creates pool metadata, copies AP registrations and composes acceptance. Does not enumerate pairs or factor rows. | Initial request allocates one pair; iterator reads factor rows and lazily tries edge pairs, skips false guard intersections, and creates a destination pair on `dst()`. | Right acceptance indices shift by left set count; conditions conjoin and marks union after the same shift. | Generic `make_twa_graph` enumerates reachable pairs. | **Accept for provider/ownership tests** and deferred product construction; not arbitrary lazy LTL translation. | `spot/twa/twaproduct.hh:79-94,136-141`; `spot/twa/twaproduct.cc:278-313,323-354,129-134,162-204` |
| `copy` / `make_twa_graph` | pinned `2.15.1.dev` | Empty-graph overload only allocates a graph; generic provider overload invokes the local implementation `copy`. Graph input can take a direct graph-copy fast path. | Generic copy drains a deque and consumes each complete successor row, interns destinations semantically, and adds graph edges. | Copies acceptance before traversal; provider must already have its final acceptance inventory. | **Use `make_twa_graph(aut, twa::prop_set::all())` with static type `const_twa_ptr`.** It fully materializes TAA/product. `copy` is an anonymous-namespace function, not a public header API. | **Accept for eager control**; never equate it with lazy generation. | `spot/twa/twagraph.hh:855-900`; `spot/twa/twagraph.cc:1603-1623,1654-1679,1693-1752` |
| `degeneralize` / `degeneralize_tba` | pinned `2.15.1.dev` | Requires an existing graph, normally builds an explicit graph of original-state/acceptance-level pairs, and may inspect SCCs. | No generic lazy row interface here. | First produces state-based ordinary (co)Büchi, second transition-based ordinary (co)Büchi. Already suitable inputs can be returned unchanged. | A generic provider must be materialized **before** it can be passed; nontrivial degeneralization itself drains a graph worklist. | **Reject in a lazy path**; accept as a later eager normalization oracle. | `spot/twaalgos/degen.hh:68-99`; `spot/twaalgos/degen.cc:324-355,435-443,488-539,744-774` |
| `twa` helpers / dictionary | pinned `2.15.1.dev` | Provider owns dictionary registrations and may retain iterator storage. | Semantic equality through `compare`/`hash`; each returned state is owned; `release_iter` can cache its iterator. | Guards need live BDD handles and registrations; transition marks do not establish a state-based rank convention. | State/iterator helpers alone do not materialize. | **Required contract** for any later consumer. | `spot/twa/twa.hh:51-108,153-204,208-252,485-501,708-718,741-762`; `spot/twa/twa.cc:37-50`; `spot/twa/bdddict.hh:44-63` |
| Additional public `ltl_to_tgba_fm_otf` | pinned `2.15.1.dev` | Initializes simplifiers/dictionaries and normally canonicalizes the initial formula by translating its symbolic successors. Does not drain the graph worklist. | Public `succ_as_bdd` / `succ_as_edges` compute formula-state successors, including destination canonicalization and promise allocation. | **Can grow during exploration; returned marks use negated-Inf semantics.** Complementation against the final number of sets is deferred until exploration ends. | `ltl_to_tgba_fm` is its eager worklist consumer. | **Reject as a ready fixed-acceptance `twa` provider.** Public row access exists, but this class is not a `twa` and its acceptance contract needs additional design. Not executed by this probe. | `spot/twaalgos/ltl2tgba_fm.hh:120-164,186-248`; `spot/twaalgos/ltl2tgba_fm.cc:2003-2110,2131-2158,2424-2450`; initial canonicalization at `spot/twaalgos/ltl2tgba_fm.cc:1889-1903` |

### What “All allocated state sets” actually owns

`state_set_vec_` is a vector of pointers to destination sets in the **alternating
skeleton** and its initial set. `create_transition` calls `add_state` and
`add_state_set`; the latter allocates a set, populates it with skeleton-state
pointers and appends it to that vector. `set_init_state` also calls
`add_state_set`. This happens during the recursive factory visitor
(`spot/twa/taatgba.hh:64-72,174-199,269-291`;
`spot/twaalgos/ltl2taa.cc:44-48,313-318,395-409`).

The vector is **not an arena of all reachable TGBA combinations**. The source
makes that distinction unambiguous: `taa_succ_iterator` allocates its own
destination combinations and transitions into `succ_`/`seen_`, deletes them in
its destructor, and `dst()` returns an independently owned copy of a destination
set. It does not append to `state_set_vec_`
(`spot/twa/taatgba.cc:124-231,233-247,269-274`). The skeleton sets live until
`taa_tgba` destruction (`spot/twa/taatgba.cc:38-44`); skeleton states and
transitions are deleted by the labelled subclass
(`spot/twa/taatgba.hh:164-171`).

Nor is the factory merely linear formula bookkeeping. Binary release rules
combine child successor lists, and conjunction uses `all_n_tuples` to enumerate
their Cartesian product before returning
(`spot/twaalgos/ltl2taa.cc:197-214,243-294,340-389`). With refined rules enabled,
language-containment checks add work (`spot/twaalgos/ltl2taa.cc:162-163,194-195`).
This probe explicitly uses the default **false** setting.

Initial-state access allocates a `set_state` wrapping the existing `init_`, with
no successor construction. The entire next row is built by the iterator
constructor, including an accepting true self-loop for the empty conjunction,
Cartesian combinations, guard conjunction/pruning, and transition merging
(`spot/twa/taatgba.cc:52-64,124-231`). An abandoned TAA iterator has therefore
already paid for its whole row. There is no persistent complete-row cache in
this provider: `succ_iter` always constructs a new iterator; the base
`release_iter` cache can retain an old iterator, but TAA does not reuse it
(`spot/twa/taatgba.cc:59-64`; `spot/twa/twa.hh:708-718`).

### Supported operators and the acceptance boundary

The factory first applies `negative_normal_form(unabbreviate(f, "^ieFG"))`.
Thus the visitor's unimplemented direct `F`/`G`, XOR, implication and equivalence
cases do **not** mean those LTL inputs are unsupported. They are rewritten
before visiting. The implemented core handles constants, APs, negated APs,
`X`/`strong_X`, `U`, `W`, `R`, `M`, conjunction and disjunction
(`spot/twaalgos/ltl2taa.cc:52-140,395-409`). PSL/SERE operators such as closure,
star and concatenation, quantifiers and the empty-word operator reach
unimplemented cases if they survive rewriting
(`spot/twaalgos/ltl2taa.cc:68-69,110-129`). Do not infer general PSL support from
a PSL spelling that the formula constructors simplify to LTL.

TAA acceptance sets are allocated eagerly by
`taa_tgba_labelled::add_acceptance_condition`; the factory finally sets the
generalized-Büchi formula. The iterator reads a const `acc_cond&` and complements
its stored promise marks on access; it never adds a set
(`spot/twa/taatgba.hh:211-218,149-152`;
`spot/twaalgos/ltl2taa.cc:408-409`; `spot/twa/taatgba.cc:124-126,283-287`).
The materialized view is an existential TGBA even though its private skeleton
is alternating. Zero acceptance sets mean `t`, not an ordinary one-set Büchi
encoding. The empty language can still have a single initial state and no
edges, as the `false` cases show.

In contrast, the FM explorer documents changing acceptance and negated marks
explicitly (`spot/twaalgos/ltl2tgba_fm.hh:134-163`). Its default initial
canonicalization already computes a symbolic row, and later translation can
allocate more colors (`spot/twaalgos/ltl2tgba_fm.cc:337-346,1889-1903,2106-2110`).
Its public API does not document a standalone operation that freezes a complete
promise-to-color inventory before exposing positive-Inf TGBA rows. Optional
fair-loop approximation or unobservable-event modes do pre-register promises
in the constructor (`spot/twaalgos/ltl2tgba_fm.cc:2048-2064`); those modes also
change translation behavior and were not validated here as a general freezing
contract. A future FM experiment would need to establish that contract, a
`twa` view and tests; it is not needed to finish P0 and was not implemented here.

## 3. Probe protocol and ownership

`src/research/spot_otf_probe.cc` follows `forward_game_replay.cc`'s hand-written
long-option parser, `fail`, `need_argument`, `usage`, TSV header and top-level
exception handling. Meson registers `acacia-spot-otf-probe` directly inside
`build_research_tools`, with only `[spot_dep, bddx_dep]`. It includes no Acacia
solver or `utils/verbose.hh`, so it requires neither Posets/stdsimd nor the
`utils::verbose`/`utils::vout` definitions.

Each formula/provider has three separate forked runs. They share no constructed
automata or dictionaries with one another. Parsing and dictionary creation
precede the factory measurement. No translation warm-up is performed.

1. **Partial:** measure the factory; record acceptance and any public explicit
   graph state count; request only the initial state; consume exactly its
   complete outgoing row; retain the first destination in provider iteration
   order and consume exactly its row. A self-loop destination is allowed.
   `false` has no destination, so its destination timing is `NA`.
2. **Full:** construct a fresh provider (separately report `full_factory_ms`),
   then call generic `make_twa_graph`. The wall-time and address-space caps
   cover the whole child, including factory and language check. Successful
   enumeration produces a complete graph; a killed/failed run has missing
   metrics and cannot pass language validation. The translator reference takes
   Spot's graph-copy fast path, explicitly labelled in `status`.
3. **Ownership:** construct a fresh provider and test cloned initial states,
   repeated `dst()`/clones using `compare` and `hash`, release after an early
   exit, release during an injected C++ exception, then complete a row after
   those release paths. Report success only after provider/dictionary teardown.

The reference is ordinary default `spot::translator{dict}.run(f)`:
TGBA/Small/High (`spot/twaalgos/postproc.hh:258-260`). It is a language and eager
construction reference, not Acacia's entire preprocessing pipeline.
Acacia's `create_automaton` normally requests BA/SBAcc, or Buchi for transition
acceptance (`src/solver/create_automaton.hh:18-54`). Nothing in the TAA factory
is routed through that function, `translator::run` or a postprocessor.

Product factors are two **independently translated** copies of the same formula,
sharing one dictionary. Consequently their intersection still recognizes that
formula, and all three providers use the same reference language. The default
factors have at most four states each. Factor translation is timed separately
as `factor_factory_ms` before timing `otf_product`; `factor_states` records both
sizes. The duplicated nontrivial acceptance sets exercise index shifting: for
`GF a & GF b` each factor has two sets and the product has four.

Every initial/destination/clone owned by the probe uses a `unique_ptr` deleter
calling `state::destroy()`. Every acquired iterator uses a deleter calling its
provider's `release_iter()` on completion, early exit and C++ exception. Both
copies returned by repeated `dst()` calls are destroyed exactly once; the first
retained destination remains owned after the first iterator is released. States
are never keyed by raw pointers or `format_state`. The full graph builder uses
Spot's semantic `state_map` and its own ownership implementation. The separate
ownership check expects three released consumer iterators and no live consumer
states. These counters verify **consumer cleanup**, not factory laziness or
the absence of all internal library leaks. Process termination by a signal
necessarily relies on OS cleanup rather than C++ unwinding.

For a later interner, `state_unicity_table` consumes the supplied state,
destroys duplicates and owns canonical states until table destruction;
`state_map` supplies semantic hash/equality but leaves state destruction to
the caller (`spot/twa/twa.hh:191-252`). Protect a not-yet-transferred state if
allocation can throw. Keep the provider alive until all its states, guards,
iterators and registrations are released. `register_ap` registers both AP
metadata and dictionary ownership; `twa::~twa` unregisters its variables
(`spot/twa/twa.hh:741-762`; `spot/twa/twa.cc:45-50`). Owning the dictionary alone
does not substitute for those registrations.

One source-level cap trap is deliberately avoided: the pinned generic graph
copy's `max_states` branch calls `seen.find(t->dst())` without releasing that
temporary destination, and can create more than the requested number before
marking incomplete states (`spot/twa/twagraph.cc:1703-1720`). This is a static
finding, not an executed leak test. The probe uses process limits around the
uncapped complete-copy operation, never the truncated-display overload.

### Measurement definitions

- Times are milliseconds from `steady_clock`, one observation each. Row times
  include iterator acquisition, complete consumption, duplicate-destination
  equality checks and iterator release. Full materialization time excludes its
  fresh factory and the subsequent language check.
- `factory_peak_bytes` is Linux `getrusage(RUSAGE_SELF).ru_maxrss * 1024`
  sampled immediately at factory return. It is the **absolute process RSS
  high-water mark through return**, not a resettable factory allocation peak.
  `factory_baseline_peak_bytes` and `/proc/self/statm` RSS before/after are
  separate columns. They include runtime overhead, shared-library pages, BDD
  infrastructure and, for products, the prebuilt factors. The two Linux RSS
  interfaces need not report identical accounting. Their differences do not
  measure attributable heap allocations. Every row explicitly labels the
  high-water interpretation; no unavailable allocation peak is invented.
- `states_after_factory` is the explicit graph's `num_states()`, or `NA` for
  TAA/product. There is no public numeric factory-state-count accessor for
  these providers; their status says so. Source inspection, not a zero count
  or a consumer counter, establishes what their factories construct.
- Acceptance cells are `number_of_sets:acceptance_formula`. All partial
  checkpoints, both fresh factories, the fully explored provider and the
  materialized graph are compared. `acceptance_changed=unknown` is retained
  when a checkpoint is unavailable; it is never interpreted as stability.
- Language validation calls `spot::are_equivalent(materialized, reference)`
  after timing enumeration. This is an automata-language comparison, not a
  finite-word sample. For the translator row it is a consistency control using
  the same library translator, not an independent proof of that translator.
- Exit 0 means all requested checks completed and passed; exit 2 retains a
  provider error, signal, cap, language mismatch or unknown/changed acceptance;
  exit 1 is a driver/CLI error. Each child reports its current stage before
  risky work, so an exception/signal preserves completed metrics and later
  providers/formulas still run.

## 4. Measured evidence

### Required eight formulas: actual complete TSV output

Command: `/tmp/otf-p0/src/acacia-spot-otf-probe`. Exit **0**, 24 data rows;
stderr empty. These are the measured values, not predictions or reconstructed
factory-work counts. The tiny, unreplicated timings are API cost observations;
they are not a benchmark speedup claim.

```tsv
formula	provider	factory_ms	factory_peak_bytes	states_after_factory	init_only_ms	first_row_ms	first_row_edges	one_destination_row_ms	full_enumeration_ms	full_enumeration_states	full_enumeration_edges	acceptance_before	acceptance_after	acceptance_changed	language_check	status	acceptance_after_init	acceptance_after_first_row	acceptance_after_destination	full_acceptance_before	materialized_acceptance	one_destination_row_edges	factory_baseline_peak_bytes	factory_rss_before_bytes	factory_rss_after_bytes	factor_factory_ms	factor_states	full_factory_ms	ownership_check	consumer_iterators_released	ownership_iterators_released
true	translator	0.180026	16166912	1	0.000553	0.002491	1	0.000703	0.001527	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	0:t	0:t	0:t	0:t	0:t	1	13139968	13811712	16392192	NA	NA	0.086860	ok	2	3
true	taa	0.034870	14274560	NA	0.000445	0.008137	1	0.005506	0.027441	2	2	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	13082624	13824000	14557184	NA	NA	0.041016	ok	2	3
true	product	0.006894	16543744	NA	0.000378	0.002307	1	0.001050	0.004275	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	16281600	16764928	16769024	0.096113	1+1	0.011106	ok	2	3
false	translator	0.105308	16113664	1	0.000357	0.000958	0	NA	0.001091	1	0	0:t	0:t	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy;no_destination	0:t	0:t	0:t	0:t	0:t	NA	13086720	13828096	16334848	NA	NA	0.107892	ok	1	3
false	taa	0.033380	14278656	NA	0.000411	0.001800	0	NA	0.012011	1	0	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable;no_destination	0:t	0:t	0:t	0:t	0:t	NA	13086720	13828096	14561280	NA	NA	0.020446	ok	1	3
false	product	0.007485	16412672	NA	0.000431	0.001016	0	NA	0.002239	1	0	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable;no_destination	0:t	0:t	0:t	0:t	0:t	NA	16150528	16699392	16703488	0.136384	1+1	0.007696	ok	1	3
F a	translator	0.188481	16547840	2	0.000242	0.001983	2	0.000686	0.000838	2	3	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1	13389824	14061568	16846848	NA	NA	0.139875	ok	2	3
F a	taa	0.021945	14581760	NA	0.000256	0.005986	2	0.001959	0.012790	2	3	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13389824	14061568	14794752	NA	NA	0.023260	ok	2	3
F a	product	0.008997	16941056	NA	0.004206	0.004508	2	0.001584	0.007249	2	3	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	1	16678912	17178624	17182720	0.272571	2+2	0.014374	ok	2	3
G a	translator	0.253091	16551936	1	0.000501	0.003053	1	0.001307	0.001563	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	0:t	0:t	0:t	0:t	0:t	1	13393920	14065664	16842752	NA	NA	0.245269	ok	2	3
G a	taa	0.032879	14585856	NA	0.000568	0.007411	1	0.002748	0.011399	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	13393920	14065664	14798848	NA	NA	0.021678	ok	2	3
G a	product	0.006461	16945152	NA	0.000325	0.002296	1	0.001125	0.003078	1	1	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	16683008	17170432	17174528	0.161822	1+1	0.007659	ok	2	3
GF a	translator	0.205339	16945152	1	0.000256	0.002100	2	0.001056	0.000848	1	2	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13393920	14069760	17182720	NA	NA	0.190478	ok	2	3
GF a	taa	0.023563	14585856	NA	0.000242	0.005363	2	0.003833	0.013940	2	5	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	3	13393920	14069760	14802944	NA	NA	0.023813	ok	2	3
GF a	product	0.006116	17207296	NA	0.000311	0.003023	2	0.001742	0.002778	1	2	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2	16945152	17510400	17514496	0.256107	1+1	0.006078	ok	2	3
GF a & GF b	translator	0.425252	16945152	1	0.000252	0.002664	4	0.001631	0.000929	1	4	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	4	13393920	14069760	17215488	NA	NA	0.398376	ok	2	3
GF a & GF b	taa	0.034797	14585856	NA	0.000272	0.007504	4	0.004545	0.032417	5	29	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	4	13393920	14069760	14868480	NA	NA	0.035016	ok	2	3
GF a & GF b	product	0.006292	17207296	NA	0.000341	0.005623	4	0.003093	0.004243	1	4	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4:Inf(0)&Inf(1)&Inf(2)&Inf(3)	4	16945152	17547264	17551360	0.608184	1+1	0.005930	ok	2	3
G(a -> F b)	translator	0.258139	16949248	2	0.000264	0.002258	2	0.001107	0.001778	2	4	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	17211392	NA	NA	0.440260	ok	2	3
G(a -> F b)	taa	0.042907	14589952	NA	0.003715	0.008950	2	0.003419	0.027643	2	5	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	14807040	NA	NA	0.052336	ok	2	3
G(a -> F b)	product	0.006156	17211392	NA	0.000314	0.003418	2	0.002104	0.004276	2	4	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2	16949248	17539072	17543168	0.331305	2+2	0.006155	ok	2	3
(a U b) | G c	translator	0.327741	16818176	4	0.000233	0.002478	4	0.000978	0.001149	4	8	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	16986112	NA	NA	0.311600	ok	2	3
(a U b) | G c	taa	0.030667	14589952	NA	0.000349	0.006336	3	0.002078	0.012537	4	7	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	2	13398016	14073856	14811136	NA	NA	0.029280	ok	2	3
(a U b) | G c	product	0.006377	17080320	NA	0.000309	0.004555	4	0.001642	0.005431	4	8	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2	16818176	17313792	17317888	0.454857	4+4	0.005876	ok	2	3
```

All acceptance checkpoints agree. Partial runs release two iterators, except
`false`, which releases one; each separate ownership run releases three.
`true` under TAA has an initial singleton and an empty-set accepting sink,
explaining its two states versus the optimized reference's one. TAA may also
retain duplicate skeleton transitions from recursive visits; neither factory
nor fully enumerated size should be assumed minimal.

### Operator coverage beyond the default list

The repeated `--formula` inputs below exercise next, weak until, release,
strong release, Boolean equivalence, negated until and a worker-shaped
implication/next/release nesting. The latter is an AP-renamed clause shape from
`tests/ltl/realizable/OneCounterGuiA9.ltl:1`, not a full worker run. That input
contains `G(disable -> X(enable R !pressed))`; other supplied formulas already
cover its Boolean operators. The simple PSL spelling `{a}<>-> b` normalizes to
a Boolean formula and is therefore not evidence for general PSL support.

One invocation passed the eight inputs using repeated `--formula` flags,
exit **0**. The following projection preserves measured state/edge counts and
check outcomes for every provider (times remain in the temporary run TSV):

| Formula | Provider | Full states | Full edges | Acceptance | Language | Acceptance changed | Ownership |
|---|---|---:|---:|---|---|---|---|
| X a | translator | 3 | 3 | 0:t | pass | no | ok |
| X a | taa | 3 | 3 | 0:t | pass | no | ok |
| X a | product | 3 | 3 | 0:t | pass | no | ok |
| a W b | translator | 2 | 3 | 0:t | pass | no | ok |
| a W b | taa | 2 | 3 | 0:t | pass | no | ok |
| a W b | product | 2 | 3 | 0:t | pass | no | ok |
| a R b | translator | 2 | 3 | 0:t | pass | no | ok |
| a R b | taa | 2 | 3 | 0:t | pass | no | ok |
| a R b | product | 2 | 3 | 0:t | pass | no | ok |
| a M b | translator | 2 | 3 | 1:Inf(0) | pass | no | ok |
| a M b | taa | 2 | 3 | 1:Inf(0) | pass | no | ok |
| a M b | product | 2 | 3 | 2:Inf(0)&Inf(1) | pass | no | ok |
| a <-> b | translator | 2 | 2 | 0:t | pass | no | ok |
| a <-> b | taa | 2 | 2 | 0:t | pass | no | ok |
| a <-> b | product | 2 | 2 | 0:t | pass | no | ok |
| !(a U b) | translator | 2 | 3 | 0:t | pass | no | ok |
| !(a U b) | taa | 2 | 3 | 0:t | pass | no | ok |
| !(a U b) | product | 2 | 3 | 0:t | pass | no | ok |
| G(a -> X(b R !c)) | translator | 2 | 4 | 0:t | pass | no | ok |
| G(a -> X(b R !c)) | taa | 2 | 4 | 0:t | pass | no | ok |
| G(a -> X(b R !c)) | product | 2 | 4 | 0:t | pass | no | ok |
| {a}<>-> b | translator | 2 | 2 | 0:t | pass | no | ok |
| {a}<>-> b | taa | 2 | 2 | 0:t | pass | no | ok |
| {a}<>-> b | product | 2 | 2 | 0:t | pass | no | ok |

### Unsupported provider input is a finding

Command: `/tmp/otf-p0/src/acacia-spot-otf-probe --formula '{a[*];b}<>-> c'`.
Exit **2**. TAA throws `std::runtime_error("unimplemented")` during all three
factories, while translator and product complete and pass. No crash occurred
on the required eight cases. This unsupported PSL case was preserved rather
than filtered from the results.

```tsv
formula	provider	factory_ms	factory_peak_bytes	states_after_factory	init_only_ms	first_row_ms	first_row_edges	one_destination_row_ms	full_enumeration_ms	full_enumeration_states	full_enumeration_edges	acceptance_before	acceptance_after	acceptance_changed	language_check	status	acceptance_after_init	acceptance_after_first_row	acceptance_after_destination	full_acceptance_before	materialized_acceptance	one_destination_row_edges	factory_baseline_peak_bytes	factory_rss_before_bytes	factory_rss_after_bytes	factor_factory_ms	factor_states	full_factory_ms	ownership_check	consumer_iterators_released	ownership_iterators_released
{a[*];b}<>-> c	translator	0.208277	16703488	2	0.000213	0.002019	2	0.000645	0.001153	2	3	1:Inf(0)	1:Inf(0)	no	pass	ok;peak_is_process_hwm;materialization_is_graph_copy	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1:Inf(0)	1	13414400	14061568	16949248	NA	NA	0.241896	ok	2	3
{a[*];b}<>-> c	taa	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	unknown	NA	partial:unimplemented@factory;full:unimplemented@factory;ownership:unimplemented@factory;acceptance_unknown;language_NA;peak_is_process_hwm;factory_state_count_unobservable	NA	NA	NA	NA	NA	NA	13422592	14073856	NA	NA	NA	NA	unimplemented@factory	NA	NA
{a[*];b}<>-> c	product	0.006272	16973824	NA	0.000289	0.003156	2	0.001085	0.004552	2	3	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	2:Inf(0)&Inf(1)	1	16711680	17219584	17223680	0.268936	2+2	0.006255	ok	2	3
```

### Factory cap and continuation check

Generated `F p0 & ... & F p19`, passed it with `--provider taa
--timeout-seconds 1`, followed by a second `--formula true`. Exit **2**. Each
large-formula phase ended with `SIGALRM` (14) at `factory`; the subsequent
`true` case passed. This directly demonstrates substantial work before any
consumer successor request, and verifies that an unusable case does not erase
later evidence. Missing factory-return values remain `NA`.

```tsv
formula	provider	factory_ms	factory_peak_bytes	states_after_factory	init_only_ms	first_row_ms	first_row_edges	one_destination_row_ms	full_enumeration_ms	full_enumeration_states	full_enumeration_edges	acceptance_before	acceptance_after	acceptance_changed	language_check	status	acceptance_after_init	acceptance_after_first_row	acceptance_after_destination	full_acceptance_before	materialized_acceptance	one_destination_row_edges	factory_baseline_peak_bytes	factory_rss_before_bytes	factory_rss_after_bytes	factor_factory_ms	factor_states	full_factory_ms	ownership_check	consumer_iterators_released	ownership_iterators_released
F p0 & F p1 & F p2 & F p3 & F p4 & F p5 & F p6 & F p7 & F p8 & F p9 & F p10 & F p11 & F p12 & F p13 & F p14 & F p15 & F p16 & F p17 & F p18 & F p19	taa	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	NA	unknown	NA	partial:signal_14@factory;full:signal_14@factory;ownership:signal_14@factory;acceptance_unknown;language_NA;peak_is_process_hwm;factory_state_count_unobservable	NA	NA	NA	NA	NA	NA	13238272	13918208	NA	NA	NA	NA	signal_14@factory	NA	NA
true	taa	0.025066	14237696	NA	0.000668	0.008731	1	0.001332	0.020158	2	2	0:t	0:t	no	pass	ok;peak_is_process_hwm;factory_state_count_unobservable	0:t	0:t	0:t	0:t	0:t	1	13045760	13778944	14512128	NA	NA	0.018195	ok	2	3
```

## 5. Options and next-stage boundaries

### a. Accept — public TAA/TGBA candidate (verdict A)

Accept direct `ltl_to_taa(..., false)` for the P5 LTL experiment. Source proves
deferred reachable set-state combination, and the required plus additional LTL
checks pass language/acceptance/ownership validation. Retain the eager
alternating skeleton and charge its cost to both C4 and C5. A future complete-row
cache can avoid repeated row construction, but no cache or adapter was built
in P0. The PSL failure and exponential-looking conjunction work are explicit
admission limits, not reasons to hide successful LTL evidence.

Before an ordinary bounded-rank consumer can use this TGBA, P5 must validate its
Büchi cursor normalization, including zero sets, multiple sets, marks that
discharge several obligations on one edge, initial cursor/rank, acceptance on
transitions versus destinations, and language equivalence with eager
degeneralization. P2's state/row ownership and successor oracle tests and P6's
same-construction eager/on-demand comparisons remain necessary. Test larger
and nested worker formulas before making any corpus coverage claim.

### b. Reject as the formula-generation choice; accept product as a test provider

`twa_product` is a real public lazy provider and passes all small-factor tests.
Accept it for generic-interface tests and experiments whose proposed saving is
specifically deferred **product** construction. Reject using those timings to
claim deferred translation of an arbitrary formula: its factors were eagerly
translated and their cost is separately visible.

The additional public FM explorer is also rejected as a ready substitute for
the fixed-acceptance `twa` view. Its missing contract is supported acceptance
inventory freezing/positive-Inf row semantics before consumption, not merely
a public successor method. No verdict-B formula provider was established by
executing it.

### c. Reject — an internal adapter as a prerequisite to this sprint

The tested public TAA route means P0 does not require opening Spot internals,
duplicating FM translation or proposing verdict C as the only feasible path.
If TAA later fails the generation experiment, the public FM explorer at
`2ae6210237` merits a separate acceptance-freezing design study. That study must
name the retained eager normalization/canonicalization work, preserve AP and
promise registrations, and test acceptance stability and language equivalence
under interrupted exploration. Its feasibility is not established here.

### d. Reject — “no suitable public provider” for this P0 evidence

The executable LTL TAA results and source-backed deferral justify A rather
than D. D would still be a successful audit outcome if subsequent required
admission tests eliminate the candidate. P1–P4 do not depend on which
generation provider survives. Do not make their implementation conditional on
a translation speedup, add a new TLSF tableau, or migrate BDD libraries.

## 6. Validation and scope

Executed:

```sh
python3 scripts/acacia-config.py validate
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig meson setup /tmp/otf-p0 -Dbuild_research_tools=true
meson compile -C /tmp/otf-p0
/tmp/otf-p0/src/acacia-spot-otf-probe
meson test -C build --suite=unit --suite=version
```

Setup and the final full research-tools compile succeeded. The first compile
exposed two probe C++ errors (const parser error-formatting and an initializer
list's pointer constness); both were corrected before any reported probe run.
The probe builds with TLSF frontend and diagnostics **disabled**, confirming
that its Meson target is not gated by either option. The required tests report:

```text
Ok:                33
Fail:              0
```

This includes `version-output`, not just unit suites. Additional probe
invocations are recorded above. CLI checks returned 0 for `--help` and 1 with
the program-name prefix for missing `--formula`, zero `--timeout-seconds`, and
an unknown provider. No benchmark suites were run and no commits were made.

The change consists only of `src/research/spot_otf_probe.cc`, this audit, and
the target registration in `src/meson.build`. No solver, translator, adapter,
new test file or sprint-report file was introduced. `otf.md` was an existing
untracked specification and was not changed.

## Bottom line

The alternating skeleton is eager; reachable TGBA set-state/row construction
is lazy at row acquisition. The measured factory timeout prevents treating
this as cost-free formula generation. Fixed acceptance and language equivalence
hold for the tested LTL cases; PSL repetition is outside the observed working
scope. Use the public route for P5 research with a validated ordinary Büchi
view, retain identical eager controls in P6, and proceed independently with
P1–P4. No production performance claim follows from this audit.

**Verdict A — the public TAA/TGBA provider works on the audited LTL cases and
defers relevant reachable construction.**
