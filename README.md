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
container so `-march=native` can target the host. Its first shipped
configuration uses a native GR(1) realizability pre-pass on TLSF input. The
image includes Rust, `cbindgen`, the OxiDD source, and cached Cargo
dependencies needed by `scripts/compile.sh`; that script builds OxiDD before
the native preset:

```
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
./scripts/compile.sh
./scripts/acacia-bonsai.sh -f 'G F req -> G F grant' -i req -o grant
cat examples/realizable.tlsf | ./scripts/acacia-bonsai.sh --tlsf
cat examples/realizable.tlsf | \
  ./scripts/acacia-synthesis.sh --tlsf > controller.aag
```

The CLI example intentionally omits `--rm`: compilation happens inside the
named container, so removing it on exit would discard the binaries. Re-enter
it with `docker start -ai acacia`.

The wrappers name no configuration and follow the `docker_default` group. Pass
a name as the first argument to choose a different shipped configuration, or run
a wrapper without arguments to list the ones it accepts. The CLI launcher reads
`config/docker-default.list`, which tests keep aligned with the registry group;
Python is only needed when compiling configurations. The GR(1) pre-pass is a
decision arm: LTL `-f` input and synthesis `-s` use the regular solver arms.
The sparse formula configuration remains available in the group as the
non-native fallback, with the Spot obligation arm alongside its four regular arms.

The default portfolio also includes an exact [parity game arm](benchmarking/coverage-first/parity-arm.md)
and a [Spot obligation arm](benchmarking/coverage-first/obligation-helper.md) for
syntactic obligations; run the latter alone with `--arms both:obligation:spot`.

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

Every build includes OxiDD and native GR(1), and requires Cargo (Rust 1.91+),
`cbindgen` (0.29.4), `flex` and `bison`; Meson builds the native library automatically.
Initialize the nested sources after cloning:
```
git submodule update --init --recursive
```

The corpus test driver (`tests/check-real-correct.sh`) can optionally run the
solver under Valgrind (`-V`) or Callgrind (`-c`); no test does so by default,
so Valgrind is only needed if you ask for it.

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

This produces a debug build. Build an optimized registered configuration with:
```
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -1)
./self-benchmark.sh -c $preset -R
```

The `-c` option selects a configuration and the `-R` option disables the
benchmarking step, so that only setup and compilation are done; the build lands
in `build_<preset>`. If compilation memory is tight, add `-L` to use the
low-memory compile profile.

Every build includes the native TLSF frontend and requires Flex and Bison:
```
build_$preset/src/acacia-bonsai -T spec.tlsf
```
`-T/--tlsf FILE` parses TLSF natively and the wrapper accepts TLSF on standard
input with `--tlsf`. No external TLSF converter is needed at runtime, and
Moore-target controller conversion happens inside Acacia.

The first `docker_default` preset includes `both:gr1-real-lift:oxidd` in its
default TLSF decision portfolio. Every `acacia-bonsai` build accepts
`--arms real:gr1:oxidd`, `--arms unreal:gr1:oxidd`, `--arms both:gr1:oxidd`,
`--arms both:gr1-lift:oxidd`, `--arms both:gr1-real-lift:oxidd`,
and `--arms real:param-lift:oxidd` with `-T spec.tlsf`. These arms run in
forked worker processes and can also be selected explicitly. A worker that
terminates on a signal contributes no verdict; the remaining arms continue.

At process start, before any threads exist, Acacia and the tlsf-tools CLIs set
`OXIDD_STACK_SIZE` when it is unset and `RLIMIT_AS` is finite, using
`min(1 GiB, max(8 MiB, RLIMIT_AS / 16))`. An explicit `OXIDD_STACK_SIZE` always
wins. Library manager construction does not change the environment. Python
module imports may occur after other threads have started, so the module does
not set this default; Python hosts can set the variable before starting threads.

The GR(1) reduction is exact, so one solve decides the winner: `both:gr1:oxidd` reports whichever side it proves, while
`both:gr1-lift:oxidd` first tries a checked system or environment lift from
shared seeds and falls back to the same exact game solve.
`both:gr1-real-lift:oxidd` tries only the checked system lift; unreal seeds and
declined lifts go directly to the exact game solve. The `real` and
`unreal` forms answer only for their own side. The former Python lifting route lives only in
[the research oracle](benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py) for
differential tests.

Correctness and performance gates, including the sequential measurement
protocol, are documented in [benchmarking/README.md](benchmarking/README.md).
The comparison with `ltlsynt` is in
[benchmarking/RESULTS.md](benchmarking/RESULTS.md).

# Compile-time configurations

Acacia-Bonsai's optimized variants are compile-time configurations. The registry
holds the options in `config/acacia-options.json` and the presets that combine
them in `config/acacia-presets.json`; `scripts/acacia-config.py` validates
presets and translates them to Meson options, and `meson.build` writes the
result into `acacia_build_config.hh`.

The options pick data structures and algorithms, for instance:

- `acacia_vector_downset`: the downset representation (`vector_backed` or
  `rank_bucketed_vector_backed`), storing SIMD vectors.
- `acacia_forward_safety_solver`: use the on-the-fly forward safety-game solver.
- `acacia_ios_precomputer`: input/output precomputation (`standard`, `mona`,
  `semantic_mona`).
- `acacia_translation_pref`: the Spot translator preference (`small` or `any`).
- `acacia_enable_equivariant_solver`: compile the exact equivariant solver,
  which declines to the classic solver when no verified profitable symmetry is available.

K starts at 2, increases by 3, and stops at 99 by default. The CLI's K options
can override these bounds and increment. Specification decomposition and the
realizability-preserving simplifier are always enabled on their applicable paths.

Some configurations take Spot's on-the-fly paths instead of the frozen automaton
graph, gated by `-Dacacia_spot_guarded_backend` and `-Dacacia_spot_lazy_provider`
and selected per polarity through `--arms`; current results and archived
provider evidence are indexed in [benchmarking/RESULTS.md](benchmarking/RESULTS.md).

Inspect the registry with:
```
python3 scripts/acacia-config.py validate
python3 scripts/acacia-config.py list-presets              # names only
python3 scripts/acacia-config.py list-presets --long       # with role and description
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -1)
python3 scripts/acacia-config.py show "$preset"            # resolved options
python3 scripts/acacia-config.py show "$preset" --describe
python3 scripts/acacia-config.py meson-args "$preset"
```

The configurations we ship are whichever ones the `docker_default` group names:

```
python3 scripts/acacia-config.py list-group docker_default
```

The Docker images, the TLSF examples above and CI all read that group. A preset
name is a historical label, not a claim about which configuration is currently
best.

Each preset declares a `role`: `shipping` for `docker_default` members,
`reference` for measurement controls, `sweep` for comparison arms, `diagnostic`
for instrumented builds, `legacy` for historical configurations.
Retained historical presets also identify frozen baselines in
`benchmarking/baselines.tsv`. New names are limited to 40 characters and cannot
contain `best`, while existing names are grandfathered.

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
