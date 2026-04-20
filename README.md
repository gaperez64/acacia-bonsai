# Acacia-Bonsai

This is a modern implementation of universal co-Buchi reactive synthesis
algorithms using antichain data structures.  The theory and practice is described in:

   https://arxiv.org/abs/2204.06079

# Docker images

Two pre-built images are published on the GitHub Container Registry. The
Jupyter image is the easiest entry point for exploring acacia-bonsai
interactively; the CLI image is what you want for benchmarking or scripting
synthesis runs.

## Boomslang image

This image comes with Spot (built with Python bindings), the Acacia Boomslang
Python interface (`acacia_boomslang`), and a Jupyter notebook server already
wired together. It is the quickest way to play with the tool — no compilation
required on your end.

Pull the image:
```
$ docker pull ghcr.io/gaperez64/acacia-boomslang:latest
```

**Note:** To specifically pull the `linux/amd64` image the following command can be used instead:
```
docker pull --platform linux/amd64 ghcr.io/gaperez64/acacia-boomslang:latest
```

Start the notebook server, exposing port 8888 on the host:
```
$ docker run --rm -p 8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
```

**Note:** To specifically run the `linux/amd64` image:
``
docker run --platform linux/amd64 --rm -p 8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
``

The container prints a URL with an access token (e.g.
`http://127.0.0.1:8888/tree?token=...`) — open it in your browser. The
working directory contains `python_examples/` with example scripts that
import both `spot` and `acacia_boomslang`.

To mount a host directory of your own notebooks instead of the bundled
examples:
```
$ docker run --rm -p 8888:8888 \
    -v "$PWD/my_notebooks:/work" \
    -e NOTEBOOK_DIR=/work \
    ghcr.io/gaperez64/acacia-boomslang:latest
```

## CLI image

A pre-built Docker image with all dependencies and sources is available from
the GitHub Container Registry. It ships sources only — compilation happens inside
the container so that `-march=native` picks up the host's SIMD instruction set.

Pull the image:
```
$ docker pull ghcr.io/gaperez64/acacia-bonsai:latest
```

Create a named container and start it interactively (note: we deliberately
do *not* pass `--rm` — compilation happens inside the container, so removing
it on exit would throw away the binaries you are about to build):
```
$ docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
```

Inside the container, compile Spot and a number of optimized acacia-bonsai
configurations:
```
$ ./scripts/compile.sh
```

This builds Spot from source and then compiles all configurations. Now,
you can run acacia-bonsai using the wrapper script:
```
$ ./scripts/acacia-bonsai.sh best_decomp_mona \
      -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
REALIZABLE
```

The wrapper also accepts TLSF specs piped on stdin (translated to LTL via
the bundled `syfco`):
```
$ cat spec.tlsf | ./scripts/acacia-bonsai.sh best_decomp_mona --tlsf
```

When you exit the shell the container stops but is preserved. To re-enter
it later with your compiled binaries intact:
```
$ docker start -ai acacia
```

From outside the container you can also pipe a TLSF spec into the running
(or stopped-then-started) container via `docker exec` / `docker start`:
```
$ docker start acacia   # if it is stopped
$ cat spec.tlsf | docker exec -i acacia \
      /opt/acacia-bonsai/scripts/acacia-bonsai.sh best_decomp_mona --tlsf
```

To synthesize a controller (AIGER/AAG format) and have it printed on stdout
when the spec is realizable, use `acacia-synthesis.sh`. It accepts the same
options as `acacia-bonsai.sh`; the REALIZABLE/UNREALIZABLE verdict goes to
stderr so stdout carries only the AAG:
```
$ cat spec.tlsf | docker exec -i acacia \
      /opt/acacia-bonsai/scripts/acacia-synthesis.sh \
      best_decomp_mona --tlsf > controller.aag
```

To see available configurations:
```
$ ./scripts/acacia-bonsai.sh
```

If you would rather not keep the `acacia` container around but still want
to reuse the compiled binaries later, snapshot the container into a new
image (we suggest the `compiled` tag to distinguish it from the source-only
`latest`):
```
$ docker commit acacia ghcr.io/gaperez64/acacia-bonsai:compiled
```

From then on you can spin up fresh, throwaway containers that already
have Spot and the acacia-bonsai configurations built in — `--rm` is fine
here because there is nothing left to compile:
```
$ docker run --rm -it ghcr.io/gaperez64/acacia-bonsai:compiled
$ cat spec.tlsf | docker run --rm -i \
      ghcr.io/gaperez64/acacia-bonsai:compiled \
      /opt/acacia-bonsai/scripts/acacia-bonsai.sh best_decomp_mona --tlsf
```

Once you are done with the original container for good, remove it
explicitly:
```
$ docker rm acacia
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
