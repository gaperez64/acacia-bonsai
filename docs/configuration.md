# Presets and runtime options

A preset selects components when you compile: downset storage, automaton
preprocessing, search support, and a default portfolio. Runtime flags choose
among the components that build contains.

## Inspect a preset

Run these commands from the repository root:

```bash
python3 scripts/acacia-config.py validate
python3 scripts/acacia-config.py list-group docker_default
python3 scripts/acacia-config.py list-presets --long
python3 scripts/acacia-config.py list-presets --unique
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -n1)
python3 scripts/acacia-config.py show "$preset" --describe
python3 scripts/acacia-config.py meson-args "$preset"
```

`docker_default` lists the configurations accepted by the Docker wrappers.
Its first entry is also the Python build default. Names such as `best_…`
are historical labels; use the group to find the current choices.
`list-presets --unique` lists one name for each distinct set of component
options, ignoring the label used to record its origin.

[acacia-options.json](../config/acacia-options.json) defines the options;
[acacia-presets.json](../config/acacia-presets.json) defines presets and groups.
Use `meson-args` to translate a preset into build options, as in the
[build instructions](../README.md#build). Use a fresh build directory when
changing compiler, compile profile, or native file. The build wrappers reject
directories whose recorded build identity no longer matches.

## Compile profiles

| Profile | Settings | Use |
|---|---|---|
| `debug` | Debug build, no local release flags | Examples, debugging, tests |
| `normal` | Release build, optimization 3, LTO, local CPU flags | Local performance measurements |
| `checked` | Debug-optimized build, no LTO, assertions enabled | Larger correctness suites |
| `lowmem` | Optimization 0, no LTO, one compile job by default | Builds with limited memory |

These are wrapper profiles defined in
[acacia-build.py](../scripts/acacia-build.py). The `normal` profile sets the
Meson option `acacia_compiler_profile=release`, which adds `-Ofast` and
`-march=native`. Build on the machine where you will run it; that binary may
not run on another CPU. Debug timings do not predict release performance.

## Translation effort

`--translation-effort` accepts `high`, `medium`, and `low`, with optional
overrides for named arms. An arm is one solver worker in the portfolio.
This example explicitly selects two workers and lowers the unrealizability
worker's translation effort:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant \
  --arms real:small:backward,unreal:formula:forward \
  --translation-effort high,unreal:formula:forward=low
```

The Meson option `acacia_default_translation_effort` uses the same syntax
for a build default. A runtime value replaces that default entirely; arms
without an override then use `high` unless a bare level sets another default.

Overrides apply only to selected arms using the `frozen-graph` provider,
which translates the whole automaton before search. A runtime override for
an inactive arm or unsupported provider is an error. Build-default overrides
without an active `frozen-graph` arm are ignored. Add `:provider` to an arm
name if it would otherwise match more than one selected worker.
