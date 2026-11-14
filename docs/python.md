# Python bindings

Build with `-Dbuild_python=true` as shown in the [README](../README.md#build).
The module is named `acacia_boomslang`; the distribution is named
`acacia-boomslang`. From the repository root in Bash, expose the build's module:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
python3 tests/check-python-preset.py "$build_dir"
export PYTHONPATH="$(realpath "$build_dir")/src/python${PYTHONPATH:+:$PYTHONPATH}"
python3 -c 'import acacia_boomslang'
```

Use the same Python interpreter that Meson found during setup. If Spot's
shared libraries are outside the loader's search path, add their directory
to `LD_LIBRARY_PATH` on Linux or `DYLD_LIBRARY_PATH` on macOS. Spot's Python
package is optional for the example below. Notebooks that import `spot` also
need its Python bindings for that interpreter; add their site-packages
directory to `PYTHONPATH` if needed.

## Solve a bounded safety game

The Python API exposes the automaton and safety-game solver. Pass the
**negation** of the desired LTL specification to `create_twa`. This builds a
universal co-Büchi automaton, which tracks plays that violate the specification.
The CLI handles this step itself.

Run this in the shell where you set `PYTHONPATH`:

```bash
python3 - <<'PY'
import acacia_boomslang as ab

game = ab.create_twa('!(G F req -> G F grant)', ['req'], ['grant'])
k = 2
ab.preprocess_aut_standard(game, k_max=k)
ab.set_bool_thresh_no_bool_states(game, k_max=k)
result = ab.solve_acacia_safety_game(game, k_max=k, k_min=k, k_inc=1)
print('Found a winning region:', result.is_real())
if result.is_real():
    region = result.get_winning_region()
    print('Initial state is winning:', region.contains(ab.get_initial_state(game)))
PY
```

Both lines print `True`. The bound `k` limits visits to accepting states in
the automaton. A winning region at a finite bound proves realizability.
`is_real()` returning `False` means this bounded search found no winning
region; it does **not** prove the LTL specification unrealizable. Use the
[CLI](cli.md) for its portfolio of decision procedures.

## Run the examples

These scripts use the same module path:

```bash
python3 python_examples/example_real.py
python3 python_examples/example_unreal.py
python3 python_examples/example_ucb_builder.py
python3 python_examples/example_simulate.py auto
```

`example_real.py` prints a winning region. `example_unreal.py` demonstrates a
bounded search with no winning region. `example_ucb_builder.py` shows state
vectors and their successors. `example_simulate.py auto` takes eight steps
using winning outputs; omit `auto` to choose inputs and outputs interactively.

## Notebooks

The [controller simulation](../python_examples/example_simulate.ipynb) and
[UCB helper](../python_examples/example_ucb_builder.ipynb) walk through the same
operations in Jupyter. The [grid-world notebook](../python_examples/example_shield_rl.ipynb)
shows the model, its LTL properties, and their automata. Local notebooks also
need Jupyter, Spot's Python bindings, and Graphviz. The
[Boomslang image](../Docker/README.md#use-python-notebooks) includes them.

## Current limitation

In the first `docker_default` debug build, the bounded Python solver reports
a winning region for `finite_mdp.bare_ltl()`, while Spot's `ltlsynt` and the
CLI's `both:parity:spot` worker both report `UNREALIZABLE`. Treat that Python
result as a known discrepancy until
[#393](https://github.com/gaperez64/acacia-bonsai/issues/393) is resolved. Its region also allows moves that leave the
grid in the example with the gate-opening assumption. Use the grid-world
notebook to inspect the model and translation, and the CLI parity worker to
check the bare formula's verdict.

## Packaged builds

Source and wheel builds resolve the first `docker_default` preset at build
time. The selected downset implementation and other compiled components are
fixed when the package is built.

Wheel builds use Meson's release build type with optimization level 3, keep
`acacia_compiler_profile=debug`, and set `tlsf-tools:cpu=baseline`. This avoids
the local release profile's CPU-specific flags. The preset's `native` SIMD
width then follows the compiler's target ABI. The wheel contains the Python
module; it does not install the CLI.
