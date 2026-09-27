# Milestone-1 per-arm preparation

This directory prepares the six **standalone** SYNTCOMP26 legs. No full-corpus
60 s leg has been run here. Run the commands below serially on the quiet timing
host. The binary is still writable for the owner to freeze. Do not rebuild it
between legs. Do not use the obfuscated-name corpus for these selection legs.

`binary-manifest.json` records the measurement executable and build provenance.
Its SHA-256 is
`22455c945576d28d86ac98a3b512327740b12f0cc3a3a6006ad1c59dfe9c9584`.
The executable was built from Acacia `3dc9fdf38b30dbc8fc85609a32ca7280ae206529`
in fresh `build_perarm_m1/`, with one compile job, B's `otf_sparse_formula`
settings and normal release/O3/LTO/native profile, plus
`acacia_native_arms=true`. All shared Acacia and release-profile Meson options
match B's saved build; the native subproject enables OxiDD.
The patched tlsf-tools/OxiDD build was verified before Meson setup. `--help`
prints both native GR(1) spellings, though this CLI treats that long help flag
as unrecognized and returns 3; the usage text is still printed. The generated
config and the no-`--arms` code path confirm the unchanged B default:

1. `real:small:backward`
2. `real:small:forward`
3. `unreal:formula:spot-guarded-sparse`
4. `unreal:automaton:forward`

The additional milestone-1 arms are `real:gr1:oxidd` and
`unreal:gr1:oxidd`.

## Six original-name legs at 60 s

Run from the Acacia repository root. `run-syntcomp26-coverage.py` appends
`-T <mapped TLSF>` to the one-arm flag string, scopes the whole solver tree
at 8 GiB with no swap, and collects GNU time resource usage. `all.list`
contains exactly 1,524 distinct original-name inputs. The loop runs one
complete leg and its `export-cactus` step before starting the next. Each
leg has its own output directory and uniform 60 s cap.

```bash
set -euo pipefail
ROOT=$(pwd)
CAMPAIGN="$ROOT/benchmarking/gr1-par2-20260923/campaign"
OUT="$CAMPAIGN/perarm-m1"
LIST="$ROOT/tests/suites/benchmarks/syntcomp26/all.list"
MAP="$ROOT/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
CORPUS="$ROOT/tlsf-corpus"
BIN="$ROOT/build_perarm_m1/src/acacia-bonsai"
echo '22455c945576d28d86ac98a3b512327740b12f0cc3a3a6006ad1c59dfe9c9584  '"$BIN" | sha256sum -c -
test "$(rg -vc '^#|^$' "$LIST")" = 1524
arms=(
  real:small:backward
  real:small:forward
  unreal:formula:spot-guarded-sparse
  unreal:automaton:forward
  real:gr1:oxidd
  unreal:gr1:oxidd
)
for i in "${!arms[@]}"; do
  arm="${arms[$i]}"
  label="arm-$((i+1))"
  out="$OUT/legs/$label"
  mkdir -p "$out"
  python3 benchmarking/run-syntcomp26-coverage.py \
    --bin "$BIN" --flags "--arms $arm" \
    --solver-label "$label" --preset otf_sparse_formula \
    --acacia-sha 3dc9fdf38b30dbc8fc85609a32ca7280ae206529 \
    --list "$LIST" --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" \
    --caps 60 --memory-max 8G --memory-swap-max 0 \
    --collect-rusage --output "$out/$label-cap60.tsv"
  python3 benchmarking/run-syntcomp26-coverage.py export-cactus \
    --summary "$out/$label-cap60-summary.tsv" \
    --list "$LIST" --cap 60 --output "$out/$label-cap60.csv"
done
```

If interrupted, resume **only the interrupted leg** with the same command and
`--resume`, then export after all 1,524 rows are present. Do not run multiple
legs at once. The runner's conflict policy stops a leg on a verdict conflict;
investigate that row before using the exported CSV.

## Fixed portfolio choice rule

After all six exports, run:

```bash
python3 "$CAMPAIGN/perarm-select.py" --list "$LIST" \
  --leg "real:small:backward=$OUT/legs/arm-1/arm-1-cap60.csv" \
  --leg "real:small:forward=$OUT/legs/arm-2/arm-2-cap60.csv" \
  --leg "unreal:formula:spot-guarded-sparse=$OUT/legs/arm-3/arm-3-cap60.csv" \
  --leg "unreal:automaton:forward=$OUT/legs/arm-4/arm-4-cap60.csv" \
  --leg "real:gr1:oxidd=$OUT/legs/arm-5/arm-5-cap60.csv" \
  --leg "unreal:gr1:oxidd=$OUT/legs/arm-6/arm-6-cap60.csv"
```

`perarm-select.py` checks the six 1,524-row exports, their 60 s raw sidecars,
and conflicting decisive verdicts. For every 4- and 5-arm subset, it takes the fastest **decisive**
isolated-arm time per input, scores unsolved inputs at 120 s through
`benchlib.par2`, and ranks by **most solved, then lowest PAR-2**. It prints
the top 10 subsets and, for each, the count solved by each arm alone within
that subset, plus each arm's unique count across all six. This virtual best is
a selection aid only. The chosen concurrent
portfolio still needs its own 60 s measurement before its score is reported.

## Smoke check, 17 s cap

The coverage runner ran all six one-arm configurations on these three mapped
original-name inputs with an 8 GiB/no-swap scope and `--collect-rusage`.
`01.ltl` is a small REAL input; `CheckAlarm_6bea956e.ltl` is a small UNREAL
input; `AlarmMin_efe2feaf.ltl` is a small non-reducible input according to
`generic-census.tsv` (`unsupported_or_failed`). The first sandboxed attempt
could not access the user scope bus and produced only harness `ERROR` rows;
the table is from the successful scoped rerun in `smoke/arm-{1..6}.tsv`.

| Arm | REAL | UNREAL | non-reducible |
|---|---|---|---|
| `real:small:backward` | REALIZABLE, 0.037 s | UNKNOWN, 0.028 s | REALIZABLE, 0.027 s |
| `real:small:forward` | REALIZABLE, 0.036 s | UNKNOWN, 0.038 s | REALIZABLE, 0.046 s |
| `unreal:formula:spot-guarded-sparse` | UNKNOWN, 0.772 s | UNREALIZABLE, 0.024 s | UNKNOWN, 2.867 s |
| `unreal:automaton:forward` | UNKNOWN, 0.088 s | UNREALIZABLE, 0.024 s | UNKNOWN, 2.269 s |
| `real:gr1:oxidd` | REALIZABLE, 0.171 s | UNKNOWN, 0.038 s | UNKNOWN, 0.079 s |
| `unreal:gr1:oxidd` | UNKNOWN, 0.105 s | UNREALIZABLE, 0.039 s | UNKNOWN, 0.043 s |

All 18 rerun rows used the manifest's binary hash. There were no crashes or
timeouts. The native arms decline the non-reducible case.

## Wall-time planning

The archived B epoch-1 rows censored to 60 s contain 1,204 solves and 320
timeouts. Their summed row durations are **20,911.66 s (5.81 h)**; epoch 2 is
20,914.03 s. These are row-time totals from a derived 120-to-60 s series,
not a fresh 60 s leg or a bound on any standalone arm. Runner startup and
cooldown add wall time. The earlier campaign README's 20,754 s row-time note
does not match the retained 60 s TSV; the values here were recomputed from
all 1,524 `seconds` fields.

| Leg | Planning wall time | Basis |
|---|---:|---|
| Each of arms 1–4 (legacy) | 6–12 h | Owner's range for a standalone 60 s arm; B's 5.81 h portfolio row time is context, not an arm prediction. |
| Each of arms 5–6 (native GR(1)) | about 10–11 h reserved | Generic census found 572 reducible and 952 non-reducible inputs. Charging all 572 at 60 s plus the latter's 3,360.69 s observed reduction time gives 37,680.69 s (10.47 h) before scope overhead. This is a capacity proxy, not an observed native runtime or a guaranteed upper bound. |

The strict 60 s admission census admitted 504/1,524 under its 1 s gate;
charging all 504 at 60 s plus its 597.51 s census time gives an alternate
8.57 h proxy. The native path can differ from that Python census, so reserve
the wider 10–11 h for each native leg. Six serial legs are roughly 45–70 h of
solver and harness time under these assumptions; plan for 2–3 days.

## VERDICT

The milestone-1 measurement binary is built and identified, the six exact
60 s commands and fixed selection rule are ready, and all 18 scoped smoke
checks completed without a verdict conflict. Full-corpus timed legs remain to
be run by the owner, serially, after freezing the binary.
