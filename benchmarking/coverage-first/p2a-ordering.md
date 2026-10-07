# P2a: structural ordering, experimental

2026-10-07. Acacia parent `e6cd4156dde0fdaea4aaf8a9115b0930ccc7b2b1`,
tlsf-tools parent `f6b5ecc`, with unstaged changes in both worktrees. No defaults,
portfolio membership, caps, or traversal heuristics change. No structural selector
was introduced. Performance admission and #204/#212 closure remain pending.

## Separate mechanisms and existing hooks

| Mechanism | Hook/location | Incumbent source of order |
|---|---|---|
| Legacy frontend formula | `subprojects/tlsf-tools/src/lib/decompose.c:181`, `src/tlsf_frontend.cc:64` | Lowered source AST; Spot later canonicalizes its formula. |
| TLSF IO vectors | `src/tlsf_frontend.cc:43` | Scalars, ordinary buses, enum buses; reverse declaration order within groups, ascending bus coordinates. |
| Raw LTL IO vectors | `src/arg_parser.cc:12`, `src/arg_parser.cc:30` | CLI comma-list order. |
| Native monitor construction | `subprojects/tlsf-tools/src/lib/gr1_reduction.cc:1505`, `:1540`, `:1570` | Declaration-based canonical IO symbols; Spot conjunct order, assumptions before guarantees. Rendered commutative terms are sorted at `:243`. |
| Spot/BuDDy AP order | `src/solver/solver_invoker.cc:344` | `bdd_dict::register_proposition`, inputs then outputs, before translation; caller vectors determine registration order. UNREAL reverses ownership first. |
| MONA IO enumeration | `src/ios_precomputers/mona.hh:65`, `:129`, `:154` | Contiguous input/output/state blocks; inputs low/high, outputs high/low. Semantic quotient keeps first residual occurrence (`semantic_mona.hh:14`). |
| Acacia action enumeration | `src/actioners/standard.hh:34`, `:54` | Lexicographic action vectors in a set, not AP names or BDD IDs. Output list starts in decoded order. |
| Critical picker | `src/input_pickers/critical.hh:33`, `:43`, `:71` | First failing input in the list; successful output is moved to the front. |
| Native candidate levels | `subprojects/tlsf-tools/src/lib/oxidd_order.c:265`, `:336` | Existing input-first, state-first, fanin-DFS, explicit order file; supported `set_var_order` before roots and `level_to_var` verification. |
| Independent checker | `subprojects/tlsf-tools/src/lib/gr1_check.c:1743`, `:1790` | Existing structural policy-size choice; paired current/next state variables. Candidate option is never passed to it. |

Changing declarations can change registration and thus decoded/traversal order.
Changing BDD order can also change Spot's construction indirectly. These are
separate mechanisms, not independent observable effects of a declaration edit.
The runtime experiment changes only the registration/level hook; it leaves formula
text, IO export vectors, monitor construction code, and traversal policy intact.

## Option and fallback

`--var-order incumbent` follows the previous path. `typed-interleaved` sorts by
typed parameter axes, ordered owner coordinates, role, declaration ordinal, local
bit coordinate and logical ID. Shared values remain unowned. `role-grouped` puts
role/declaration before owner coordinates. Names identify metadata records only;
no spelling, prefix, family or corpus information enters either comparison.

Legacy Spot orders stay inside input/output blocks because MONA decodes numeric
block boundaries. Raw LTL without typed metadata retains its original order.
Ambiguous frontend metadata also retains that order. Scalars, representation
bits and buses without a verified parameter axis use unowned local coordinates.
Remaining ties use source declaration/logical IDs; this is not canonical labeling.

Native direct and seed candidate managers use reduction provenance and full level
permutations. Missing typed provenance gives identity; incomplete/conflicting
inventories stop with configuration failure. Ordered monitor bindings retain
owner tuples, while monitor bits and auxiliary counters retain local coordinates.
Old APIs and ABI layouts remain available through versioned wrappers. Schema
instantiation managers for R/U retain their existing layouts. Candidate managers
use functional next-state substitution (`gr1_oxidd.c:1912`), with no separate next
variables to interleave. The independent checker already interleaves current/next
state and remains unchanged.

## Evidence and admission

Four small indexed generated Mealy/Moore specs were observed before editing,
with source, rename-only and declaration-only twins, over all five shipping arms.
All 60 observations retain their verdict/route. Formula bytes are unchanged by
the declaration intervention; IO order changes. Single-run wall times are saved,
without repetitions, fitting or a performance conclusion.

The CLI regression has five generated specs, three twins, three orders and five
arms (225 differential checks), raw-partition controls, invalid-option checks,
and frozen-incumbent byte comparisons. An independent Spot AIGER/LTL product
checks Mealy controllers against their original target formulas exactly. Moore
synthesis checks assert controller/verdict identity across orders; strict pytest
xfails retain the supplied and minimal delay counterexamples under every order.
The inherited Moore semantic defect also reproduces in the frozen incumbent and
is investigated separately in `cov/wt-moore-synth`; no Moore correctness claim
or synthesis fix is made by this package. Native tests independently check
REAL and UNREAL proof artifacts using a fixed checker; compare complete rank truth
tables, variable maps and incumbent bytes; and cover reset/initial conditions,
quantifier cubes, functional next-state maps, shared values and representation bits.

The 22 P2a targets come only from the P1 `package-targets.tsv` rows. Reversing whole
IO declarations changes 18 twins; four single-declaration pairs are unchanged.
All 22 retain frontend formula bytes, IO sets, and timing semantics. Raw evidence,
build pins, exact commands and controls are in the worktree's ignored
`_bm-logs.p2a/REPORT.md` and `_bm-logs.p2a/controls/screen-commands.sh`. Screens are
prepared for 17 s and fresh 60 s, one executable with three options, no deadline
flag. The driver runs these outside the sandbox and must preserve native wins,
adjudicate paired full-race gains/losses, and complete the full-corpus admission.

#212's `collector_v3_pb_9` gap has the separately reported PR #224 equivariance
cause; this package does not claim that ordering fixes it. The other instance is
not identified in local issue snapshots, and network access is prohibited. Its
frontend/order diagnosis remains pending identification and a matched native/raw
pair with timing, worker membership and TLSF hints accounted for.
