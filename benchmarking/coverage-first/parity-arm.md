# Deterministic automaton and parity arm

`both:parity:spot` is part of the first `docker_default` portfolio. It translates the
original positive specification with Spot's deterministic, generic-acceptance,
high-level translation through `src/solver/parity/route.{hh,cc}`.
The declared input/output partition, including unused signals,
is preserved. Raw LTL is Mealy; native TLSF uses the frontend's effective-Mealy
lowering once and the ordinary controller conversion for a Moore target. No
realizability simplification, decomposition, input/output swap or extra timing
shift precedes translation.

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
deadline. There is no local wall allowance or solver-visible deadline. The worker
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
adds this arm to the previous shipped portfolio, retained second in the group.
