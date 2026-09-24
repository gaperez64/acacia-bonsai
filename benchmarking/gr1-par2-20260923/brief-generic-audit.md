# Brief G0 — audit every family-specific dependency; design a generic lifting route (READ-ONLY)

Owner rule (decisions.md, last entry): **no hardcoding ever.** The route must work unchanged if every
benchmark name were obfuscated and no family were ever seen before. No family registry, no pinned
templates, no per-family data (arities, stable sizes, role classes, seeds, bus schemas), no
per-family switches, no filename or name-pattern logic, no family-specific code paths. Global
parameters are allowed only if they are few, documented, and not per-family.
Write ONLY benchmarking/gr1-par2-20260923/generic-design.md. Edit nothing else; never stage or
commit by any path; no compiling; no solver runs except tiny read-only probes (tlsfinfo/tlsf2tlsf on
a handful of corpus files, serial, <5 s each).
Scope to audit: scripts/acacia_lift/ (all modules + data/), scripts/acacia-lift-portfolio.py,
benchmarking/param-lift-20260922/ (dated CLIs, prove_all_n.py), and tlsf-tools
(/home/gperez/GIT-repos/tlsf-tools at main c956847: scripts/gr1_monitor_game.py and anything it
imports, src/main_tlsfsolve.c GR(1) paths, src/main_tlsfcertcheck.c).
Deliver in generic-design.md:
1. **Inventory**: every place where a family name, filename/pattern, per-family constant or table,
   pinned template, or family-specific algorithmic branch (e.g. a `_collector_candidate` special
   case, bus-schema recognisers keyed to names, role-class tables) influences behaviour — file:line,
   what it does, what it is used for. Be exhaustive; grep for every family name in capabilities-v1.json
   and m4-*.tsv, for "family", "FAMILIES", "arbiter", "amba", "collector", "load_balancer", etc.
2. **What the input itself provides**: for the SYNTCOMP26 corpus (tests/suites/benchmarks/syntcomp26/
   all.list + tlsf-sources.tsv + tlsf-corpus/), how many inputs declare PARAMETERS, which parameter
   names occur, whether tlsf2tlsf can re-instantiate an input at other parameter values (overrides),
   and whether that works when the parameter is used in bus widths / ranges / generators. Use the
   existing census data (campaign/eligibility-v3.tsv) where possible; spot-check with tiny probes.
   State clearly the assumption the generic route would rely on (the input is a parametric TLSF whose
   parameters can be re-instantiated), and what happens when an organiser expands parameters away.
3. **Generic replacement design** for each inventoried dependency: seed choice (which smaller
   parameter values, how many, chosen online), detecting degenerate small instances without a
   stable_from table, role/index discovery from the instantiated signals' provenance (not names),
   arity search online (smallest arity whose schema is consistent on the seeds, bounded by deadline),
   proof method choice (policy vs region) by a global rule or by trying both within the budget,
   UNREAL handling (direct exact GR(1) solve + environment certificate for ANY GR(1)-reducible input,
   which is not lifting but is generic), multi-parameter inputs, and decline conditions. For each,
   say what exists in code today that can be reused and what is new.
4. **Soundness argument**: why the generic route stays sound regardless of how bad the online
   guesses are (target certificate checked independently on the actual input; declines otherwise).
5. **Expected coverage and cost**: which of the 72 previously route-eligible inputs a generic route
   can still reach and which only worked because of per-family data; the cost of online search on
   inputs where it fails (it will be charged before B) and how to bound it globally.
6. **A staged implementation plan** (independently reviewable steps) and an **evaluation protocol**
   that does not tune on the evaluation instances (e.g. global parameters fixed a priori or chosen on
   a disclosed development split; names obfuscated in the evaluation run to prove independence).
Be concrete; tables welcome; cite file:line.
