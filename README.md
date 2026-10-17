# Acacia-Bonsai

Acacia-Bonsai decides LTL realizability using antichains over downsets and
synthesizes controllers. See the [TACAS 2023 paper](https://arxiv.org/abs/2204.06079).

## Docker

The CLI image includes sources and build dependencies. Compile inside the named
container to target the host CPU, then run the wrappers:

```sh
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
./scripts/compile.sh
./scripts/acacia-bonsai.sh -f 'G F req -> G F grant' -i req -o grant
cat examples/realizable.tlsf | ./scripts/acacia-bonsai.sh --tlsf
cat examples/realizable.tlsf | ./scripts/acacia-synthesis.sh --tlsf > controller.aag
```

Re-enter with `docker start -ai acacia`. The wrappers follow `docker_default`;
pass a shipped configuration as the first argument to select another.
The Boomslang image bundles the Python bindings, Spot and Jupyter:

```sh
docker run --rm -p 8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
```

## Build and run

Install GCC with C++23 support, Spot, Meson, Ninja, Zsh, Flex, Bison,
Cargo (Rust 1.91+) and cbindgen 0.29.4. Every build includes OxiDD and native
GR(1); Meson builds the native library from the nested sources. Initialize them:

```sh
git submodule update --init --recursive
meson setup build
meson compile -C build
build/src/acacia-bonsai -f 'G F req -> G F grant' -i req -o grant
build/src/acacia-bonsai -T examples/realizable.tlsf
```

On macOS, install GCC with Homebrew, set `CC`/`CXX` to that GCC toolchain, and
build Spot with the same compiler. If needed, set `PKG_CONFIG_PATH` to Spot's
`lib/pkgconfig` directory before Meson setup.

For an optimized build, read a shipping configuration from the registry:

```sh
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -n1)
./self-benchmark.sh -R -c "$preset"
"build_${preset}/src/acacia-bonsai" -T spec.tlsf -s controller.aag
```

`-R` builds without benchmarking; add `-L` for the low-memory compile profile.
Release builds target the local CPU. Native TLSF parsing needs no external
converter at runtime. `-s` writes a controller for a REALIZABLE answer, including
conversion for Moore targets. Use `-h` for CLI options.

## Reference and results

- [Build and development conventions](CLAUDE.md)
- [Architecture and arm contracts](benchmarking/DATA-STRUCTURES.md),
  including the [parity arm](benchmarking/coverage-first/parity-arm.md)
- [Correctness and measurement protocol](benchmarking/README.md)
- [Results and open gaps](benchmarking/RESULTS.md)
- [Decisions and archive IDs](benchmarking/decisions.md)
- [User-visible changes](CHANGELOG.md)

Generate API documentation with `doxygen Doxyfile`.

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
