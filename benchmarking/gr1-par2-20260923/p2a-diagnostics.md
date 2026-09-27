# P2a tiny-instance diagnostic attribution

Both arms used `generalize_gr1.py --diagnostics` on
`prioritized_arbiter`, seeds `3,4`, target `5`, a 180-second per-operation
limit, and `--check-method both`.  The reference arm enabled the retained
two-pass substitution oracle and uncached AAG path with
`GENERALIZE_GR1_REFERENCE_COMPOSE=1` and
`GENERALIZE_GR1_REFERENCE_AAG_CONTEXT=1`; the optimized arm used the P2a
implementation as it stood during attribution.  Both completed with
`VERIFIED`.  After the P2a review fix, native composition is opt-in: build the
adapter with `native/build.py` and pass its path with `--buddy-adapter` to
reproduce the optimized arm.  An unconfigured run explicitly records and
uses `compose_route=two_pass`.

| S0 counter | Reference | P2a | Change |
|---|---:|---:|---:|
| `substitute_variables_calls` | 30 | 30 | same work requests |
| `bdd_compose_calls` | 1,680 | 0 | −1,680 |
| `bdd_veccompose_calls` | 0 | 30 | +30 simultaneous calls |
| `from_aag_calls` | 60 | 3 | −57 traversals |
| `from_aag_gates_traversed` | 1,610 | 232 | −1,378 gate visits |
| `bdd_to_aig_nodes_visited` | 898 | 840 | −58 repeated policy-export visits |

The raw documents are
`build_scratch/p2a/prioritized-reference-diagnostics.json` and
`build_scratch/p2a/prioritized-optimized-diagnostics.json`.  This is
mechanism attribution on one tiny checked instance, not a solver campaign or
a runtime admission claim.
