# Brief N3 — Acacia gr1 arms (native-design.md step 3)

Workspace /home/gperez/GIT-repos/acacia-bonsai (branch sprint/gr1-par2-20260923; I commit; NEVER stage
or commit by any path). tlsf-tools submodule will be pinned to the native-api commit that contains
steps 1-2 (I bump it before you start; check `git submodule status`). One job for every compile
(meson compile -j 1); build dirs build_native3*/; never /tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig.
Spec: benchmarking/gr1-par2-20260923/native-design.md ("User-facing arms and execution" and step 3),
decisions.md (owner rules: acacia-bonsai is the entry point; arms named in the existing --arms style;
tlsf-tools via its C API only; no Python on the solver path; no hardcoding; renaming invariance).
Implement exactly the design's step 3:
- arms `real:gr1:oxidd` (REALIZABLE only: exact reduction, solve, system certificate + policy checked
  by the tlsf-tools checker library) and `unreal:gr1:oxidd` (UNREALIZABLE only: exact reduction,
  solve, environment certificate + counterstrategy checked), parsed by parse_portfolio_arms with the
  design's rules (oxidd backend only for native transforms; no provider; require -T; reject -f/-F/-s
  combinations as designed; help/error text);
- portfolio_arm kind dispatch in the existing forked child in src/acacia-bonsai.cc: native children
  call the tlsf-tools library in-process with the source snapshot (read once, SHA-256, before fork),
  the outer monotonic deadline and memory caps; only a VERIFIED result of the arm's own polarity maps
  to EXIT_CODE_REAL/UNREAL; everything else is EXIT_CODE_UNKNOWN with a structured diagnostic;
  signal-killed children never count; parent publish-first/terminate-and-reap unchanged;
- -T handling as designed: snapshot bytes, defer legacy conversion, native-only lists never need
  legacy conversion, mixed lists still run native arms if legacy conversion fails;
- meson: an Acacia option (e.g. acacia_native_arms) that builds the tlsf-tools subproject with
  native_gr1=enabled, oxidd=enabled; one Spot/BuDDy; OxiDD archive linked once; the default build
  unchanged when the option is off; register the option in all three configuration frontends
  (meson.options, config/acacia-options.json, src/config/acacia_build_config.hh.in) as CLAUDE.md
  requires, and pass `python3 scripts/acacia-config.py validate` and tests/check-config-frontends.py.
- default portfolio unchanged (native arms opt-in).
Tests: arg parsing (valid/invalid spellings, duplicates, provider rejected, -T required); one-arm runs
of each native arm on small REAL and UNREAL TLSF inputs (right polarity answers, wrong polarity gives
UNKNOWN); a mixed list with a legacy arm (first verified answer wins; loser killed and reaped); failed
verification -> UNKNOWN; deadline expiry -> UNKNOWN and children reaped; renamed/obfuscated inputs
(benchmarking/gr1-par2-20260923/obfuscate-tlsf.py) give identical outcomes; differential vs
scripts/acacia_lift/direct.py (Python oracle, test-only) on ~50 corpus inputs: same verdict or
native UNKNOWN only where Python also fails/declines, report any disagreement. Existing unit suite
(`meson test -C <build> --suite unit`) green in both option states. VERDICT at end.
