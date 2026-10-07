# P1 matched R/equivariance ablation

This is a driver protocol; no campaign was run for this implementation. Use one
optimized executable for every leg, native TLSF input, the fixed 1,524-instance
SYNTCOMP26 manifest and source map, and fresh 17 s observations. The corpus is an
already inspected development set, not a holdout. Follow the G0–G5 and near-cap
adjudication rules in [the benchmarking protocol](../README.md).

## Runtime switches and attribution

Both switches accept `on` or `off`, work with `--arms` and the default portfolio,
and preserve the incumbent defaults. No new compile-time options are needed:
compile-time configuration still controls which implementations are available.
`--equivariance on` requires `acacia_enable_equivariant_solver=true`; with support
compiled out, the omitted switch defaults to off. Synthesis excludes equivariance
regardless of the switch. R is on by default.

| Leg | CLI switches | Native arm | Backward worker |
|---|---|---|---|
| A | `--r-prepass off --equivariance off` | direct in combined context | ordinary backward |
| B | `--r-prepass off --equivariance on` | direct in combined context | incumbent equivariance pre-pass |
| C | `--r-prepass on --equivariance off` | incumbent R, then direct fallback | ordinary backward |
| D | `--r-prepass on --equivariance on` | incumbent R, then direct fallback | incumbent equivariance pre-pass |

Use `both:gr1-real-lift:oxidd` throughout, so U is off. `--r-prepass` governs R
inside combined native arms; it does not disable the separate parameter-lift arm.
Do not replace the combined arm with `both:gr1:oxidd`. The new versioned
`TlsfGr1BothOptionsV2` / `tlsf_gr1_both_from_target_v2` API borrows the unchanged
legacy lift options and observer; old entry points retain their defaults and ABI.
With both lifting routes disabled, it skips seed discovery, reduction, solving,
checking and candidate fitting, then uses the same trusted target and direct path.
R-off can also coexist with U-on for other API callers; that is outside this study.

Direct resource settings are independent of the R switch: the same construction
budget and deadline, 64 MiB artifact cap, solver node/cache caps of 2^22 / 2^20,
and checker node/cache caps of 2^22 / 2^20. These are existing global settings;
no threshold was introduced or tuned. Worker count/order, address-space limits,
K bounds, thread settings and every other option stay fixed across A–D.

Every worker lifecycle event, including parent spawn/terminal/winner events,
contains boolean `r_prepass` and `equivariance`. The fields are invocation switch
states; a worker may bypass the applicable hook (for example, forward or synthesis).
`route_start:equivariance` precedes recognition; it is an attempt to enter the
pre-pass, not evidence of an admitted symmetry or a solve. `route_start:backward`
records ordinary solving after a decline or with the switch off. Native R-off
must have no `seed_discovery`, `R` or `U` events, and zero seed counters. Apply the
[attribution completeness rules](attribution.md) before interpreting missing
routes or accepting route-aware recycling. A candidate remains separate from a
verified, parent-accepted answer.

## Build and freeze one measurement binary

Run from the worktree root with local dependencies already available. Resolve the
shipping preset through its group, and record its expanded configuration. The
checked implementation build is `build_scratch/p1-ablation/`; use a distinct
optimized build for measurements:

```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
P1_PY=/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3
P1_PRESET=$("$P1_PY" scripts/acacia-config.py list-group docker_default | head -n 1)
read -r -a P1_OPTIONS <<< "$("$P1_PY" scripts/acacia-config.py meson-args "$P1_PRESET")"
mkdir -p _bm-logs.p1-ablation
"$P1_PY" scripts/acacia-config.py show "$P1_PRESET" > _bm-logs.p1-ablation/config.json
meson setup build_scratch/p1-ablation/measurement --wrap-mode=nodownload \
  --buildtype=release -Doptimization=3 -Db_lto=true -Dbuild_python=false \
  -Dacacia_compiler_profile=release \
  "${P1_OPTIONS[@]}"
meson compile -C build_scratch/p1-ablation/measurement -j 4
export P1_BIN="$PWD/build_scratch/p1-ablation/measurement/src/acacia-bonsai"
export P1_PRESET P1_PY
sha256sum "$P1_BIN" > _bm-logs.p1-ablation/binary.sha256
meson introspect build_scratch/p1-ablation/measurement --buildoptions \
  > _bm-logs.p1-ablation/build-options.json
```

Record root/subproject/dependency revisions, dirty diffs, compiler and host identity,
Spot/OxiDD versions, the executable digest, and internal thread counts alongside
these files. Keep the same binary and dynamically linked libraries for all legs.
The expanded shipping configuration must compile equivariance and all five arms.

## Serial, paired 17 s runs with the existing runner

The driver supplies the flat corpus and a CPU set actually delegated to its user
manager. `8G` is 8 GiB, swap is disabled, and CPU assignment is identical in every
scope. Do not use background solver jobs or staged caps. The runner refuses an
undelegated CPU set and stops on verdict conflicts. Keep its resource/lifecycle
sidecars. Require trustworthy whole-scope peaks after normal exit, timeout and
OOM; report missing counts/reasons instead of replacing missing peaks with RSS.

```bash
export P1_CORPUS=/absolute/path/to/the/verified/flat/tlsf/corpus
export P1_CPUS=0-3  # replace with the driver's assigned, delegated CPU set
export P1_MAP="$PWD/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
```

Save the following small orchestration script as
`_bm-logs.p1-ablation/drive.py`. It calls the existing coverage runner for one
manifest entry at a time, rotates the treatment order for paired observations,
and uses the existing host sampler. It does not implement solver execution,
classification, scope management or benchmark scoring. Each runner call is waited
for before the next begins. The final resume calls complete the summaries over
the full selected list without executing any already recorded entry.

```python
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path.cwd() / "benchmarking"))
from benchlib import HostSampler

root = Path(os.environ["P1_OUT"])
root.mkdir(parents=True, exist_ok=True)
manifest = Path(os.environ["P1_LIST"])
instances = [line.strip() for line in manifest.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
assert len(instances) == len(set(instances))
full = ("real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,"
        "unreal:automaton:forward,both:gr1-real-lift:oxidd")
without_backward = full.split(",", 1)[1]
if os.environ["P1_KIND"] == "matrix":
    treatments = [("A", full, "off", "off"), ("B", full, "off", "on"),
                  ("C", full, "on", "off"), ("D", full, "on", "on")]
else:
    assert os.environ["P1_KIND"] == "backward"
    treatments = [("backward-on", full, "on", "on"),
                  ("backward-off", without_backward, "on", "on")]
common = [os.environ["P1_PY"], "benchmarking/run-syntcomp26-coverage.py",
          "--bin", os.environ["P1_BIN"], "--list", str(manifest),
          "--tlsf-map", os.environ["P1_MAP"], "--tlsf-corpus", os.environ["P1_CORPUS"],
          "--caps", "17", "--memory-max", "8G", "--memory-swap-max", "0",
          "--allowed-cpus", os.environ["P1_CPUS"], "--collect-rusage",
          "--preset", os.environ["P1_PRESET"], "--resume"]
with HostSampler(root / "host.tsv", interval=1) as sampler:
    previous = None
    for index, instance in enumerate(instances):
        shift = index % len(treatments)
        for label, arms, r, eq in treatments[shift:] + treatments[:shift]:
            output = root / f"{label}.tsv"
            sampler.set_leg(label, output)
            cmd = common + ["--solver-label", label, "--output", str(output),
                            "--phase-records-dir", str(root / "phases"),
                            "--flags", f"--arms {arms} --r-prepass {r} --equivariance {eq}",
                            "--limit", "1"]
            if previous is not None:
                cmd += ["--start-after", previous]
            with (root / "runner.log").open("a") as log:
                subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT)
        previous = instance
    for label, arms, r, eq in treatments:
        subprocess.run(common + ["--solver-label", label,
                       "--output", str(root / f"{label}.tsv"),
                       "--phase-records-dir", str(root / "phases"),
                       "--flags", f"--arms {arms} --r-prepass {r} --equivariance {eq}"],
                       check=True)
```

After the small correctness/pilot panel and G0–G5 pass, run the full matrix:

```bash
P1_KIND=matrix P1_LIST="$PWD/tests/suites/benchmarks/syntcomp26/all.list" \
  P1_OUT="$PWD/_bm-logs.p1-ablation/full17" \
  "$P1_PY" _bm-logs.p1-ablation/drive.py
```

This is four actual five-worker races per input. The flags are the only treatment
differences. Do not infer the matrix from isolated worker winners.

## Original activation set: remove the backward arm

This separate comparison measures portfolio membership. It intentionally removes
`real:small:backward`; it is not the equivariance-off leg. R and equivariance remain
on, U remains off, and the remaining worker order is unchanged. Removing a worker
also changes contention and the incumbent native construction-budget share; report
that effect as part of this membership comparison, not as an equivariance effect.

Restore `opt20260927-p4eqv` with the existing evidence fetcher (or use a previously
verified local restoration). The original activation set is all 22 census rows
with `status=attempted`, including silent orbit-sweep attempts, not `selected.list`
(the smaller profiling cohort). Preserve the census and archive mapping:

```bash
"$P1_PY" -s scripts/acacia-evidence.py fetch --campaign opt20260927-p4eqv \
  --dest _bm-logs.p1-ablation/restore/opt20260927-p4eqv
"$P1_PY" - <<'PY'
import csv
from pathlib import Path
base = Path("_bm-logs.p1-ablation")
census = (base / "restore/opt20260927-p4eqv/build_scratch/optimize-20260927/"
          "p4eqv/census.tsv")
with census.open() as handle:
    activated = {row["instance"] for row in csv.DictReader(handle, delimiter="\t")
                 if row["status"] == "attempted"}
manifest = Path("tests/suites/benchmarks/syntcomp26/all.list")
ordered = [line.strip() for line in manifest.read_text().splitlines()
           if line.strip() and not line.lstrip().startswith("#")]
assert len(activated) == 22 and activated <= set(ordered)
(base / "original-activation.list").write_text(
    "".join(name + "\n" for name in ordered if name in activated))
PY
P1_KIND=backward P1_LIST="$PWD/_bm-logs.p1-ablation/original-activation.list" \
  P1_OUT="$PWD/_bm-logs.p1-ablation/backward17" \
  "$P1_PY" _bm-logs.p1-ablation/drive.py
```

For an isolated algorithm comparison on this same set, additionally run
`--arms real:small:backward --equivariance on` versus
`--arms real:small:backward --equivariance off`, with the same runner/resources.
Those isolated observations explain the pre-pass; they do not replace either race
comparison. Higher-cap follow-ups use fresh 60 s rows unless route/budget/deadline
independence is explicitly verified within the same treatment and binary.

## Reporting and admission

Export each treatment using the existing runner's validated uniform-cap export:

```bash
for leg in A B C D; do
  "$P1_PY" benchmarking/run-syntcomp26-coverage.py export-cactus \
    --summary "_bm-logs.p1-ablation/full17/$leg-summary.tsv" \
    --runs "_bm-logs.p1-ablation/full17/$leg.tsv" \
    --list tests/suites/benchmarks/syntcomp26/all.list --cap 17 \
    --output "_bm-logs.p1-ablation/full17/$leg.csv"
done
"$P1_PY" benchmarking/cactus-report.py \
  --csv A=_bm-logs.p1-ablation/full17/A.csv --csv B=_bm-logs.p1-ablation/full17/B.csv \
  --csv C=_bm-logs.p1-ablation/full17/C.csv --csv D=_bm-logs.p1-ablation/full17/D.csv \
  --title 'Matched R/equivariance ablation, 17 s' --timeout 17 \
  --out-prefix _bm-logs.p1-ablation/full17/cactus \
  --markdown _bm-logs.p1-ablation/full17/report.md
```

Join by logical instance ID. Report REAL/UNREAL gains and losses for R at fixed eq
(C−A, D−B), eq at fixed R (B−A, D−C), and backward membership on/off. Report paired
PAR-2 with every unsolved result costing 34 s, and the interaction
`(PAR2(D)−PAR2(C))−(PAR2(B)−PAR2(A))`, both in aggregate and per input. Retain
coverage interaction sets, race-only interference, stopped attempts' cost, native
verification outcomes, and equivariance recognition/admission separately from
route selection. Winner counts alone do not establish incremental contribution.

Disclose memory completeness for every treatment, scope OOM versus worker failure,
CPU/internal thread configuration and thermal sample coverage. Repeat suspicious
near-cap differences with rotated order into fresh outputs; preserve primary rows
and annotate adjudication separately. Compare D with a matched frozen incumbent
for the no-regression gate; any wider sprint admission also needs ltlsynt 2.16 and
TACAS23, as required by handoff section 9. Archive and verify evidence with the
existing infrastructure before publishing any performance or coverage claim.
