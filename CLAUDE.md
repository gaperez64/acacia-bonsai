# Working in this repository

Acacia-Bonsai decides LTL realizability with antichains over downsets, and
synthesizes a controller when the answer is yes. The theory is in
[arXiv:2204.06079](https://arxiv.org/abs/2204.06079); this file is about the
things that are true of the repository but not visible from the code.

## The configuration registry is the source of truth

Optimized variants are **compile-time** configurations. `config/acacia-options.json`
declares every option with its Meson name and macro; `config/acacia-presets.json`
declares the presets and the groups. `scripts/acacia-config.py` validates them
and translates a preset into Meson arguments; `meson.build` writes the result
into a generated `acacia_build_config.hh`.

Three frontends have to agree — `meson.options`, the JSON registry, and the
`#ifndef` fallbacks in `src/config/acacia_build_config.hh.in`. CI checks this
before anything builds (`acacia-config.py validate`), and
`tests/check-config-frontends.py` checks it without needing a build. Adding an
option means adding it in all three.

**A group is a pointer; a preset name is a historical label.** The shipped
configurations are whatever `docker_default` currently names:

```
python3 scripts/acacia-config.py list-group docker_default
```

Do not hardcode a preset name in documentation, a script, or a workflow — read
the group. The `best` in existing names was accurate when those presets were
chosen and silently stopped being so, which is exactly how the README's Docker
examples came to name a preset the wrapper rejected.

## Building

```
meson setup build && meson compile -C build     # debug, -O0
./self-benchmark.sh -R -c <preset>              # optimized build_<preset>, no benchmark
```

`self-benchmark.sh` is the build-and-measure driver. Its compile profiles:

| `BENCHMARK_COMPILE_PROFILE` | what it is | for |
|---|---|---|
| `normal` (default) | `--buildtype=release -Doptimization=3 -Db_lto=true`, `acacia_compiler_profile=release` | measurement |
| `checked` | `debugoptimized`, no LTO, assertions kept | correctness suites too slow at `-O0` |
| `lowmem` | `-O0`, no LTO, one compile job | tight memory |

`acacia_compiler_profile=release` adds `-Ofast -march=native`, so **release
binaries are not portable and cannot be reproduced later** — the version is
embedded from git as well. This is why a frozen baseline binary is kept rather
than rebuilt (see below).

`BENCHMARK_COMPILE_JOBS` and `BENCHMARK_TEST_JOBS` both default to **1**, because
the default caller is measuring. Set them when you are not.

Known wart: `.acacia-config.json` in a build directory records only the *acacia*
options, so switching compile profile on an existing build directory silently
reuses the old objects. Use a fresh directory when changing profile.

## Testing and gates

`meson test -C build --suite unit` is the fast signal: ~40 tests, about seven
seconds. The corpus suites (`ab/tiny`, `ab/small`, `ab/issue54`, `synthesis`)
run the solver over LTL and TLSF instances and take much longer.

The gate protocol — G0 correctness through G5 native TLSF parity — is defined in
[benchmarking/README.md](benchmarking/README.md) and should be used as written
rather than reinvented. Two things about it are easy to get wrong:

- **G1's `--baseline-bin` has no default.** It must be the same configuration
  built from the previous revision. An earlier default pointed at a different
  configuration and silently answered a different question.
- **Read PAR-2 against the measured noise floor**: 21.1 s on SYNTCOMP25, 12.1 s
  on SYNTCOMP26. A change inside that spread is not evidence on its own;
  coverage changes and per-instance losses are.

Suites are dominated by instances sitting at the timeout cap rather than being
solved, so when one is slow, look at the timeout/solve split before reaching for
compiler flags — parallelism moves it and optimization largely does not.

## Artifacts

`build*/` and `_bm-logs*/` are gitignored and accumulate without limit. Two
signals mark the ones that are evidence rather than scratch, neither visible
from the name:

- a **write-protected** directory is deliberately frozen;
- a directory whose binary's **SHA-256 appears in a committed file** is
  provenance — campaign results carry a `binary_sha256` column, and
  `benchmarking/README.md` pins the frozen G1 baseline by hash.

`scripts/prune-artifacts.sh` encodes both. It reports by default and only
removes with `--delete`.

## Conventions

- Commit messages are sentences describing what changed and why, in the
  imperative, with no `type:` prefix. Look at `git log` before writing one.
- Python is linted with ruff using the configuration in `pyproject.toml`.
- `.clang-format` matches the house style (`foo (x)`, 99 columns) but is not
  enforced anywhere yet.
- Benchmark evidence is durable: sprint records under `benchmarking/` are kept
  rather than deleted, and measured numbers are carried forward verbatim.
