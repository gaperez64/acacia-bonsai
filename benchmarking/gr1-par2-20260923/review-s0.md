# Review brief — S0 diagnostics instrument

Reviewer; do not edit tracked files; report to build_scratch/review-s0/REPORT.md. A timed diagnostic
campaign is running on this machine right now: DO NOT run test_generalize_gr1.py or any solver /
campaign / compile. You may run test_s0_diagnostics.py, test_tool_config.py, test_request.py (fast)
and ruff, one at a time.
Scope: uncommitted diff (generalize_gr1.py, param-lift-campaign.py, new s0_diagnostics.py,
test_s0_diagnostics.py, s0/README.md, s0/diagnostics.schema.json, .gitignore,
p0-dependency-manifest.json replay section). Brief: benchmarking/gr1-par2-20260923/brief-s0.md.
Check: (1) truly zero-cost when --diagnostics is off (no timers/counters/allocations on hot paths;
one boolean guard) and artifacts identical on/off; (2) diagnostics cannot change control flow,
deadlines, or results (exceptions inside instrumentation are contained); (3) the cgroup memory.peak
read is the whole invocation's scope, not a child's or the parent's rusage; censored stages recorded
as lower bounds; (4) the S0 distributions measure what plan §5A S0 asks (actual support width, owner
tuple arity, subset reuse, mask word-length) and not something easier; (5) the manifest's replay
section matches p0-replay/replay-L0.tsv exactly; (6) .gitignore of s0/raw/: is ignoring raw
evidence OK given plan §2.7 (raw data immutable and kept)? propose which small raw files must be
committed.
End with `## VERDICT` ACCEPT / ACCEPT-WITH-NITS / REJECT and numbered findings with severity.
