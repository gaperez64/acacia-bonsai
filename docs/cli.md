# Command-line guide

Build the CLI with the [README instructions](../README.md#build). Run the
examples from the repository root in Bash. Select that build directory:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
"$build_dir/src/acacia-bonsai" --help
"$build_dir/src/acacia-bonsai" --version
```

`--help` exits with status 0. `--version` prints the version and configuration
but exits with status 2; allow for that when using it in a script.

## Read a specification

Use `-f` for a formula and `-i` and `-o` for comma-separated input and output
names. Quote the formula so the shell leaves its operators intact. Declare
every proposition, and use an empty string for an empty input or output list:

```bash
"$build_dir/src/acacia-bonsai" -f 'G (req -> F grant)' -i req -o grant
"$build_dir/src/acacia-bonsai" -f 'G grant' -i '' -o grant
```

`-F` reads a formula from a file; it still needs `-i` and `-o`. For example,
the checked-in formula below uses input `p_0` and output `acc`:

```bash
"$build_dir/src/acacia-bonsai" -F tests/ltl/realizable/ltl2dba_U1_1.ltl -i p_0 -o acc
```

`-T` (or `--tlsf`) reads a TLSF file, including its input/output declarations
and timing semantics. TLSF parsing is built in and needs no external converter:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf
"$build_dir/src/acacia-bonsai" -T examples/unrealizable-small.tlsf
```

The first file is realizable; the second is unrealizable and exits with
status 1. The CLI uses Mealy timing for raw LTL: the controller can see the
current inputs before choosing outputs. Use TLSF to specify Moore timing,
where it chooses outputs before seeing the current inputs.

## Read the result

| Verdict | Exit status | Meaning |
|---|---:|---|
| `REALIZABLE` | 0 | A controller can satisfy the specification. |
| `UNREALIZABLE` | 1 | No controller can satisfy it. |
| `UNKNOWN` | 2 | The selected workers did not decide it within their limits. |
| Error | 3 | The input, options, or execution failed; read stderr. |

Check the exit status when scripting. Status 1 is a valid answer and stops
a shell using `set -e`. Solver workers may also print diagnostics to stderr.

## Write a controller

`-s FILE` writes an ASCII AIGER controller when the answer is `REALIZABLE`.
The [quick start](../README.md#quick-start) shows a complete call.

For TLSF, the controller follows the declared target timing, including Moore
targets. Request synthesis in the solver call; a verdict alone does not write
a controller. The [Docker synthesis wrapper](../Docker/README.md#write-a-controller)
can send the circuit to stdout.

`--parity-mealy-reduction none|bisim|bisim-output|reselect` selects the parity route's
post-extraction reduction under `-s` only. `bisim-output` uses Spot's
bisimulation with output assignment (simplification level 2); `bisim` uses level 1.
Both preserve realization by specializing the selected controller's allowed output
traces. `none` restores the previous extraction and encoding pipeline. Decision
runs do not execute any reduction. Machines above 512 states, 8,192 edges, or
65,536 label BDD nodes (summed over edges) skip the optional work. SAT minimization
is deliberately excluded because Spot's API offers no cancellation budget.
The default, `reselect`, additionally re-solves a copy of small tier-4 arenas after sorting
outgoing edges by destination, then applies level 2. It retains the alternative
only if its (states, edges) pair is smaller. The second solve is skipped above
2,048 arena states, 8,192 arena edges, or four acceptance sets. It does not change
the decision solve, winning regions, or exported game certificates.

`--aiger-mode MODE` selects Spot's runtime encoder for every synthesis route.
The default encoder remains `isop`. Use `ite` (if-then-else),
`isop` (irredundant sum of products), or `both` (try both and keep the smaller encoding).
Append `+ud` to try negated conditions too, and
`+sub0`, `+sub1`, or `+sub2` for no splitting, input/latch/gate splitting (ISOP),
or common-subexpression search. Modifiers may appear in any order; repeated
modifiers follow Spot's behavior (the last `subN` wins). For example:

```bash
"$build_dir/src/acacia-bonsai" -T examples/realizable.tlsf -s controller.aag \
  --aiger-mode both+ud
```

Spot 2.16 also accepts `optim` (experimental global BDD encoding) and a
comma-separated list of alternatives, retaining the circuit with the fewest
gates. Acacia accepts a canonical subset of Spot's syntax: no empty components.
Empty methods/modifiers (including trailing `+` or `,`) and unknown tokens are
rejected before solving, both by the CLI parser and direct C++ entry points.
The flag is case-sensitive; the dual modifier is spelled `+ud`, not `+dual`.
This option changes encoding only; strategy extraction and the selected reduction
are independent. Compare sizes only after independent verification.
The `+dc` output don't-care modifier is rejected before solving, including in
comma-separated alternatives. The smoke sweep found incorrect controllers with
Acacia's original BDD dictionary; fresh-dictionary HOA replay did not reproduce
them. The cause may involve Acacia's machine/BDD-variable handling as well as
Spot; see issue #466 and the
[measured rejections](../benchmarking/RESULTS.md). The default remains `isop`.

## Choose solver workers

The default portfolio runs several solver workers, called **arms**. They try
different ways to prove realizability or unrealizability. Start with the build
default. For experiments, `--arms` replaces the whole portfolio:

```bash
"$build_dir/src/acacia-bonsai" -f 'G F req -> G F grant' -i req -o grant \
  --arms real:small:backward,unreal:formula:forward
```

Here `real:small:backward` tries to prove realizability with a small-automaton
translation and backward search. `unreal:formula:forward` tries to prove
unrealizability through a formula transformation and forward search.
Available arms depend on the compiled components; see the
[configuration guide](configuration.md) and `--help`.

`-r LIST` selects realizability checks using `small` or `any` translation
preferences. `-u LIST` selects unrealizability checks using `formula`,
`automaton`, or `both`. Each selects only its stated direction unless the
other is also supplied. Both require a value. Do not combine them with
`--arms`.

`-K` sets the final counter bound; `-M` sets its starting value and `-I` its
increment. Reaching the bound without a proof can give `UNKNOWN`. Keep the
defaults unless you need to control this search.
