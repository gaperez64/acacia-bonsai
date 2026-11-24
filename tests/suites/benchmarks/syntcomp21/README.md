# SYNTCOMP 2021 provenance

Logical names resolve through `sources.tsv` into the shared content-addressed
[corpus](../../../ltl/syntcomp/README.md), fixing formula/partition bytes.
Timing buckets came from Strix/`ltlsynt` StarExec job 40520 in commit
`1659b1362374eef152943257e12284d18f8faef3` (source table
`doc/starexec/Job40520_info.csv`). No upstream archive URL or checksum was recorded;
reproducibility starts with the checked-in pairs.

`crit.list` contains 94 instances that historically took Acacia roughly 1-15 s.
Other lists are runtime/data-structure slices; `all.list` includes them and is
not an independently sampled panel.
