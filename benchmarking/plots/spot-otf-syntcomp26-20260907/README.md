# Spot OTF closing comparison, SYNTCOMP26 full selection (2026-09-07 campaign)

Eight configurations, each run once on all 1,524 SYNTCOMP26 logical inputs with
its compiled defaults and empty runtime flags: 17-second cap, 8 GiB, zero swap,
one sequential invocation per systemd scope.

`cactus.png` / `cactus.pdf` / `cactus-table.md` compare those eight. The
three-way against ltlsynt and Acacia 1.x is a separate directory.

Per-configuration CSVs are in `cactus-report.py`'s input format
(`instance,result,seconds,exit`), 1,524 rows each, with `MEMOUT` mapped to
`RESOURCE_LIMIT` and `CRASH` to `ERROR`.

Decision-bearing summaries kept here:

| file | what it settles |
|---|---|
| `gain-attribution.tsv` | 383 isolated-worker jobs over the 50 answers new to all four shipping configurations: which worker actually decides each |
| `loss-diagnosis.tsv` | the separate 60-second diagnosis of both primary losses; never replaces a 17-second result |
| `primary-change-stability.tsv` | which primary coverage changes reproduce across three paired repetition rounds |
| `admission.json` | the frozen G1/G3/G4 eligibility record |
| `eligible-menus.json` | four-member union arithmetic over the eligible configurations |
| `taa-gain-replay.tsv` | same-provider eager/lazy replay behind the TAA real answers |

Raw per-instance rows, the repetition plan and provenance, and the pairwise
comparison dumps are under `_bm-logs.spot-otf-20260907/`, which is not tracked.
