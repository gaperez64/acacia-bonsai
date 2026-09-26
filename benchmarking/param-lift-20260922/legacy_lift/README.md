# Dated M-series implementation

This package is the withdrawn, family-specific experiment implementation.
The dated `param-lift-campaign.py` and `generalize_gr1.py` entry points import
it to reproduce historical evidence. It is not imported by
`benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py` or `acacia_lift.runner`.

The registry and pinned TLSF files were moved here verbatim from the former
production package. `legacy_source()` resolves their original manifest paths
to this directory so the historical template hashes remain valid. The four
production pytest files that asserted the withdrawn route are archived in
`../legacy_tests/`.
