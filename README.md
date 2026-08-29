# Acacia-Bonsai

This is a modern implementation of universal co-Buchi reactive synthesis
algorithms using antichain data structures.  The theory and practice is described in:

   https://arxiv.org/abs/2204.06079

# Docker images

Two images are published on the GitHub Container Registry. Boomslang bundles
Spot's Python bindings, `acacia_boomslang`, and Jupyter:

```
docker pull ghcr.io/gaperez64/acacia-boomslang:latest
docker run --rm -p 8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
```

The CLI image contains the dependencies and sources. It compiles inside the
container so `-march=native` can target the host:

```
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
./scripts/compile.sh
./scripts/acacia-bonsai.sh best_decomp_rank_bucketed_mona \
  -f 'G F req -> G F grant' -i req -o grant
cat examples/realizable.tlsf | \
  ./scripts/acacia-bonsai.sh best_decomp_rank_bucketed_mona --tlsf
cat examples/realizable.tlsf | \
  ./scripts/acacia-synthesis.sh best_decomp_rank_bucketed_mona --tlsf > controller.aag
```

The CLI example intentionally omits `--rm`: compilation happens inside the
named container, so removing it on exit would discard the binaries. Re-enter
it with `docker start -ai acacia`. Run the wrapper without arguments to list
the configurations it accepts.

# Dependencies

This program depends on:
- A modern C++ compiler (C++23 is used)
- [The Meson Build System](https://mesonbuild.com/)
- [The Downset Manipulation Library](https://github.com/michaelcadilhac/posets)
- [The Spot Library](https://spot.lrde.epita.fr/): You will need to compile
  and install spot separately. The downset manipulation library requires
  compilation with `g++` so to link against spot you need to compile it with
  `g++` too (set `CXX` before configuring, compiling, and installing).
- [The Z shell](https://www.zsh.org/), for some scripts.

Initialize the vendored Posets and TLSF-tools dependencies after cloning:
```
git submodule update --init
```
This is deliberately non-recursive; Acacia disables TLSF-tools' optional
OxiDD backend.

Some of the tests also depend on:
- Valgrind

## Installing dependencies on macOS

Note that on macOS the compilation has to happen via GCC. 
GCC can be installed using Homebrew: `brew install gcc`. Once installed,
meson needs to be told to use GCC instead of built-in Clang. This can be done
using the meson-native file, or by setting the `CXX` and `CC` environment variables.
For instance, do `export CXX=$(brew --prefix)/bin/g++-XX` before starting with
meson.

Spot has to be manually compiled using GCC and installed. After compiling it
and installing it, you still need to ensure that meson can find Spot using `pkgconfig`.
This can be done, for example, by setting the `pkg_config_path` in a meson-native file
or by issuing `export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig` before
starting with meson (this works if you installed spot in the default
location).

# Compiling, running, benchmarking

To compile and run, use Meson:
```
meson setup build
cd build
meson compile
src/acacia-bonsai -h
  [...]
src/acacia-bonsai -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
REALIZABLE
```

This produces a debug build. Build an optimized registered preset with:
```
./self-benchmark.sh -c best_decomp_mona -R
```

The `-c` option selects a configuration and the `-R` option disables the
benchmarking step, so that only setup and compilation are done. If compilation
memory is tight, add `-L` to use the low-memory compile profile.

The Meson option `acacia_enable_tlsf_frontend` is off by default, so a bare
`meson setup build` does not require Flex and Bison. Every configuration preset
enables the frontend, so any `self-benchmark.sh -c NAME` build includes it:
```
./self-benchmark.sh -L -R -c best_decomp_mona
build_best_decomp_mona/src/acacia-bonsai -T spec.tlsf
```
`-T/--tlsf FILE` parses TLSF natively. The wrapper accepts TLSF on standard
input with `--tlsf` through the linked frontend; no external TLSF
converter or metadata binary is used at runtime. Moore-target controller
conversion is likewise performed inside Acacia.

Correctness and performance gates, including the sequential measurement
protocol, are documented in [benchmarking/README.md](benchmarking/README.md).
The current comparison with `ltlsynt`, residual gap analysis, and durable experiment record are
in [benchmarking/LTLSYNT-GAP.md](benchmarking/LTLSYNT-GAP.md).

# Compile-time configurations

Acacia-Bonsai's optimized variants are compile-time configurations.  The
configuration registry lives in `config/acacia-options.json` and
`config/acacia-presets.json`; `scripts/acacia-config.py` validates presets and
translates them to Meson options.

Inspect and validate the registry with:
```
python3 scripts/acacia-config.py validate
python3 scripts/acacia-config.py list-presets
python3 scripts/acacia-config.py show best_decomp_mona
python3 scripts/acacia-config.py meson-args best_decomp_mona
```

`meson.build` writes these choices into `acacia_build_config.hh`. The Spot
translator preference is registry-backed as
`acacia_translation_pref`; the default is `small`. The values `any` and
`small+any` are available for ablation and racing presets such as
`best_decomp_mona_any` and `best_decomp_mona_race`.

The shipping configuration is `best_decomp_rank_bucketed_mona`; the Docker
image and the TLSF examples above build it, and the correctness and performance
gates are frozen against it. `best_decomp_mona` is the same configuration over
the plain vector-backed downset, kept as the reference point for downset
comparisons.

The shipping preset enables the exact equivariant solver. It automatically
declines to the classic solver when no verified profitable symmetry is
available, or when fewer than `acacia_equivariant_min_blocks` client-state
blocks are found (default 2). Use `best_decomp_rank_bucketed_mona_noequivariant`
for the explicit classic-only escape hatch and performance ablation. New
presets should inherit from the nearest existing configuration and override
only the values being tested.

# Documentation

This project comes with a doxygen configuration file. Execute the following command to generate
documentation:
```
doxygen Doxyfile
```

# Citing

If you use this tool for your academic work, please make sure to cite the
paper we wrote about it.

```
@inproceedings{DBLP:conf/tacas/CadilhacP23,
  author       = {Micha{\"{e}}l Cadilhac and
                  Guillermo A. P{\'{e}}rez},
  editor       = {Sriram Sankaranarayanan and
                  Natasha Sharygina},
  title        = {Acacia-Bonsai: {A} Modern Implementation of Downset-Based {LTL} Realizability},
  booktitle    = {Tools and Algorithms for the Construction and Analysis of Systems
                  - 29th International Conference, {TACAS} 2023, Held as Part of the
                  European Joint Conferences on Theory and Practice of Software, {ETAPS}
                  2022, Paris, France, April 22-27, 2023, Proceedings, Part {II}},
  series       = {Lecture Notes in Computer Science},
  volume       = {13994},
  pages        = {192--207},
  publisher    = {Springer},
  year         = {2023},
  url          = {https://doi.org/10.1007/978-3-031-30820-8\_14},
  doi          = {10.1007/978-3-031-30820-8\_14},
  timestamp    = {Sat, 29 Apr 2023 19:25:03 +0200},
  biburl       = {https://dblp.org/rec/conf/tacas/CadilhacP23.bib},
  bibsource    = {dblp computer science bibliography, https://dblp.org}
}
```
