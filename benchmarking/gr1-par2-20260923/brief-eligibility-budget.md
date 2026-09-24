# Brief E — bound the eligibility tax on inputs that will decline

Read benchmarking/gr1-par2-20260923/campaign/eligibility-summary.md and eligibility.tsv (census),
decisions.md (integration entries), scripts/acacia_lift/capabilities.py and the wrapper
scripts/acacia-lift-portfolio.py. NEVER stage or commit by any path. No solver campaigns.
Problem: 360 non-family n-parametric inputs make 92 tool calls before declining (P99 0.87 s,
max 7.08 s), and 3 inputs hit the 5 s lowering-tool timeout (~7 s total). At a 17 s cap that time
is taken from B. A decline is always sound.
1. Cheap structural pre-filter before any lowering call: from data the binding already has
   (tlsfinfo basic metadata of the actual source: semantics, target, declared input/output
   signal names/bus names, parameter names and inferred values), compare with each shortlisted
   capability's template signature at the inferred n computed WITHOUT lowering (store per-capability
   signature facts in capabilities-v1.json, derived and verified at load from the vendored template,
   or computed once per process and cached). Candidates failing the signature are dropped before
   tlsf2tlsf/tlsf2ltl. The final exact equality contract is unchanged — the pre-filter may only
   decline, never admit.
2. An absolute eligibility budget: wrapper option --eligibility-budget-seconds (default 1.0 s,
   plus a fraction cap: min(1.0, 0.05 × cap)); binding runs under min(that, remaining deadline);
   overrun → decline with reason eligibility_budget_exhausted, kill tool processes. Evidence and
   route record carry it.
3. Re-run the census (eligibility-census.py, serial) with the new code into
   campaign/eligibility-v2.tsv + summary; the eligible set must be IDENTICAL to eligible.list (any
   difference is a bug unless it is a budget decline, which must be reported loudly); report the
   new decline-cost distribution (target: every decline under the budget; median unchanged).
4. Tests: pre-filter never rejects a true member (all 91 eligible + the 33 guard-declined members
   still reach the same decision); budget overrun declines and kills children; a slow fake lowering
   tool is cut off at the budget.
Machine note: another process may be timing; wait until build_scratch/seq8/progress.txt is absent or
has a line starting "DONE" before running the census or heavy suites. Full suites + ruff. VERDICT.
