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
$ meson build
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
