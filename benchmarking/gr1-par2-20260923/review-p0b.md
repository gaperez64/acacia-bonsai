# Review brief — P0b (source binding)

Reviewer; do not edit tracked files. Report to build_scratch/review-p0b/REPORT.md.
Scope: uncommitted diff under benchmarking/param-lift-20260922/ (generalize_gr1.py,
param-lift-campaign.py, tool_config.py, new request.py, test_request.py). Brief:
benchmarking/gr1-par2-20260923/brief-p0b.md; plan §2, §3. No compiling; you may run the Python tests
(.venv/bin/python -m pytest), keep solver work tiny and serial.
Priorities:
1. SOUNDNESS: the diff says exact-game routes now "accept either independently proven side". Verify
   that a route sound only for REAL (strengthened reduction) can never emit UNREAL, that environment
   certificates are only accepted on an exact reduction, and that CERT_FAILED / timeout / unknown never
   becomes an answer (plan §2 invariants 2-3). Construct a concrete counter-scenario if you find a hole.
2. The runtime (source) path never reads status_120s / expected-verdict / baseline columns; filename
   match alone declines; an n=6 certificate never licenses n=7; mutated files cannot inherit a cached
   verdict. Try at least two mutations the tests don't cover.
3. The reproducer path (default) behaves exactly as before for the historical M6 commands —
   compare against HEAD (git show HEAD:path) for the reproducer code path.
4. Test count: the generalizer suite test_generalize_gr1.py had 11 tests passing at HEAD; the
   implementer reports "5 generalizer tests". Establish whether tests were removed/skipped/weakened
   and run the whole suite yourself (it takes ~4 minutes).
5. SyFCo-equality binding check: are its failure modes conservative (decline) everywhere, including
   SyFCo crash/timeout and parse errors?
End with `## VERDICT` ACCEPT / ACCEPT-WITH-NITS / REJECT and numbered findings with severity.
