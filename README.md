# Acacia-Bonsai

Acacia-Bonsai decides whether a controller can satisfy a Linear Temporal Logic
(LTL) specification for every input sequence, and can build the controller.
It accepts LTL formulas and TLSF files, using antichains over downsets. See the
[TACAS 2023 paper](https://arxiv.org/abs/2204.06079) for the method.

## Build

You need GCC with C++23/C23 support, Spot 2.16.3 development headers, Meson 1.7+,
Ninja, pkg-config, Zsh, Flex, Bison, Cargo with Rust 1.91+, and cbindgen 0.29.4.
Python builds also need SWIG, Python development headers and Python 3.13+.

Initialize submodules recursively. From the repository root in Bash, build the
CLI and Python module with the first `docker_default` configuration:

```bash
export CARGO_BUILD_JOBS=2
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
build_dir=${ACACIA_BUILD_DIR:-build}
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -n1)
read -r -a preset_args <<< "$(python3 scripts/acacia-config.py meson-args "$preset")"
meson setup "$build_dir" --buildtype=debug -Dbuild_python=true \
  "${preset_args[@]}" -Dtlsf-tools:cpu=baseline
meson compile -C "$build_dir" -j 2
```

Set `ACACIA_BUILD_DIR` to change the directory. Spot must expose `libspot` and
`libbddx` through pkg-config; adjust `PKG_CONFIG_PATH` for its prefix. On macOS,
use Homebrew GCC for Spot and Acacia-Bonsai, setting `CC`/`CXX` before setup.

## Quick start

In the same shell, check whether repeated requests can receive repeated grants:

```bash
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant
```

The verdict is `REALIZABLE`. `G` means "always" and `F` means "eventually";
`G F req` means requests recur infinitely often. Read TLSF and write an ASCII
AIGER controller with:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf -s controller.aag
```

`--aiger-mode MODE` selects the controller encoding (default `isop`);
`both+ud` tries both encoders and negated conditions. Modes containing `+dc`
are rejected because they can produce incorrect controllers (#466).

## Guides

- [CLI](docs/cli.md): inputs, verdicts, exit codes, controllers and solver arms.
  `UNREALIZABLE` returns status 1, which matters in scripts using `set -e`.
- [Python](docs/python.md): import `acacia_boomslang`, solve a bounded safety game
  and inspect its region.
- [Docker](Docker/README.md): CLI source image and Boomslang notebooks.
- [Configuration](docs/configuration.md): compile-time presets and runtime options.
  Read current shipping choices with
  `python3 scripts/acacia-config.py list-group docker_default`.
- [Benchmarks](benchmarking/README.md): local runs and measurement gates;
  [results](benchmarking/RESULTS.md) records comparisons. Use matched release
  builds for performance work.

## Citing

```bibtex
@inproceedings{CadilhacPerez2023,
  author = {Michaël Cadilhac and Guillermo A. Pérez},
  title = {Acacia-Bonsai: A Modern Implementation of Downset-Based LTL Realizability},
  booktitle = {TACAS},
  year = {2023},
  pages = {192--207},
  doi = {10.1007/978-3-031-30820-8_14}
}
```

Licensed under [GPLv3](LICENSE). See the [changelog](CHANGELOG.md) for releases
and [CLAUDE.md](CLAUDE.md) for repository conventions.
