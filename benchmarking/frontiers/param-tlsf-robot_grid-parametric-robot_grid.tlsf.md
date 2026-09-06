# Frontier dossier: `robot_grid`

- **family key**: `param:tlsf/robot_grid/parametric/robot_grid.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 9
- **frozen targets from this family**: `robot_grid_pb_3_3_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| xN=2, yN=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.26 |
| xN=3, yN=3 | TIMEOUT | TIMEOUT | REALIZABLE | 60.08 |
| xN=4, yN=4 | TIMEOUT | TIMEOUT | REALIZABLE | 60.05 |
| xN=5, yN=1 | TIMEOUT | REALIZABLE | REALIZABLE | 60.09 |
| xN=5, yN=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| xN=6, yN=1 | TIMEOUT | TIMEOUT | REALIZABLE | 60.11 |
| xN=6, yN=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.33 |
| xN=7, yN=7 | MEMOUT | MEMOUT | MEMOUT | 10.36 |
| xN=8, yN=8 | MEMOUT | MEMOUT | MEMOUT | 13.01 |

## Boundary

- largest solved: `robot_grid_pb_5_1_pe_.ltl` (0.07 s)
- first unsolved: `robot_grid_pb_3_3_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `robot_grid_pb_3_3_pe_.ltl` | 10514 | 82 | 721 | 19 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 3: `robot_grid_pb_3_3_pe_.ltl`, `robot_grid_pb_4_4_pe_.ltl`, `robot_grid_pb_6_1_pe_.ltl`
- fails where B or S succeed: 0

## Structural conjecture

**The forward solver extends this family's frontier, and where it stops is informative.**

Observed: `(2,2)` both solve; `(3,3)`, `(4,4)` and `(6,1)` are **forward-only**; `(5,5)` and
`(6,6)` defeat both; `(7,7)` and `(8,8)` exhaust 8 GiB for both.

At `(3,3)` the backward worker reaches **10,514 automaton states with only 82 numeric rank
coordinates and 721 actions per pass**. That is the shape a reachable forward search should
win on, and it does: a large permissive region encoded over few counting coordinates, where
one strategy touches a small part of it.

The conjecture is that the winning strategy on the grid is *local* — the controller only ever
needs the part of the grid near the current position, so the reachable rank set stays small
while the maximal winning region grows with the grid area. Forward stops at `(5,5)`/`(6,6)`
not because the strategy became large but because the number of reachable positions finally
overtook the node budget.

The memory failures at `(7,7)` and `(8,8)` hit **both** solvers, which places them before the
game: the automaton or action construction exhausts 8 GiB before either fixed point starts.
Those are not a statement about backward versus forward at all.

## Next theorem

1. Instrument `forward_env_nodes` at `(4,4)` and `(5,5)`: if the reachable node count grows
   polynomially in the grid area while the backward peak grows exponentially, the locality
   claim is established and the family has a strategy-size theorem.
2. Determine whether `(7,7)`'s memory failure is translation, action materialisation, or the
   first CPre. It is a different bug class from everything else in this dossier and is
   currently attributed to nothing.
3. `(6,1)` is forward-only while `(5,5)` is not, so the frontier is not a function of the area
   alone. Whether the aspect ratio matters is testable on the existing points.
