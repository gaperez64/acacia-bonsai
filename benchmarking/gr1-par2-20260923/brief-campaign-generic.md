# Brief CG — campaign preparation for the generic N (no timed runs by you)

> Historical research note: the Python wrapper and lifting package below are retained
> only as differential oracles. Current solver runs use native `acacia-bonsai` arms.

Read decisions.md (from "Owner stop" on), campaign/README.md (structure to reuse), the wrapper,
benchmarking/gr1-par2-20260923/oracle/acacia_lift/lifting/settings.py. NEVER stage or commit by any path. No compiling, no timed
solver runs; the obfuscation step and admission census are fine (serial).
Tools: /home/gperez/GIT-repos/tlsf-tools/build-SB-8b158d7. B, TACAS23, ltlsynt as in campaign/README.md.
1. **Obfuscated corpus.** Using benchmarking/gr1-par2-20260923/obfuscate-tlsf.py with ONE recorded
   master seed, produce the full obfuscated corpus of all 1,524 TLSF inputs into a gitignored dir
   (e.g. tlsf-corpus-obf/), with random basenames and alpha-renamed signals, plus: an obfuscated
   list + source map in the same formats as tests/suites/benchmarks/syntcomp26/{all.list,
   tlsf-sources.tsv}, and a SEPARATE sealed mapping file (original id <-> obfuscated id, signal
   maps) stored outside the corpus dir, used only by the post-hoc comparison. Verify every
   obfuscated file lowers to the renamed LTL of its original (reuse verify-obfuscation.py) and
   report counts. Commit-ready: list, map, mapping file hash (mapping itself committed too, it is
   evidence), the seed.
2. **Development selection (original names).** Admission census of the generic route at 17 s and
   60 s under the unchanged eligibility gate on the ORIGINAL corpus (which inputs the route will
   attempt: direct and/or lifting); write campaign/generic-selection/admitted-{17,60}.list. Commands
   (campaign/README-generic.md) to run N-a (lift slice cap/3) and N-b (2·cap/3) on exactly those IDs at
   each cap, B at 60 s derived from 120 s as before, comparisons via compare-selection.py (make it
   generic: route labels direct-certified / lifted-certified / attempted-declined).
3. **Final evaluation (obfuscated names).** Commands to run the chosen variant (placeholder CHOSEN)
   over all 1,524 obfuscated inputs at 60 s and 17 s with route records; export-cactus; a join
   script that maps obfuscated rows back to original IDs via the sealed mapping ONLY after the run,
   producing N CSVs keyed by original IDs for the three-way report; an invariance check comparing,
   for every ID present in both the selection run and the final run of the same variant and cap,
   the decisive verdict (must be identical; timings may differ); TACAS23 at 60 s (original pairs,
   fresh leg) as in campaign/README.md; the three-way cactus-report.py commands for CAP=60 and
   CAP=17 with ltlsynt (derived 60 s / archived 17 s) and TACAS23 (fresh 60 s / archived 17 s),
   labelling derived series; plus B-vs-N and per-route attribution (lifted-certified,
   direct-certified, B) tables.
4. **B on renamed inputs.** B is compared on original names; add a small command that runs B on
   30 randomly chosen obfuscated inputs and compares verdicts with its archived original rows, to
   show B's outcomes are name-independent (report command only).
Estimate wall times. VERDICT at end.
