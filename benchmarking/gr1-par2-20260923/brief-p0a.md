# Brief P0a — dependency repair at the merged tlsf-tools pin, explicit tool configuration

Read first: benchmarking/gr1-par2-20260923/plan.md §0 "Pinned review inputs", "First solver task",
§3 (P0), §2 invariants; CLAUDE.md; subprojects/tlsf-tools/README.md and its build docs.
State: branch sprint/gr1-par2-20260923 (do not switch branches; git commit/checkout are blocked for
you — I commit). Submodule subprojects/tlsf-tools is now at 6b2507f (tlsf-tools main incl. #35 and #36);
the committed gitlink is still f2130939. Python venv: /home/gperez/GIT-repos/acacia-bonsai/.venv/bin.
Machine rules: never compile with more than -j1 / one job; no parallel solver runs; you cannot use
systemd-run; scratch goes in build_scratch/<task>/ under the repo, never /tmp. PKG_CONFIG_PATH must
include /usr/local/lib/pkgconfig for any meson setup.

Tasks:
1. Build tlsf-tools at the current submodule HEAD into a NEW directory
   subprojects/tlsf-tools/build-L0 with exactly the same meson options as the existing
   subprojects/tlsf-tools/build-oxidd (read them from build-oxidd/meson-info). DO NOT touch, rebuild,
   or reconfigure build-oxidd or any other existing build dir (build-oxidd is historical evidence).
   Record `meson introspect --buildoptions` of both in the manifest.
2. Find how the campaign/generalizer obtain the Python BDD bindings (generalize_gr1.py ~lines 29-40,
   409, 718: /usr/bin/python3.13 and /usr/local/lib64/python3.13/site-packages) and the tool paths in
   param-lift-campaign.py lines 46-49. Replace the host-specific hardcodes with explicit configuration:
   CLI flags on both entry points (--tlsf-tools-build DIR, --bindings-python PATH,
   --bindings-site DIR) with env-var fallbacks (ACACIA_TLSF_TOOLS_BUILD, ACACIA_BINDINGS_PYTHON,
   ACACIA_BINDINGS_SITE) and defaults that reproduce today's behaviour exactly when nothing is set
   (so the historical commands still work). Add a `probe` subcommand/option that validates the
   configured binaries exist and report their versions, and that the bindings import and expose the
   API the generalizer uses (list the functions it calls), WITHOUT running benchmark work; exit
   nonzero with a precise message otherwise. Keep the changes minimal and in the existing style.
   Identify which library/ABI the bindings are (BuDDy? which libbdd .so?) and record it.
3. Tests: run the tlsf-tools test suite in build-L0 (MESON_TESTTHREADS=1, `meson test -C build-L0
   --num-processes 1`), including the GR(1) certificate tests (test_gr1_unreal_certificate.py,
   test_gr1_monitor_game.py and whatever tests tlsfcertcheck). Run
   benchmarking/param-lift-20260922/test_generalize_gr1.py against build-L0. Add unit tests for the
   new configuration/probe code (unset → old defaults; env var; flag beats env; missing binary →
   clear error; probe does not start any solver).
4. Replay command, not replay: do NOT run the cold campaign yourself (it needs cgroups). Work out the
   exact param-lift-campaign.py invocation(s) that replay, against build-L0, (a) the 26 decisive M6
   rows (22 REAL + 4 UNREAL, see m6-campaign.tsv / m6-report.md) and (b) small REAL/UNREAL
   certificate regression controls (fast arbiter / prioritized_arbiter rows), each at the original
   120 s budget, writing to NEW files under benchmarking/gr1-par2-20260923/p0-replay/ (never
   overwrite m6-campaign.tsv). Check whether the script already wraps each run in systemd-run
   MemoryMax=8G MemorySwapMax=0; if it does not, say so. Put the commands in
   benchmarking/gr1-par2-20260923/p0-replay/README.md.
5. Write benchmarking/gr1-par2-20260923/p0-dependency-manifest.json: old/new gitlinks (f2130939 →
   6b2507f), the tlsf-tools commits in between (one line each), sha256 of tlsfsolve/tlsfcertcheck in
   build-oxidd and build-L0, build options, compiler versions, bindings identity/version, Python
   version, and a placeholder `replay` section I will fill after running step 4.
6. Lint changed Python with ruff (repo pyproject.toml).

Finish with a short VERDICT section in your final message: what changed (files), test results with
counts, anything that failed or looks off, and the replay commands.
