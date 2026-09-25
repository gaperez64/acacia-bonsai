# Brief N4 — native parametric lifting in tlsf-tools (native-design.md step 4)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api at 33555ba (steps 1-2 + OxiDD patch).
NEVER stage or commit by any path. One job for every build (meson compile -j 1, cargo -j 1); never
/tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. TIMED BENCHMARKS ARE RUNNING on this machine for the
next ~2 days: keep to one core, do not run anything heavy in parallel, and keep test runs short
(bounded inputs, timeouts).
Spec: /home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/native-design.md (step 4
row and the C ABI section), decisions.md there (owner rules: no hardcoding, lifting only with
PARAMETERS, renaming invariance, target-derived moves by design, global knobs in one place), and
the Python reference to port: /home/gperez/GIT-repos/acacia-bonsai/scripts/acacia_lift/lifting/*
and direct.py (Stage C, reviewed; test oracle only). Use the step-1/2 library pieces (pipeline byte
loading + parameter overrides, provenance, tlsf_gr1_reduce_v1, versioned solve with deadlines, the
independent checker incl. --method region). Choose OxiDD or Spot's BuDDy for schema BDD work as the
design recommends and justify it (one BDD package per call path; no second BuDDy copy).
Deliver a library entry point in the existing API style (e.g. include/tlsf/gr1_lift.h:
tlsf_gr1_lift_v1(source snapshot, target overrides, options{deadline, cancel, caps, knobs}) ->
verified REALIZABLE certificate/policy or a structured decline), implementing the Stage C algorithm:
seed windows from the input's own PARAMETERS via overrides (never the target size), provenance-based
roles/indices (never names), online arity search up to global K, exact per-seed reconstruction and
cross-seed template equality, target instantiation with target-derived moves, policy check then
region check under one global order, and only an independently VERIFIED target certificate counts;
never UNREALIZABLE. Global knobs in one header/struct with documented defaults equal to the Python
settings.py values.
Tests (design table): differential vs the Python route on generated unseen parametric families (write
or reuse a generator; random names) and a bounded development-corpus sample (≤40 parametric inputs,
timeout 60 s each, serial): same seed choices, roles, arities, method and verdict (compare BDD
functions, not serialized AIGs); no-PARAMETERS declines; unsupported bus widths; unstable ranks;
large arity declines; a mutated certificate rejected by the checker; deadline/cancellation/capacity
declines; renaming invariance (obfuscated twins identical). Full meson test serially; ASan/UBSan
on the lifting unit tests. Patches + notes in build-native4/. Checkpoint your progress in
build-native4/PROGRESS.md as you go (this is a long task). VERDICT at end.
