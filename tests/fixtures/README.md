`native_gr1/round_robin_arbiter.tlsf` is copied verbatim from the SYNTCOMP benchmarks repository (https://github.com/SYNTCOMP/benchmarks), path `tlsf/round_robin_arbiter/parametric/round_robin_arbiter.tlsf`, commit `4105caf1`.

`native_gr1/exhausted-probes.tlsf` reproduces #443: thirteen unused parameters
exhaust the seed-probe budget before the viable axis, then the direct route solves it.

The three `restrictions/` fixtures adapt the [SYNTCOMP benchmarks repository](https://github.com/SYNTCOMP/benchmarks),
commit `4105caf1f1e5fd3b76657879bfce8021d130cbde`, under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
([license text](../syntcomp-benchmarks/LICENSE-CC-BY)):
`recurrence-input.tlsf` derives from `tlsf/ltl2dba/parametric/ltl2dba_theta.tlsf`,
`pair-input.tlsf` from `tlsf/ltl2dba/parametric/ltl2dba_C2_unreal.tlsf`, and
`scalar-output.tlsf` from `tlsf/collector/parametric/collector_v3.tlsf`.
Titles are shortened, descriptions/comments/status trailers removed, formatting
condensed, and each default parameter reduced to 3; the objectives are retained.

`encoded-fold.tlsf` is adapted from the [SYNTCOMP benchmarks repository](https://github.com/SYNTCOMP/benchmarks),
path `tlsf/simple_arbiter_log/parametric/simple_arbiter_enc.tlsf`,
commit `4105caf1f1e5fd3b76657879bfce8021d130cbde`, under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
([license text](../syntcomp-benchmarks/LICENSE-CC-BY)). The title is shortened,
the description and comments are removed, formatting is condensed, `n` is reduced
from 8 to 5, and the grant width changes from `nbits(n+1)` to `nbits(n)`;
the helper definitions and obligations are retained.

`parametric-fold.tlsf` derives from tlsf-tools' `test/fixtures/parametric_fold.tlsf`
at `c47a040`, adapted from the [SYNTCOMP benchmarks repository](https://github.com/SYNTCOMP/benchmarks),
path `tlsf/load_balancer/parametric/load_balancer.tlsf`, commit
`4105caf1f1e5fd3b76657879bfce8021d130cbde`, under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
([license text](../syntcomp-benchmarks/LICENSE-CC-BY)). The default parameter is
changed from `n=2` to `n=6`, trailing whitespace is removed, and the attribution comment
points here; the original description, definitions and obligations are retained.
