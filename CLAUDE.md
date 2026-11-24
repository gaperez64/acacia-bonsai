# Repository conventions

- Read shipping presets from `python3 scripts/acacia-config.py list-group
  docker_default`; preset names are historical labels. Do not hardcode the group's
  members.

- When adding an option, update `meson.options`, `config/acacia-options.json`
  and `src/config/acacia_build_config.hh.in` together.

- Use a fresh build directory
  when changing compiler, compile profile or native file. Release builds target the
  local CPU and embed the Git version; preserve frozen baseline executables instead of
  rebuilding them.

- Benchmark jobs default to one for measurement. Follow the
  [gates](benchmarking/README.md#gates), using the same configuration from the previous
  revision for G1. PAR-2 noise floors are 21.134 s on SYNTCOMP25 and 12.074 s on
  SYNTCOMP26.

- Write raw evidence to ignored `_bm-logs*/` directories or outside the
  checkout from the start. Pack, publish and verify archives with
  `scripts/acacia-evidence.py`, then register them in `benchmarking/evidence-index.tsv`.
  Keep active or sole-copy evidence until verification; preserve [publication
  holds](benchmarking/evidence-do-not-publish.txt).

- Pin frozen binaries in
  `benchmarking/baselines.tsv`. `scripts/prune-artifacts.sh` reports by default;
  deletion requires `--delete`.

- Record measured outcomes and rejected ideas in
  [RESULTS.md](benchmarking/RESULTS.md), citing archive IDs. Preserve original
  measurements when correcting their interpretation.

- Write imperative commit messages
  explaining the change, without a `type:` prefix. Follow recent history.
