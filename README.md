# Acacia-Bonsai

Acacia-Bonsai checks whether a controller can satisfy a Linear Temporal Logic
(LTL) specification for every possible sequence of inputs. If it can, the tool
can build that controller. The environment chooses the inputs; the controller
chooses the outputs.

It accepts LTL formulas and TLSF files. The solver uses antichains over downsets;
the [TACAS 2023 paper](https://arxiv.org/abs/2204.06079) explains the method.

## Build

You need GCC with C++23 and C23 support, Spot 2.16 with development headers,
Meson 1.7 or newer, Ninja, pkg-config, Zsh, Flex, Bison, Cargo with Rust 1.91 or newer,
and cbindgen 0.29.4. For Python, also install SWIG and Python development headers.
The Python package requires Python 3.13 or newer.

Start with a checkout whose submodules, including nested submodules, are
initialized. Run these commands from the repository root in Bash. This builds
the CLI and Python module with the first configuration in `docker_default`:

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

Keep using the same shell for the quick start below. Set `ACACIA_BUILD_DIR`
before setup to put the build elsewhere. Spot must provide both `libspot` and
`libbddx` through pkg-config; adjust `PKG_CONFIG_PATH` for a different install
prefix. On macOS, use Homebrew GCC for both Spot and Acacia-Bonsai, and set
`CC` and `CXX` before setup.

## Quick start

Ask whether repeated requests can be met by repeated grants:

```bash
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant
```

The verdict is `REALIZABLE`. `G` means “always” and `F` means “eventually”, so
`G F req` means requests occur infinitely often. To read a TLSF specification
and write its controller as an ASCII AIGER circuit:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf -s controller.aag
```

## CLI

The [CLI guide](docs/cli.md) covers input files, verdicts, exit codes, controller
output, and solver selection. Use it when calling the tool from a script:
`UNREALIZABLE` has exit status 1, even though the solver answered the question.

## Python

The build above also produces `acacia_boomslang`. The
[Python guide](docs/python.md) shows how to import it, solve a bounded safety
game, and inspect its winning region. It also explains how that API differs
from the CLI.

## Docker

The [Docker guide](Docker/README.md) covers the CLI source image and the
Boomslang notebook image. The CLI image needs a compile step inside the
container; Boomslang includes a built Python module, Spot, and Jupyter.

## Presets and configuration

A preset chooses components at compile time. `docker_default` names the
configurations used by the Docker wrappers; its first entry is also used by
Python builds. Read the group instead of copying a preset name. The
[configuration guide](docs/configuration.md) explains how to inspect presets,
choose solver workers at runtime, and set translation effort.

## Benchmarks

See [results and their measurement conditions](benchmarking/RESULTS.md) for
performance comparisons. The [benchmarking guide](benchmarking/README.md)
starts with a small local run and describes the correctness and measurement
gates. Debug builds are useful for checking examples; use matched release
builds for performance comparisons.

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

The software is licensed under [GPLv3](LICENSE). See the
[changelog](CHANGELOG.md) for user-visible changes and
[CLAUDE.md](CLAUDE.md) for development conventions.
