# 07 — Stage C review fixes

Apply `07-review-fixes.patch` after patches 00–06. It contains the production,
test, and design-document changes; it also adds the informal measurement and
summary scripts to a clean replay tree. The patch was checked with `git apply
--check` on a temporary replay of 00–06. Nothing was staged or committed.

The default lifted move relation now uses the exact target game's transitions
and the lifted invariant and ranks. Learned moves are disabled by a single
global setting; when enabled, any learning or instantiation decline stops the
lift attempt. Evidence records `move_source`. Unknown attempts carry
`attempted-declined`, including in the wrapper and the corrected informal TSV
view. The frozen raw sample remains unchanged.

The guard rejects the three reviewer mutants and further literal spelling,
size, and formula branches. Generated name, size, and formula-format variants
exercise the runtime invariance rule. Direct solver/checker, BuDDy, wrapper,
and lifting cost settings are declared together in `lifting/settings.py` and
documented in the route README.
