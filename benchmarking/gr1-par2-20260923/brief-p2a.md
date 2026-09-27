# Brief P2a — native simultaneous composition + compile AIG roots once (generalizer)

Read plan.md §2, §5.1, §5.2, §5A S4, §6.1-6.2; the manifest
benchmarking/gr1-par2-20260923/p0-dependency-manifest.json (bindings identity: Spot's BuDDy,
libbddx.so.0); generalize_gr1.py (Bdds, substitute_variables, from_aag, _predicate_templates,
game_functions, generic policy export) and its S0 diagnostics (committed just before this task).
Branch sprint/gr1-par2-20260923; I commit. Compile with one job only; scratch/build under
build_scratch/p2a/ never /tmp. Python via .venv/bin; the BDD bindings run under the configured
bindings interpreter (see tool_config.py). No solver campaigns; unit tests only.

Step 0 — feasibility (write findings to benchmarking/gr1-par2-20260923/p2a-feasibility.md FIRST):
can a tiny native helper (C or C++ extension built against the SAME libbddx the Python `buddy`
module uses — verify by resolving the loaded .so of the running process, never load a second copy)
take the Python bdd proxies' underlying BDD ids, run bdd_veccompose with a bddPair built via
bdd_newpair/bdd_setbddpair, and return a proxy the binding owns with correct refcounting? Or does
the binding already expose bdd_veccompose / bddPair (check SWIG module symbols)? Choose ONE route:
(a) use an already-exposed API, (b) a small native adapter against the same libbddx, or explain why
neither is sound and stop step 1 (still do step 2). Do not implement an OxiDD kernel.

Step 1 — simultaneous composition (if feasible): Bdds.substitute_variables uses one
simultaneous composition instead of 2m bdd_compose calls. Keep the old two-pass method as a test
oracle. Tests: x:=y,y:=x swap; cycles; replacements depending on each other; non-injective;
constants; variables outside support; repeated calls under GC (force bdd_gbc); resource failure
propagates as an exception, never a wrong BDD.

Step 2 — compiled-AIG context (§5.2): attempt-local context with normalized gate map, public
variable ABI, shared traversal memo, manager lifetime; decode required seed predicates as one
multi-root DAG pass; cache _predicate_templates by the full key the plan lists (seed certificate
identity, predicate, role/anchor/goal relation, arity, normalization mapping, manager lifetime);
reuse one BDD→AIG memo across outputs with the same variable-to-literal map in policy export; cache
game_functions() results in the context. Explicit release at the end of the reuse window. Keys never
use raw node ids across instances/managers.
Tests: generated certificates/policies semantically equal to HEAD's (BDD equivalence after
re-import, not text) on the generalizer test suite's instances; caching never crosses instance or
manager; S0 diagnostics show fewer bdd_compose calls and fewer from_aag traversals.
Keep each step a separate patch file (build_scratch/p2a/stepN.patch, git add -N for new files is
allowed) with a one-paragraph note. Full test_generalize_gr1.py + test_request.py +
test_tool_config.py green after each step; ruff clean.
Finish with a VERDICT.
