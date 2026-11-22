# Command line

[Build the CLI](../README.md#build), then run from the repository root in Bash:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
"$build_dir/src/acacia-bonsai" --help
"$build_dir/src/acacia-bonsai" --version
```

`--help` exits 0; `--version` prints version/configuration and exits 2.

## Read a specification

`-f` takes a quoted formula; `-i` and `-o` take comma-separated input/output
names. Declare every proposition; use an empty string for an empty list:

```bash
"$build_dir/src/acacia-bonsai" -f 'G (req -> F grant)' -i req -o grant
"$build_dir/src/acacia-bonsai" -f 'G grant' -i '' -o grant
```

`-F` reads a formula file and still requires `-i`/`-o`:

```bash
"$build_dir/src/acacia-bonsai" -F tests/ltl/realizable/ltl2dba_U1_1.ltl -i p_0 -o acc
```

`-T`/`--tlsf` reads TLSF, including I/O declarations and timing, without an
external converter:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf
"$build_dir/src/acacia-bonsai" -T examples/unrealizable-small.tlsf
```

The first TLSF example is realizable, the second unrealizable (exit 1).
Raw LTL uses Mealy timing: the controller sees current inputs before choosing
outputs. TLSF can specify Moore timing, where it chooses first.

## Read the result

| Verdict | Exit | Meaning |
|---|---:|---|
| `REALIZABLE` | 0 | A controller can satisfy the specification. |
| `UNREALIZABLE` | 1 | No controller can satisfy it. |
| `UNKNOWN` | 2 | Workers did not decide within their limits. |
| Error | 3 | Invalid input/options or execution failure; read stderr. |

Status 1 is a valid answer and stops shells using `set -e`. Worker diagnostics
also go to stderr.

## Write a controller

`-s FILE` writes ASCII AIGER after a REALIZABLE result, respecting the TLSF target,
including Moore. See the [quick start](../README.md#quick-start).
The [Docker wrapper](../Docker/README.md#write-a-controller) can write to stdout.

## Choose solver workers

The portfolio runs workers called arms. `--arms` replaces the whole portfolio:

```bash
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant \
  --arms real:small:backward,unreal:formula:forward
```

`real:small:backward` proves realizability with small-automaton translation and
backward search. `unreal:formula:forward` proves unrealizability through a formula
transformation and forward search. Compiled components determine available arms;
see [configuration](configuration.md) and `--help`.

`-r LIST` selects realizability with `small`/`any` translation; `-u LIST` selects
unrealizability via `formula`, `automaton` or `both`. Each enables only its own
direction unless both are supplied. Both need values and cannot combine with
`--arms`.

`-K` sets the final counter bound, `-M` its starting value and `-I` its increment.
Reaching the bound without proof can return UNKNOWN.
The default counter schedule starts at 2, increments by 3 and ends at 99.
