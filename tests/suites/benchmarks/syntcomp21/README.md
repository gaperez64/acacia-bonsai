# SYNTCOMP 2021 benchmark provenance

This is the repository's historical SYNTCOMP-era LTL benchmark suite. The
logical names in the `.list` files resolve through `sources.tsv` into the
shared, content-addressed corpus at `tests/ltl/syntcomp`; the map therefore
fixes the exact formula and partition content used by current runs.

The timing buckets originated in commit `1659b1362374eef152943257e12284d18f8faef3`
from Strix and `ltlsynt` results in StarExec job 40520. That commit retained the
source table as `doc/starexec/Job40520_info.csv` and described the import as
“benchmarks based on Strix and ltlsynt stats on StarExec.” Later commits moved
the selections out of `tests/meson.build` and deduplicated the files without
changing their logical names.

`crit.list` is the 94-instance local critical set selected from targets that
historically took roughly 1–15 seconds in Acacia. `baseline-crit.csv` is a
later measurement of that frozen list and was not used to import the corpus.
The other lists are historical runtime or data-structure slices; `all.list`
is an include-only aggregate, not an independently sampled panel.

Unlike the 2025 and 2026 imports, the 2021 history does not record an official
release archive URL or archive checksum. Reproducibility is consequently at
the checked-in formula/partition level through `sources.tsv`, not at the
original-download level. This limitation is recorded here rather than
inventing missing upstream metadata.
