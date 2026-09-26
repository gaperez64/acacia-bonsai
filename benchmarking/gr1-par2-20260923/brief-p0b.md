# Brief P0b — bind the lifting route to the actual input (source binding)

Read: benchmarking/gr1-par2-20260923/plan.md §2 (invariants 1-3), §3 (all), §9.2 last paragraph,
§10.1; the current code benchmarking/param-lift-20260922/{param-lift-campaign.py (Request,
_resolve_request, FAMILIES), generalize_gr1.py (source registry, build_game, run, run_unreal_direct),
tool_config.py}; subprojects/tlsf-tools/scripts/gr1_monitor_game.py (exact reduction metadata).
Branch sprint/gr1-par2-20260923; I commit. No compiling; no solver runs beyond unit tests with tiny
instances (no systemd-run available to you; keep any test run under a few seconds each and serial).
Python via /home/gperez/GIT-repos/acacia-bonsai/.venv/bin; ruff per pyproject.toml.

Goal: a production request path that never trusts a basename or experiment columns.
1. Keep the existing dataset reproducer (basename → m0-instances.tsv row → template at n) working
   unchanged for historical commands, but clearly labelled as the reproducer path.
2. Add a production request object (new small module, e.g. benchmarking/param-lift-20260922/request.py
   — later P9 will move it into a maintained package, so keep it self-contained): actual input bytes +
   sha256, parsed TLSF source identity (semantics/target, INPUTS/OUTPUTS ownership, parameters),
   the family capability that matched and HOW (structural/semantic match, or content-verified template
   instantiation: re-instantiate the family template at the inferred parameters and verify it denotes
   the supplied instance under the existing lowering — e.g. syfco/normalized-formula equality or the
   existing reduction's canonical output, whichever the code already has; pick the strongest cheap
   check available and document it), the complete parameter assignment, and I/O mapping. The runtime
   path must never read status_120s, expected-verdict, or baseline columns. Filename match alone
   declines.
3. One capability registry: remove the duplicated FAMILIES definitions between the campaign and the
   generalizer (single source). A capability selects a route kind (REAL-proposal / exact-game both
   sides / sound one-sided) — not an answer.
4. Tests (§3 Tests): same filename with modified guarantee, changed parameter, changed target
   semantics, changed I/O ownership, substituted template, duplicate/missing AP, stale manifest →
   decline (or correct original-spec route); no mutated file inherits a cached verdict; an n=6
   certificate never licenses n=7. Use small real SYNTCOMP26 family files from the corpus
   (tests/syntcomp-benchmarks) as the unmutated positives.
5. Add a `--request-mode {reproducer,source}` (default reproducer, so historical commands keep
   working) to the campaign entry point, and a JSON evidence field recording which mode and binding
   evidence were used.
Finish with a VERDICT: files changed, test counts, which binding check you chose and its known gaps.
