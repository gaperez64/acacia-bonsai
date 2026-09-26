# Brief PA — freeze the milestone-1 measurement binary and prepare the per-arm legs (no timed runs)

Read decisions.md (owner plan: each arm alone at 60 s over SYNTCOMP26, serially; then choose a 4-5-arm
portfolio; real run; three-way), CLAUDE.md (presets/groups, self-benchmark.sh profiles, artifact
conventions), campaign/README.md and README-generic.md (runner usage), and the witness-lifting
manifest for how B (`otf_sparse_formula`) was built. NEVER stage or commit by any path. One job for
the build; never /tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig.
1. Build ONE frozen measurement binary from the current HEAD (milestone 1: gr1 arms + portfolio
   deadline fixes; tlsf-tools 33555ba with the OxiDD patch): the same configuration and release
   profile as B (`otf_sparse_formula`, BENCHMARK_COMPILE_PROFILE=normal i.e. release/O3/LTO/native,
   the same way B was built — read how) PLUS acacia_native_arms=true. Put it in a fresh
   build_perarm_m1/ directory, record sha256, git revision, meson options, tlsf-tools and OxiDD
   versions and patch record, compiler versions, in campaign/perarm-m1/binary-manifest.json. Verify
   `--help` lists the gr1 arms and that the default portfolio (no --arms) is exactly B's four arms.
   Do not chmod it read-only (I will freeze it).
2. Determine the exact spellings of B's four default arms from the binary/config (expected
   real:small:backward, real:small:forward, unreal:formula:spot-guarded-sparse,
   unreal:automaton:forward — verify), and write campaign/perarm-m1/README.md with the exact serial
   commands for six standalone legs over all 1,524 ORIGINAL-name inputs at a uniform 60 s cap
   (run-syntcomp26-coverage.py, --flags '--arms <arm>', -T appended by the runner, 8 GiB, no swap,
   --collect-rusage, one output dir per arm), plus the export-cactus step per leg, plus a
   selection script campaign/perarm-select.py (reuse benchlib): given the per-arm legs, compute for
   every 4- and 5-arm subset the virtual-best solved count and PAR-2, rank by solved count then
   PAR-2, and print the top 10 subsets and each arm's unique contributions — the rule is fixed now:
   maximise solved, tie-break by PAR-2. Mark it as a selection aid only.
3. Smoke-check (not timed): run each of the six arms alone on 3 small inputs (one REAL, one
   UNREAL, one non-reducible) under the runner with a 17 s cap and report outcomes.
4. Estimate wall time per leg from prior 60 s derived B rows / census data.
VERDICT at end.
