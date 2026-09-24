# Brief P9 — extract the maintained lifting package; capability data replaces runtime m4 reads

Read plan.md §10 (all), §2; decisions.md (2026-09-24 entries, esp. "Integration design decided" and
"the proof method becomes a capability attribute"); integration-survey.md §6 (proposed layout and
the exact runtime TSV dependencies); CLAUDE.md. NEVER stage or commit by any path. One job; scratch
build_scratch/p9/. This is a REFACTOR-ONLY change plus one data move: generated artifacts and
decisions must be identical before/after (except the new per-capability real_check attribute,
which is new behaviour — do that as a separate final step/patch).
Steps (separate patch + note each in build_scratch/p9/):
1. Create scripts/acacia_lift/ as in integration-survey.md §6 (capabilities, tools, artifact,
   bdd_kernel, schema, instantiate, runner, evidence, diagnostics, buddy_veccompose, native/). Move
   code from benchmarking/param-lift-20260922/{generalize_gr1.py, request.py, tool_config.py,
   s0_diagnostics.py, buddy_veccompose.py, native/} with `git mv`-friendly structure (keep history
   readable: prefer moving whole functions verbatim; no algorithm changes). Keep
   benchmarking/param-lift-20260922/generalize_gr1.py and param-lift-campaign.py as the dated
   forwarding CLIs with their old argv (thin shims importing the package), and thin forwarding
   modules for request.py / tool_config.py / s0_diagnostics.py / buddy_veccompose.py so historical
   commands and tests still run. Tests move to tests/pytest/ (or stay and import the package) —
   all existing tests must still pass unmodified in intent.
2. data/capabilities-v1.json: a validated, versioned capability file that carries, per capability,
   the source/template hashes already pinned in request.py plus the fields the runtime currently
   reads from m4-alignment.tsv (stable_from, role-class count), m4-invariant-separability.tsv
   (invariant arity) and m4-move-separability.tsv (move arity); a loader with schema validation and
   hash binding; the runtime stops reading those TSVs (prove it: test that the runtime works with
   the TSVs moved away). Regression: every capability decision (schema arities, stable_from, role
   classes) identical to what the TSVs produced. Teach prove_all_n.py to read the package data.
   Do NOT delete or move any m4-*.tsv (they stay as dated evidence). m4-results.tsv becomes an
   explicit --results-out artifact, never an input.
3. The wrapper scripts/acacia-lift-portfolio.py keeps working (its --lift-entry may point at the
   dated campaign CLI or a new package entry `python -m acacia_lift.runner`; add the package entry
   and make it the documented default; keep evidence JSON identical in fields).
4. (separate patch, new behaviour) Add `real_check` per capability in capabilities-v1.json:
   "region" for arbiter_with_buffer and amba_decomposed_lock, "policy" for all others; the source
   route uses the capability's value unless --real-check is given explicitly (explicit flag wins;
   evidence records which and why). Tests.
Validation: full test suites (generalizer, request, tool_config, diagnostics, wrapper), ruff on the
package; a byte-identity differential of generated game/certificate/policy artifacts and evidence
decision fields before vs after steps 1-3 on arbiter n=6, load_balancer n=8,
round_robin_arbiter_unreal2 n=5 (reproducer and source modes) using
/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b and an adapter built into build_scratch/p9/.
MACHINE MAY BE RUNNING TIMED MEASUREMENTS: before heavy work wait until build_scratch/seq7/progress.txt
is absent or has a line starting "DONE". VERDICT at end.
