# TLSF signal-renaming differential check

The corrected obfuscator processed all 1,524 SYNTCOMP26 TLSF inputs with seed
20260924 plus the one-based corpus index. It parses scalar declarations,
indexed buses, and `TYPE name` enum buses. For every file, the sidecar keys
exactly matched all declared INPUTS/OUTPUTS signal names, and the renamed
file's declarations exactly matched the sidecar values. Parameters and their
uses remain unchanged.

For each original/renamed pair, `tlsfinfo --expanded-ins/--expanded-outs`
established the expanded AP mapping; `tlsf2ltl --format ltl` lowered both
files; and the normalized formulas were compared after applying that mapping.
Rerun result: **1,524 equal, 0 mismatches, 0 tool errors**. The per-input
results are in `obfuscation-invariance.tsv`; generated files and sidecars are
under `build_scratch/stageA/obfuscation-corpus/`.

Command: `python benchmarking/gr1-par2-20260923/verify-obfuscation.py --per-tool-timeout 12`
