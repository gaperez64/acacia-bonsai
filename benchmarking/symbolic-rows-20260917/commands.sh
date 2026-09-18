#!/usr/bin/env bash
# Exact commands used so far this sprint. Not a runnable pipeline end to end
# (R0's recovery step used git apply --3way plus hand-edits, not a script);
# this documents what each stage actually ran, in order.
set -euo pipefail

# R0: recovery. See the R0 commit message for the exact `git diff`/`git apply
# --3way` invocations against the pinned research-branch commits; no shell
# commands beyond ordinary git are reusable as a script here.

# Build used for every check below (debugoptimized/checked profile, not the
# release measurement profile; see CLAUDE.md's compile-profile table):
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig meson setup build_check \
  --buildtype=debugoptimized -Db_lto=false \
  -Dacacia_enable_tlsf_frontend=true -Dbuild_research_tools=true
meson compile -C build_check

# Gates run after every commit in this sprint:
meson test -C build_check --suite unit
python3 scripts/acacia-config.py validate
python3 tests/check-config-frontends.py .
git diff --check

# A2: first-row diagnosis over the frozen P2 cohort, both RowExpansion modes,
# same binary. Job formulas/partitions come from
# _bm-logs.20260916-demand-sparse/diag-p2mech/wrec/p2mech-solo-unreal-closure/
# (previous sprint's captured worker records; gitignored, locally present).
python3 benchmarking/symbolic-rows-20260917/targets/run-a2-first-row.py \
  build_check/src/acacia-spot-provider-replay
# -> benchmarking/symbolic-rows-20260917/targets/a2-first-row-results.tsv

# Single-job form the driver above wraps, for one target:
#   build_check/src/acacia-spot-provider-replay --arm c5 \
#     --formula "$WORKER_FORMULA" --k 2 --provider closure-buchi \
#     --closure-row-expansion enumerative|symbolic-boolean \
#     --partition "$PARTITION" --timeout-seconds 60 --stop-after-first-row
# (--formula-file FILE instead of --formula for jobs whose transformed
# formula exceeds the kernel's ~128 KiB per-argument length limit.)
