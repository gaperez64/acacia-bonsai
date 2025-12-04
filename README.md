# Acacia-Bonsai

This is a modern implementation of universal co-Buchi reactive synthesis
algorithms using antichain data structures.  The theory and practice is described in:

   https://arxiv.org/abs/2204.06079
   
# Dependencies

This program depends on:
- [Boost C++ Library](https://www.boost.org/)
- A modern C++ compiler (C++20 is used)
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

Boost has to be compiled using GCC and installed in a way that meson can find Boost.
It might be true that the only boost libraries used in this project are header only.
In that case, Boost can be installed using Homebrew.

Spot has to be manually compiled using GCC and installed.
It is necessary to ensure that meson can find Spot using `pkgconfig`.
This can be done, for example, by setting the `pkg_config_path` in a meson-native file.

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
UNKNOWN
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
