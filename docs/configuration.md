# Presets and runtime options

A preset selects components when you compile: downset storage, automaton
preprocessing, search support, and a default portfolio. Runtime flags choose
among the components that build contains.

`--oracle-layout grouped|scan` selects the sparse search and verifier layout;
`grouped` is the default.

## Inspect a preset

From the repository root:

```bash
python3 scripts/acacia-config.py validate
python3 scripts/acacia-config.py list-group docker_default
python3 scripts/acacia-config.py list-presets --long
python3 scripts/acacia-config.py list-presets --unique
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -n1)
python3 scripts/acacia-config.py show "$preset" --describe
python3 scripts/acacia-config.py meson-args "$preset"
```

`docker_default` lists the configurations accepted by the Docker wrappers. Its
first entry is also the Python build default. Names such as `best_…` are
historical labels; use the group to find the current choices. The ordered slots
are the six-arm default, its eight-arm forward-inclusive variant, the historical
backward solver, and the previous second slot's six-arm GR(1) portfolio. The
second slot changes only `default_arms` from the first, appending
`real:small:forward` and `unreal:automaton:forward`. `list-presets --unique`
lists one name for each distinct set of component options, ignoring the label
used to record its origin.

[acacia-options.json](../config/acacia-options.json) defines the options;
[acacia-presets.json](../config/acacia-presets.json) defines presets and groups.
Use `meson-args` to translate a preset into build options, as in the [build
instructions](../README.md#build). Use a fresh build directory when changing
compiler, compile profile, or native file. The build wrappers reject directories
whose recorded build identity no longer matches.

The first two `docker_default` presets use
`unreal:formula:spot-guarded-sparse:closure-recurrence`, with candidate mode
`fallback` and `low` effort for fallback translation through `frozen-graph`.
They set `acacia_default_candidate_mode=fallback`; the option's general default
is `only`. `--candidate-mode only|fallback` overrides the build default
globally. During synthesis (`-s`), the default formula arm uses `frozen-graph`.
Explicit closure-provider selections remain decision-only.

## Compile profiles

| Profile | Settings | Use |
|---|---|---|
| `debug` | Debug build, no local release flags | Examples, debugging, tests |
| `normal` | Release build, optimization 3, LTO, local CPU flags | Local performance measurements |
| `checked` | Debug-optimized build, no LTO, assertions enabled | Larger correctness suites |
| `lowmem` | Optimization 0, no LTO, one compile job by default | Builds with limited memory |

[acacia-build.py](../scripts/acacia-build.py) defines these profiles. `normal`
sets `acacia_compiler_profile=release`, adding `-Ofast -march=native`; its
binaries may not run on another CPU.

For `self-benchmark.sh`, select `normal` (default), `checked` or `lowmem` with
`BENCHMARK_COMPILE_PROFILE`; `-L` also selects `lowmem`. Debug builds use the
README's Meson command. `BENCHMARK_COMPILE_JOBS` and `BENCHMARK_TEST_JOBS`
control parallelism and both default to 1.

`acacia_simd_width` is a compile-time choice: `native` (default), `compatible`
(128 bits), or `max` (largest fixed size). It does not dispatch at runtime.

## Translation effort

`--translation-effort` accepts `high`, `medium`, and `low`, with optional
overrides for named arms. This example lowers one worker's effort:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant \
  --arms real:small:backward,unreal:formula:forward \
  --translation-effort high,unreal:formula:forward=low
```

The Meson option `acacia_default_translation_effort` uses the same syntax for a
build default. A runtime value replaces that default entirely; arms without an
override then use `high` unless a bare level sets another default.

Overrides apply to selected arms using the `frozen-graph` provider and to
`closure-recurrence` arms in `--candidate-mode fallback`, where effort controls
the fallback translation. A runtime override for an inactive arm or unsupported
provider is an error. Build-default overrides without an active translating arm
are ignored. Add `:provider` to an arm name if it would otherwise match more
than one selected worker.

The `closure-recurrence` construction uses the existing equal worker memory
share: half the smaller of the scope/address-space limit and 8 GiB, divided by
the selected worker count. `ACACIA_SPOT_MAX_PROVIDER_BYTES` overrides this
budget in bytes (zero forces failure). Both retained storage estimates and
sampled worker RSS enforce it, including BDD storage and allocator overhead.

## Parity arms

`both:parity:spot` translates the original positive specification with Spot's
high-level deterministic generic-acceptance translation through
`src/solver/parity/route.{hh,cc}`. It preserves declared I/O, including unused
signals. Raw LTL is Mealy; TLSF applies effective-Mealy lowering once and ordinary
controller conversion for Moore targets. Translation skips realizability
simplification, decomposition, I/O swapping and extra timing shifts.

`both:parity:oxidd` builds a tlsf-tools DPA from native TLSF. It tries the proved
grammar first, retrying only unsupported creation with general Boolean Π2/Σ2
construction. That general construction has offline checks on a recorded sample
against a Lean-proved construction; other formulas rely on tests and that evidence.
Both attempts share one construction deadline; resource declines do not retry.
The completed DPA feeds an exact parity game and supports synthesis.

Production trusts completed parity results and controllers; independent
checks run in tests.

The process supervisor handles cancellation and invocation deadlines.
`both:parity:spot` has no local wall allowance or solver-visible deadline.
`both:parity:oxidd` limits construction and import together to 120 seconds or the
earlier invocation deadline, then returns UNKNOWN. Workers use the obligation
helper's address cap and soft RSS share: half the available cap divided by
scheduled workers. Default `headroom` enforces shares only within an eighth of
`memory.max`; without a readable cap it enforces `equal` at all times.
Resource stops/declines supply no verdict; missing phase records stay unknown.
