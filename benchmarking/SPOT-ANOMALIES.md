# Spot `ltlsynt` anomaly reproducers

These commands revalidate two upstream-facing observations from the August 2026 Acacia–ltlsynt
gap campaign. They were run with `ltlsynt (spot) 2.15.1.dev`; the local executable SHA-256 was
`ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900`.

This file is a prepared report, not evidence that an external issue or email has been sent.

## `LedMatrix`: verdict changes with two simplifications disabled

From the repository root, use the generated wrapper to read the paired `.part` file:

```sh
/bin/zsh -f build_best_decomp_rank_bucketed_mona_eq_min_blocks_2/tests/check-real-correct.sh \
  -l -F tests/ltl/syntcomp/1e1f32e7199375f1c447d0de1697eb06c2aa283484f57eda258a6001d683ec56.ltl

env LTLSYNT_OPTS='--polarity=no --global-equivalence=no' \
  /bin/zsh -f build_best_decomp_rank_bucketed_mona_eq_min_blocks_2/tests/check-real-correct.sh \
  -l -F tests/ltl/syntcomp/1e1f32e7199375f1c447d0de1697eb06c2aa283484f57eda258a6001d683ec56.ltl
```

The default command prints `UNREALIZABLE`; the second prints `REALIZABLE`. Both exit normally.
The wrapper prints the fully expanded `ltlsynt --ins=... --outs=...` command before executing it.

## Acceptance-set limit with the same flags

The plan described six status-2 rows. Revalidation of the distinct SYNTCOMP25 formula/partition
pairs found five immediate status-2 failures and corrected the physical-instance count; the
25- and 30-client cases time out at 20 s instead of reaching the exception. Run the five confirmed
cases with:

```sh
for hash in \
  5db36152043903730177132b14643e8d741b183ad986cbf6daecd9d5beee3a4e \
  b8fa7251f78a5dc2e772fabf030ab7c50a002ba6b250f6c195beb2821d182a7e \
  8472d4d906b7796456982d3897685ebd781709fa112fcefe2fba951d7bd39508 \
  a9ed24bdc9509e187f62937bf4cd9ec57600e144255d2dc814063bc3bf4c7e71 \
  d42d28bf71601c2e9c5ea85a673bf97dc4d746385f9ace4a995b0acb4b5bb77b
do
  env LTLSYNT_OPTS='--polarity=no --global-equivalence=no' \
    /bin/zsh -f build_best_decomp_rank_bucketed_mona_eq_min_blocks_2/tests/check-real-correct.sh \
    -l -F "tests/ltl/syntcomp/$hash.ltl"
done
```

The hashes correspond to `prioritized_arbiter_unreal2` with 100 and 60 clients and
`simple_arbiter_unreal2` with 50, 60, and 75 clients. In each case `ltlsynt` prints
`Too many acceptance sets used. The limit is 64.` and exits 2; the validation wrapper maps that
non-verdict to exit 3. The independently converted SYNTCOMP26 pairs reproduce the same five
family/size failures.
