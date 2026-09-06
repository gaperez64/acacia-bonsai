# SYNTCOMP 2025 panel construction

`all.list` preserves the repository's selection from the official 2025 TLSF
selection (`selection-ltl-2025v2/selection-ltl-2025`). The historical SyFCo
conversion produced 1,579 `.ltl`/`.part` pairs. Seven `finding_nemo`
specifications using strong next were excluded; their names and the conversion
diagnostic are preserved in `skipped.tsv`. The selection now runs from TLSF:
`tlsf-sources.tsv`, with the tab-separated header `instance\ttlsf`, maps every
logical name in `all.list` to a flat TLSF filename. Meson resolves those names
against the directory produced by `python3 benchmarking/syntcomp-corpus.py
materialize --out DIR`; the build receives that directory as
`-Dacacia_tlsf_corpus_dir=DIR` and passes each source to the wrapper with `-T`.

Materialize once: any one of `--tlsf-corpus DIR` (G2s/G3),
`ACACIA_TLSF_CORPUS=DIR`, or the build's `-Dacacia_tlsf_corpus_dir=DIR` is
enough for the benchmark gates, in that precedence order. With none set, they
use the repository's `.acacia-tlsf-corpus-path` pointer, provided its directory
still exists and carries the `.acacia-tlsf-corpus` marker. Materialize writes
both records after verification; the marker contains the entry count and
manifest SHA-256. `materialize --no-record` skips updating the pointer.
Meson still requires the build option at configure time to enumerate suite
entries; the gates reuse it without needing a second setting. G2s/G3 use the
candidate binary's build (`BUILD/src/acacia-bonsai`); G2s calibration uses the
baseline build.

`panel.list` is a deterministic 180-instance subset of the 1,579-instance
selection. Its reference data is the full, paired, serialized campaign retained under
`_bm-logs.gap-plan-20260804/syntcomp25-reference`, run from repository commit
`5c589692a84480f7267b0efcabde66afc21d5f8e`. It compared the
`best_decomp_mona` Acacia configuration with the default `ltlsynt` from Spot
2.15.1.dev at a 17-second cap, one test job at a time, with an 8 GiB memory
limit and no swap. The four slices cover all 1,579 converted instances.

The shared `benchmarking/make-panel.py` algorithm classified an instance as:

- `easy` when Acacia answered in less than 1 second;
- `border` when Acacia answered in at least 1 but less than 17 seconds;
- `gap` when Acacia did not answer before 17 seconds but `ltlsynt` did; or
- `open` when neither tool answered before 17 seconds.

An answer requires an `OK` harness result and exactly one standalone
`REALIZABLE` or `UNREALIZABLE` verdict. The selected quotas were 40 easy, 65
border, 60 gap, and 15 open. Within each stratum, the quota is apportioned by
verdict, then sampled round-robin across normalized benchmark families. Family
normalization removes generated eight-hex-digit suffixes and numeric parameter
suffixes, with explicit grouping for common arbiter, `ltl2dba`, `ltl2dpa`,
robot-grid, and collector families. Deterministic shuffling uses seed
`20260804`.

`panel.tsv` is the audit trail: it records every selected instance's stratum,
two reference times, verdict, normalized family, and source campaign. The
header comments in `panel.list` record the complete pool and selected
composition.

The current TLSF input basis is materialized and enabled with:

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out /tmp/syntcomp-tlsf
meson setup build -Dacacia_enable_tlsf_frontend=true \
  -Dacacia_tlsf_corpus_dir=/tmp/syntcomp-tlsf
```

## TLSF corpus provenance

The flat 1,586-file corpus is reconstructed from the
`tests/syntcomp-benchmarks` submodule of
[SYNTCOMP/benchmarks](https://github.com/SYNTCOMP/benchmarks), pinned at tag
`v2026`, rather than kept as a local unpacked release archive. The checkout is
sparse to `tlsf/`, shallow, and blobless: 42 MB rather than 4.9 GB. It is
registered with `update = none`, so plain `git submodule update --init` skips
it; `syntcomp-corpus.py init` is the opt-in path.

Upstream does not carry the flat competition corpus. Of the 1,586 files, 807
come from the checkout directly and 779 are reconstructed by expanding its
parametric templates. Every file is verified against the SHA-256 manifest at
`tests/suites/benchmarks/tlsf-manifest.tsv`. An independent byte comparison
against the release archive found 0 missing, 0 extra, and 0 mismatching files.

The vendored pairs formerly used by the 2025 and 2026 suites were deleted:
1,932 pooled instances, 3,864 files, and 74.6 MiB. This suite's `sources.tsv`
now retains only the 1,171 instances whose pooled files survive because
`syntcomp21` and `syntcomp24` also reference them. Those suites have no TLSF
provenance and remain on the vendored `.ltl` basis. The 2026 `sources.tsv` was
deleted entirely.

The old 2025 pairs were SyFCo conversions, whereas the old 2026 pairs used
Acacia's native frontend, so moving this suite to `-T` is a real basis change.
It was measured free on the 180-instance panel: Acacia solved 111 instances
under both bases across four arms, with zero differing instances, matching the
published figure. The campaign is archived at
`_bm-logs.syntcomp25-basis-20260828`.

`ltlsynt` is no longer fed pairs produced by Acacia's frontend. With `-T`, the
wrapper converts the TLSF with SyFCo itself and passes an explicit
`--semantics`; see
[Comparison basis: each tool converts the TLSF itself](../../../../benchmarking/LTLSYNT-GAP.md#comparison-basis-each-tool-converts-the-tlsf-itself).

SYNTCOMP accepts benchmark submissions under the Creative Commons Attribution
(CC BY) licence; see https://www.syntcomp.org/submission/.
