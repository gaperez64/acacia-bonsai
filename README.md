# Acacia-Bonsai

This is a modern implementation of universal co-Buchi reactive synthesis
algorithms using antichain data structures.  The theory and practice is described in:

   https://arxiv.org/abs/2204.06079

# Docker image

A pre-built Docker image with all dependencies and sources is available from
GitHub Container Registry. It ships sources only — compilation happens inside
the container so that `-march=native` picks up the host's SIMD instruction set.

Pull the image:
```
$ docker pull ghcr.io/gaperez64/acacia-bonsai:latest
```

Run the container interactively:
```
$ docker run --rm -it ghcr.io/gaperez64/acacia-bonsai:latest
```

Inside the container, compile Spot and all four optimized acacia-bonsai
configurations:
```
$ ./compile.sh
```

This builds Spot from source and then compiles four configurations:
`best_decomp_kdtree_mona`, `best_decomp_kdtree_mona_no_bitsets`,
`best_decomp_mona_no_bitsets`, and `best_downset_vector_or_kdtree`.

Run acacia-bonsai using the wrapper script:
```
$ ./acacia-bonsai.sh best_decomp_kdtree_mona -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
REALIZABLE
```

To see available configurations:
```
$ ./acacia-bonsai.sh
```

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
$ meson setup build
$ cd build
$ meson compile
$ src/acacia-bonsai -h
  [...]
$ src/acacia-bonsai -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
REALIZABLE
```

Another usage:
```
$ src/acacia-bonsai -f '((G (F (req))) <-> (G(!grant) ))' -i req -o grant
UNREALIZABLE
```

Note that this will compile a debug version of Acacia-Bonsai.  A benchmarking
script is available at the root:
```
$ ./self-benchmark.sh -h
```

In particular, it can be used to build an optimized version of Acacia-Bonsai:
```
$ ./self-benchmark.sh -c best -B
  [...]
$ cd build_best
$ src/acacia-bonsai -h
$ src/acacia-bonsai -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
REALIZABLE
```

The `-c` option selects a configuration and the `-B` option deactivates actual
benchmarking, so that only compilation is done.

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
