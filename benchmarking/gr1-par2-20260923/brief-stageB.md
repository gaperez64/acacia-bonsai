# Brief Stage B — source-origin provenance from the TLSF frontend (tlsf-tools)

Workspace /home/gperez/GIT-repos/tlsf-tools (branch main at c956847 is checked out; create nothing —
I will create the branch; work in the tree and leave changes uncommitted; NEVER stage or commit by
any path). Read /home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/generic-design.md
(§1 rows for gr1_monitor_game.py and generalizer provenance, §3.3, §6 stage 2) and the owner rule in
decisions.md there: no hardcoding — provenance must come from the TLSF source structure, never from
signal-name spelling or numeric suffixes. Read the TLSF frontend (parser, AST, generator/bus
expansion: src/, include/tlsf/) and scripts/gr1_monitor_game.py (source_origin_metadata, currently
available=false; AP suffix parsing at ~431-502).
Goal: a versioned, machine-readable provenance output from the frontend's expansion, e.g.
`tlsf2tlsf --provenance-out FILE.json` (or tlsfinfo), recording for the concrete instance:
- parameters and their concrete values;
- every expanded INPUT/OUTPUT signal → its source declaration id (stable across parameter values:
  declaration position/ordinal in the source, not its name), bus/array dimensionality, the concrete
  index tuple, and encoded-width information (e.g. nbits(n) buses: representation bit vs element);
- every expanded top-level conjunct in each specification block (INITIALLY/PRESET/REQUIRE/ASSERT/
  ASSUME/GUARANTEE) → source block, source formula node id (stable across parameter values), and the
  generator variable bindings (index values) that produced it, plus which expanded signals it
  mentions;
- a format version and the source sha256.
Ids must be derived from source structure (AST positions / declaration order), so alpha-renaming
signals or changing the basename leaves every id and index tuple unchanged, and re-instantiating
the same source at another parameter value gives the same ids with different index tuples.
Then make gr1_monitor_game.py consume it (set source_origin_metadata.available=true and carry
per-monitor/per-conjunct origin ids and index tuples into the game metadata) WITHOUT changing the
emitted game AIG (byte-identical games for all existing tests/fixtures); keep the old suffix-based
inference only as a fallback that is clearly marked and that Acacia's generic route will refuse
(provenance_source: "frontend" vs "suffix-heuristic").
Tests: differential — for a sample of SYNTCOMP26 TLSF inputs (there are TLSF files under
/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus/; pick ≥40 covering one-parameter, two-parameter
(n,u / N,M / xN,yN), nbits-encoded buses, 2-D arrays, ranges, and non-parametric), check that the
expanded signals/conjuncts listed by provenance exactly match the existing tlsf2tlsf/tlsf2ltl
expansion; invariance under alpha-renaming (use
/home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/obfuscate-tlsf.py if present, or
a simple consistent renaming) and under basename change; stability of ids across two parameter
values of the same source; ambiguous cases (if any) reported, not guessed.
Build one job only in build-stageB/ (meson setup -Dbuildtype=release -Doxidd=enabled
-Dresearch_tools=true; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig); full suite `meson test
--num-processes 1`; clang-format; never /tmp. VERDICT at end.
