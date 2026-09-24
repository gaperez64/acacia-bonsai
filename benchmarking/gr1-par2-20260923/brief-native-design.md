# Brief N0 — design the native gr1 and param-lift arms (READ-ONLY; write only the design)

Owner rules (decisions.md, last entries): acacia-bonsai is the single entry point; new techniques
are portfolio arms named in the existing `--arms` style that says whether the arm is for the
realizability or the unrealizability check; tlsf-tools is called through a C API from acacia-bonsai —
no Python anywhere on the solver path; no hardcoding of families/names/templates/per-family
choices; lifting only for inputs with PARAMETERS; outcomes invariant under renaming.
Write ONLY benchmarking/gr1-par2-20260923/native-design.md. No code edits; never stage/commit.
Read: acacia-bonsai src/ (arg_parser.hh --arms grammar and parse_portfolio_arms, portfolio_arm.hh,
acacia-bonsai.cc fork/race/kill of arms, how -T TLSF input is parsed today and which tlsf-tools library
calls Acacia already makes, meson.build linkage of tlsf-tools/Spot/BuDDy); tlsf-tools at branch
generic-provenance 8b158d7 (/home/gperez/GIT-repos/tlsf-tools: include/tlsf/*.h public headers,
libtlsf, src/main_tlsfsolve.c GR(1) path, src/main_tlsfcertcheck.c, src/oxidd_common.c, provenance
C code, scripts/gr1_monitor_game.py — Python+Spot reduction to port); the Python route to port
(scripts/acacia_lift/direct.py and scripts/acacia_lift/lifting/*: seeds, provenance use, schema
learning with BuDDy, instantiation, policy/region proof, settings.py knobs).
Design, concretely, with file-level plans and interface signatures:
1. Arm grammar: exact spellings for the new arms consistent with polarity:transform:backend[:provider]
   (e.g. whether direct GR(1) is two arms `real:gr1:...` and `unreal:gr1:...` or one exact arm that
   can answer both, and `real:param-lift:...`), help text, defaults (are they in the default
   portfolio or opt-in?), and how per-arm runs work (`--arms real:param-lift:...` alone).
2. The tlsf-tools C API: header(s), opaque handles, ownership, error model, deadlines/cancellation
   (cooperative checks + the arm process being killable), memory caps; functions for: parse/expand
   TLSF with provenance (existing C), GR(1) reduction (port of gr1_monitor_game.py to C++ inside
   tlsf-tools using Spot's C++ API — Acacia already links Spot; say where Spot lives), GR(1) solve
   (library-ise tlsfsolve's GR(1) core), certificate export + independent check (library-ise
   tlsfcertcheck; the checker must stay independent of the solver's algorithm), and parametric
   lifting (C++ port of Stage C: seed override instantiation, provenance-based roles, online arity
   search, schema reconstruction with BuDDy or OxiDD — pick one with reasons, instantiation,
   policy/region proof). Where each piece lives (tlsf-tools for solver-independent TLSF/GR(1)/
   certificate/lifting; Acacia for arm plumbing only).
3. Build: meson/linkage between Acacia and libtlsf (static or shared), Rust OxiDD, Spot/BuDDy
   (avoid two BuDDy copies), CI implications.
4. A staged implementation plan (reviewable steps, each with differential tests against the
   current CLIs / the Python route as oracle): e.g. (a) C API for parse/provenance/solve/check around
   existing C; (b) port the reduction; (c) Acacia gr1 arms; (d) port lifting; (e) param-lift arm;
   (f) retire the Python route from the shipped path. Estimate effort per step honestly.
5. Measurement plan: per-arm standalone legs via --arms (each of B's four arms, gr1, param-lift),
   then the full portfolio, original and obfuscated corpus, 60 s and 17 s; memory/CPU contention
   of an extra in-process-library arm inside the forked-arm model.
