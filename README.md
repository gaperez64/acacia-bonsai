[![CI](https://github.com/gaperez64/acacia-bonsai/actions/workflows/main.yml/badge.svg)](https://github.com/gaperez64/acacia-bonsai/actions/workflows/main.yml)

# Acacia-Bonsai

This is a modern implementation of universal co-Buchi reactive synthesis
algorithms using antichain data structures.  The theory and practice is described in:

   https://arxiv.org/abs/2204.06079
   
# Dependencies

This program depends on:
- [Boost C++ Library](https://www.boost.org/)
- A modern C++ compiler (C++20 is used)
- [The Meson Build System](https://mesonbuild.com/)
- [The Spot Library](https://spot.lrde.epita.fr/): Spot is automagically
  compiled as a submodule; however, it takes quite a bit of time to do so, and
  each build configuration of Acacia-Bonsai will recompile Spot.  We recommend
  that Spot be separately compiled and installed.
- [The Z shell](https://www.zsh.org/), for some scripts.

# Compiling, running, benchmarking

To compile and run, use Meson:
```
$ meson setup build
$ cd build
$ meson compile
$ src/acacia-bonsai --help
  [...]
$ src/acacia-bonsai -f '((G (F (req))) -> (G (F (grant))))' --ins req --outs grant
REALIZABLE
```

Note that this will compile a debug version of Acacia-Bonsai.  A benchmarking
script is available at the root:

```
$ ./self-benchmark.sh --help
```

In particular, it can be used to build an optimized version of Acacia-Bonsai:
```
$ ./self-benchmark.sh -c best -B
  [...]
$ cd build_best
$ src/acacia-bonsai --help
$ src/acacia-bonsai -f '((G (F (req))) -> (G (F (grant))))' --ins req --outs grant
REALIZABLE
```

The `-c` option selects a configuration and the `-B` option deactivates actual
benchmarking, so that only compilation is done.

## Compiling for StarExec
You will need some wrapping script (see `starexec` directory). Additionally,
you will need to compile in an `x86_64` machine with SIMD deactivated and
everything statically linked because StarExec runs on an old linux with old
libraries. For instance:
1. Set up a meson build library with 
```
CXXFLAGS="-DNO_SIMD -DNDEBUG" meson setup $BUILD_DIR --buildtype=release --prefer-static --default-library=static
```
2. Print the compilation command with `meson compile -vC $BUILD_DIR` for acacia-bonsai and add
   `-static` to ensure everything is statically linked.

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
