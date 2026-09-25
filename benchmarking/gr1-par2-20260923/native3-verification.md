# Native GR(1) arm verification (updated 2026-09-25)

## Final patched OxiDD build

tlsf-tools is at `33555ba`. Its `scripts/build_oxidd.sh` applied
`patches/oxidd-local-store-generation.patch` and rebuilt the OxiDD archive
with one Cargo job. `scripts/build_oxidd.sh --verify` passed. The fresh
`build_native3final/` build used `-Dacacia_native_arms=true` and serial
compilation. Its generated `subprojects/tlsf-tools/build_config.h` records
`TLSF_BUILD_OXIDD=1` and a `TLSF_BUILD_OXIDD_PATCH` value containing
`local-store-generation`, patch SHA-256
`993c39f07f9cd6078874764d48060f6796d7e2dd7201f19f9e69042c0c616a28`,
and archive SHA-256
`9c86d4e7de3a8cab3c3f4bc573c6e684f265cf643e464352496746e179096cbe`.
Acacia now calls `tlsf_gr1_check` on the arm's solver thread.

- `native-gr1-same-thread`: 200/200 REALIZABLE round-robin runs and 100/100
  UNREALIZABLE small-input runs, with no child signal reports or unexpected
  exit status. The test uses a checked-in copy if the corpus submodule is absent.
- Native-on unit suite: 53/53 passed, including `portfolio-deadline`.
- Native-off unit suite: 51/51 passed, including `portfolio-deadline`.
- tlsf-tools `oxidd_manager_lifetime_manager` and
  `oxidd_manager_lifetime_checker`: 2/2 passed.
- `scripts/acacia-config.py validate` and `tests/check-config-frontends.py`:
  passed; the latter reports the documented TLSF-frontend default divergence.

Both unit suites ran with `-j 1` and a build-tree `TMPDIR`.

## Earlier step-3 checks on tlsf-tools `17ffec9`

The Acacia build used `-Dacacia_enable_tlsf_frontend=true -Dacacia_native_arms=true`
with tlsf-tools pinned to `17ffec9`. The option-off build used the same TLSF
frontend without native arms. Both builds used `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`
and serial compilation.

## Checks

- `python3 scripts/acacia-config.py validate`: passed.
- `python3 tests/check-config-frontends.py`: passed (the pre-existing, documented
  `acacia_enable_tlsf_frontend` default divergence remains).
- `meson test -C build_native3_on --suite unit --no-rebuild -j 1`: 51/51 passed.
- `meson test -C build_native3_off --suite unit --no-rebuild -j 1`: 50/50 passed.
- `meson test -C build_native3_on --no-rebuild native_api_c native_api_cpp
  native_api_failure native_reduction_c`: 4/4 passed. The native API tests
  exercise a rejected certificate independently of the solver.
- Native CLI checks cover accepted and opposite polarities, parse errors,
  native-only malformed input, mixed legacy/native selection, deadline expiry,
  child reaping, renamed inputs, concurrent parseable JSON diagnostics, and a
  debug-only corrupted-proof check.
- The final Acacia link command contains one `libtlsf.a`, one OxiDD archive,
  one `libspot.so`, and one `libbddx.so`. The generated installed-library
  `tlsf.pc` reports `-loxidd_ffi_c -lpthread -lm -lboost_json -lstdc++ -ldl`
  plus `libspot`/`libbddx` through `Requires.private`.
- A separate `-Dacacia_native_arms=true` build (without setting the TLSF
  frontend option) compiled and returned REALIZABLE on the tiny real input;
  the native option correctly implies the frontend. The unmodified default
  option set also compiled without tlsf-tools.

## Differential check

Run `check-native3-differential.py` with the Acacia binary and a fresh
tlsf-tools CLI build from `17ffec9`:

```sh
python3 benchmarking/gr1-par2-20260923/check-native3-differential.py \
  --binary build_native3_on/src/acacia-bonsai \
  --tlsf-build build_native3_oracle \
  --output build_native3_on/differential_final --count 50 --seconds 10
```

It selects the smallest available TLSF input from each family with a file
under 20 KiB, then fills the remainder by file size, for 50 inputs under
20 KiB. Results are in `build_native3_on/differential_final/results.tsv`.
Each native and Python oracle invocation has 10 seconds.
The Python route is `scripts/acacia_lift/direct.py` and is test-only.

| Native result | Python oracle result | Inputs |
| --- | --- | ---: |
| REALIZABLE | REALIZABLE | 21 |
| UNREALIZABLE | UNREALIZABLE | 5 |
| UNKNOWN | Declined | 24 |

Final disagreements: **0/50**.

The first differential run had one transient UNKNOWN on
`round_robin_arbiter.tlsf` where Python verified REALIZABLE. Repetition found an
intermittent `SIGSEGV` in OxiDD BDD apply when the checker reused the solver's
thread. Checking the same exported artifacts in a fresh checker process
succeeded repeatedly. Acacia then invoked the C checker on a fresh thread in
the same arm child, after copying the artifacts. In 100 repeated single-arm
and 100 repeated two-arm runs of that input, there were no failures; the final
50-case differential run also had no disagreement. This is an observed
workaround for a dependency interaction. The final build above removes this
workaround and uses the patched OxiDD archive.
