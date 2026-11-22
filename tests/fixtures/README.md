`native_gr1/round_robin_arbiter.tlsf` is copied verbatim from the SYNTCOMP benchmarks repository (https://github.com/SYNTCOMP/benchmarks), path `tlsf/round_robin_arbiter/parametric/round_robin_arbiter.tlsf`, commit `4105caf1`.

`native_gr1/exhausted-probes.tlsf` reproduces #443: thirteen unused parameters
exhaust the seed-probe budget before the viable axis, then the direct route solves it.

`encoded-fold.tlsf` is adapted from the [SYNTCOMP benchmarks repository](https://github.com/SYNTCOMP/benchmarks),
path `tlsf/simple_arbiter_log/parametric/simple_arbiter_enc.tlsf`,
commit `4105caf1f1e5fd3b76657879bfce8021d130cbde`, under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The title is shortened,
the description and comments are removed, formatting is condensed, `n` is reduced
from 8 to 5, and the grant width changes from `nbits(n+1)` to `nbits(n)`;
the helper definitions and obligations are retained.

`parametric-fold.tlsf` derives from tlsf-tools' `test/fixtures/parametric_fold.tlsf`
at `c47a040`, adapted from the [SYNTCOMP benchmarks repository](https://github.com/SYNTCOMP/benchmarks),
path `tlsf/load_balancer/parametric/load_balancer.tlsf`, commit
`4105caf1f1e5fd3b76657879bfce8021d130cbde`, under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The default parameter is
changed from `n=2` to `n=6`, trailing whitespace is removed, and the attribution comment
points here; the original description, definitions and obligations are retained.
