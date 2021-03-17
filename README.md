# Acacia bonsai

This is a minimal implementation of universal co-Buchi reactive synthesis
algorithms using antichain data structures.

## Submodules and dependencies
The repository contains submodules, so remember to run:
`git submodule update --init`

Additionally, the tool is built using the [spot library](https://spot.lrde.epita.fr/install.html).
Make sure to install `spot-devel`, ideally using your package manager. We also have scripts
written for Z shell so make sure to install `zsh`.

Finally, if you want to run the Acacia+ benchmarks/tests, you will need pygraph.
[This](https://github.com/Shoobx/python-graph) is the last
location of a Python3-friendly version known to us.

## Compilation
Use meson:
```
meson build && cd build && meson compile
```
