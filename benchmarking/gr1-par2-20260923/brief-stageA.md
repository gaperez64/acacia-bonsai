# Brief Stage A — a hardcoding-free production route: generic direct-certified exact GR(1)

> Historical research note: the Python wrapper and lifting package below are retained
> only as differential oracles. Current solver runs use native `acacia-bonsai` arms.

Read decisions.md (the last three entries), generic-design.md (all), CLAUDE.md, and the owner rule:
**no hardcoding ever** — nothing about benchmark families, names, filenames, name patterns, pinned
templates, per-family tables or per-family switches may influence any decision; the route must be
invariant if every basename were randomized and every signal identifier alpha-renamed. Competition
input is TLSF. NEVER stage or commit by any path (including /usr/bin/git). One job; scratch under
build_scratch/stageA/; never /tmp. Machine is free (no timed runs until I say).
Deliverables, as separate patches with notes in build_scratch/stageA/:
1. **Strip.** The production path (benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py → python -m acacia_lift.runner,
   source mode) must contain no family registry, no vendored templates, no capabilities-v1.json,
   no route_enabled / real_check tables, no family_hint, no M0/census/basename lookup, no
   `_collector_candidate`, no named structured-grant branch, no name/text bus recognisers, no
   per-family stable_from/arity/role tables. Remove them from benchmarking/gr1-par2-20260923/oracle/acacia_lift/ (delete the
   data/templates tree and capabilities-v1.json). REAL lifting depends on those, so in Stage A the
   production route makes NO REAL lifting attempt (it will return in Stage C, generically). The
   historical reproducer (the dated benchmarking/param-lift-20260922 CLIs that re-run M-series
   evidence) may keep working only if it lives entirely outside benchmarking/gr1-par2-20260923/oracle/acacia_lift/ and is
   unreachable from the wrapper and runner; if keeping it costs real complexity, instead make the
   dated CLIs fail with a clear message pointing at the commit that last supported them (a620… no —
   use `git log` to find the last commit before this change) and say so. Do not delete dated
   evidence files (TSVs, reports).
2. **Generic direct route.** For ANY TLSF input: hash/spool the bytes once; lower the ACTUAL input
   (no parameter overrides) with tlsf-tools' gr1_monitor_game.py in exact mode; if it declines or
   fails → decline. Otherwise solve the exact GR(1) game with tlsfsolve (pass an explicit GR(1)
   game profile — generic-design.md §3.6 notes the auto profile requires a controllable input),
   export the winning side's certificate (system certificate+policy for REAL, environment
   certificate for UNREAL), and check it with tlsfcertcheck independently; answer only on an
   independently VERIFIED certificate bound to this input's hash and the exact game. Strict
   (non-exact) reductions may only ever yield REALIZABLE. Evidence records route "direct-certified".
   Reuse the existing run_exact_direct / checker invocation code; remove its capability gating.
   Budget: the wrapper's existing lift slice and eligibility budget, unchanged in mechanism.
3. **Anti-hardcoding guard.** A pytest (tests/pytest/) that fails if any module under
   benchmarking/gr1-par2-20260923/oracle/acacia_lift/ or the wrapper contains any of the 29 family labels found in the old
   registry/m4 tables (list them in the test), a basename/regex dispatch on input names, or reads
   any file under tests/syntcomp-benchmarks or benchmarking/param-lift-20260922 at runtime.
4. **Obfuscation tool + invariance test.** benchmarking/gr1-par2-20260923/obfuscate-tlsf.py: for a
   TLSF file, produce a semantically identical file with a random basename and every INPUTS/OUTPUTS
   signal (and bus) identifier alpha-renamed consistently (deterministic from a seed; keep TLSF
   keywords/operators; preserve parameters and their uses); write a mapping sidecar. Verify on the
   whole corpus that the lowered LTL of original and obfuscated inputs is equal modulo the renaming
   (tlsf2ltl both, apply the mapping, compare), and report any file where that fails. A test runs the
   production route on 10 original/obfuscated pairs (small, fast ones, including one REAL and one
   UNREAL direct-certified win and one decline) and asserts identical decisions and verdicts.
5. **Census.** Re-run a generic eligibility census over the 1,524 inputs: for each, does the exact
   reduction succeed (and how long it takes), under a per-input bound equal to the 17 s cap's lift
   slice; write campaign/generic-census.tsv + summary (counts of reducible inputs, reduction time
   distribution). This is the attempt set for Stage A.
Validation: full tests/pytest and remaining lifting suites serially; ruff on touched files.
VERDICT at the end: files removed/changed, what the dated CLIs now do, census counts.
