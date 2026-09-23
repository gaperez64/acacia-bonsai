# M1b: binary-encode monitor states (the `u`-axis blowup)

Work in `/home/gperez/GIT-repos/acacia-bonsai/subprojects/tlsf-tools`, branch `param-lift-gr1`.
**Do not commit and do not stage anything.** Do not touch
`benchmarking/witness-lifting-20260918/`, `build_w1_B`, `build_w1_S` in the parent repository.

## The measurement

`scripts/gr1_monitor_game.py` emits one latch per deterministic-Büchi monitor state — the
provenance records `"latch_encoding": "one-hot"`. For most conjuncts the monitors are tiny and this
costs nothing. For the `*_unreal1` families it is the whole obstruction.

Those families take two parameters. `n` is the interface width; `u` is an X-depth, appearing as
`G(!r_i0 | X(!r_i1 | X^(u-1)(g_i0 & g_i1)))`. Measured on `round_robin_arbiter_unreal1` at n=3:

| u | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| monitors | 16 | 16 | 16 | 16 | 16 | 16 | 16 |
| total latches | 41 | 47 | 59 | 83 | 131 | 227 | 419 |
| largest monitor, states | | | | | 33 | 65 | 129 |

`u` adds no monitors at all — it only deepens one, whose state count is `2^(u-1)+1`. That state
count is inherent: overlapping obligations mean the monitor must remember, for each of the last `u`
steps, whether the trigger fired, i.e. a `u`-bit shift register. **Spending `2^u` latches on `u`
bits of state is not inherent.** At u=7 that is 419 latches where roughly 40 would do, and the
GR(1) solve is a BDD over that many variables.

`round_robin_arbiter_unreal1` has 21 unsolved SYNTCOMP26 instances and `load_balancer_unreal1` has
7 — 28 between them, the largest single block among the DBA-reducible families, and this encoding
is what puts the `u` axis out of reach.

## What to do

Add a state-encoding choice to `gr1_monitor_game.py`:

```
--latch-encoding {one-hot,binary,auto}     default: auto
```

- `binary`: encode a monitor's `S` states in `ceil(log2(S))` latches.
- `auto`: pick per monitor — one-hot below a threshold (start at `S <= 8`, justify whatever you
  choose), binary above it. This is the default because one-hot keeps a small monitor's transition
  relation a simple disjunction, and that is genuinely better for the many tiny monitors; the win is
  only on the large ones.
- Record the choice per monitor in the provenance (`latch_encoding` becomes per-monitor; keep a
  top-level summary field so existing readers do not break, and say in the schema what changed).

The reduction must stay **exact**: the game language is unchanged, only the state encoding differs.

## Verification

- **Equivalence.** For every monitor the two encodings must accept the same language. Check it
  directly on the existing `test/` monitor cases, and differentially: for a seeded set of random
  conjuncts, build the game both ways and confirm `tlsfsolve` returns the same realizability verdict
  on every one. Any disagreement is a bug in the encoding, not a tolerance.
- **Seeds unchanged.** The 11 exact-mode seeds (`arbiter` 2/3/4, `round_robin_arbiter` 2..5,
  `prioritized_arbiter` 2/3, `load_balancer` 2, `lift` 2) must still solve to REALIZABLE and still
  VERIFY under `tlsfcertcheck --method both`.
- **The actual win, measured.** Report latch counts and `tlsfsolve` wall time and peak RSS for
  `round_robin_arbiter_unreal1` at n=3, u=1..10 and `load_balancer_unreal1` at n=2, u=1..10, under
  both encodings, with a per-run timeout. State plainly how far up the `u` axis each encoding gets
  before timing out — that number is the deliverable.
- **No regression on the small monitors.** Compare total solve time across all 11 seeds under
  `auto` versus `one-hot`. If `auto` is slower anywhere, say so and say why rather than adjusting
  the threshold until the table looks good.
- Full suite green, pinned clang-format 18.1.8 clean, `git diff --check` clean.

Every heavy run must be wrapped: `systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0`,
and serialized — never two solver runs at once.

Report: the encoding rule you chose and why, the equivalence evidence, and the two `u`-axis tables.
