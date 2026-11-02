# Deterministic automaton and parity arm

The first `docker_default` portfolio has eight arms, including `both:parity:spot`
and `both:parity:oxidd`. The Spot arm translates the
original positive specification with Spot's deterministic, generic-acceptance,
high-level translation through `src/solver/parity/route.{hh,cc}`.
The declared input/output partition, including unused signals,
is preserved. Raw LTL is Mealy; native TLSF uses the frontend's effective-Mealy
lowering once and the ordinary controller conversion for a Moore target. No
realizability simplification, decomposition, input/output swap or extra timing
shift precedes translation.

The `both:parity:oxidd` arm builds the tlsf-tools DPA from native TLSF.
It tries the proved grammar first. Only an unsupported creation retries with the
general Boolean Π2/Σ2 construction. tlsf-tools validates that construction
offline, on a recorded sample of formulas, against a construction proved in
Lean; formulas outside the sample rely on that evidence and on testing. Both
attempts share one construction deadline;
resource declines do not retry. The completed DPA feeds the exact parity game
and supports controller synthesis.

Issue #361 additionally exposes opt-in `unreal:parity:oxidd-otf` and
`real:parity:oxidd-otf`. These decision-only arms expand complete successor
rows lazily and use Spot's existing exact solver after 1, 2, 4, ... expanded
rows. Frontier self-loops lose for the environment in UNREAL-only mode and
for the system in REAL-only mode. Each arm publishes only its named verdict;
the opposite full-game answer declines as UNKNOWN. Both explicitly decline
`-s` and require native TLSF. The default portfolio remains unchanged pending
measurements. The pre-code reviewed soundness argument is
[on-the-fly-parity-proof.md](on-the-fly-parity-proof.md). `dpa_snapshot` phase
records expose discovered/expanded states and solve counts. No extra helper
worker is created.

`--parity-exploration bfs|strategy` selects discovery order (the default) or
an opt-in order guided by the current optimistic winner's strategy. Both use
the same one-sided completion and exact solver. The reviewed ordering
invariant and diagnostic stopping rule are in
[on-the-fly-parity-guidance.md](on-the-fly-parity-guidance.md). Neither order
can avoid constructing the first complete DPA row.

## Structural tiers and engines

Classify the worker's negated-specification language using acceptance structure alone:

| Tier | Acceptance | Production route |
|---|---|---|
| 1 | Büchi or constant | Keep the automaton unchanged; deterministic-Büchi fast path. |
| 2 | No finite-occurrence acceptance (generalized Büchi) | Degeneralize while preserving determinism; same fast path. |
| 3 | Exactly convertible to deterministic Büchi | Apply Spot's exact determinism-preserving conversion; same fast path. |
| 4 | Genuine parity remainder | Use the positive original automaton, ACD parity conversion and two-step game splitting. |

Tiers 1–3 use **Spot's explicit game solver only**, through
`src/solver/spot_nba_fastpath.hh`: complete the deterministic Büchi automaton,
build the co-Büchi game and solve it with Spot. The explicit Spot solver beats the
symbolic GR(1) route on these tiers; neither symbolic GR(1) nor the
K-bounded antichain search is an alternative production engine for tiers 1–3.
Tier 4 solves the split ACD parity game directly with Spot's `solve_parity_game`.

## Trust, synthesis and supervision

Production trusts completed REAL/UNREAL results and strategies without a post-hoc
recheck. Checks stay in tests: exported AIGs against a fresh negated-specification
translation, Moore output independence and game strategy checks. The registered
`parity-route` and `parity-arm` unit tests exercise verification through the same
production route library.

Decision-only calls do not build controllers. Synthesis uses the existing parent
rendezvous: only a selected REAL worker constructs and writes the controller;
UNREAL never opens the destination, and failed export resumes suspended competitors.
The existing process-group supervisor handles cancellation and the outer invocation
deadline. `both:parity:spot` has no local wall allowance or solver-visible deadline;
`both:parity:oxidd` bounds DPA construction and import together by one 120-second
deadline (or the outer deadline, if earlier) and reports UNKNOWN when it expires. The worker
uses the obligation helper's address-space cap and cgroup-aware soft RSS share
(half the available cap divided by scheduled workers). The default `headroom` mode
(`sharegate-20261028`) enforces the share only when the solver's own memory scope is
nearly full, within an eighth of `memory.max`. Without a readable limit, it behaves
like `--parity-memory-share equal`, which enforces the share at all times.
Resource stops and declines
are never verdicts. Phase records identify tiers, game construction, solving
and trust; missing records remain uncertain.

## Evidence

Developer measurements and admission history live in the evidence archives and
[RESULTS.md](../RESULTS.md): `otd-C-parity-route20261015` (routing),
`otd-parity-engines20261015` (engine comparisons), `otd-gates-OTF20261016` (gates),
and `otd-OTF-owner20261016` (default admission). Resolve the shipped preset through
`python3 scripts/acacia-config.py list-group docker_default`; the parity portfolio
adds both parity arms to the previous shipped portfolio, retained second in the group.
