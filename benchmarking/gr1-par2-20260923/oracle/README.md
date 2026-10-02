# Python differential oracle

`acacia-lift-portfolio.py` and `acacia_lift/` are retained for native arm
differential tests and reproduction of historical GR(1) experiments. They are
excluded from the CLI Docker build context and are not installed or invoked
by the shipped solver. Run `acacia-bonsai` with `-T` and a native `--arms`
selection for current GR(1) solving.

From the repository root, tests can import the oracle with
`PYTHONPATH=benchmarking/gr1-par2-20260923/oracle`. The wrapper can still be
invoked directly at `benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py`
when reproducing an archived campaign.

Build the oracle's pinned frontend before running its online tests:

```sh
scripts/build-oracle-toolchain.sh
.venv/bin/python -s -m pytest -q tests/pytest/test_acacia_lift_online.py
```

The script builds the five required tlsf-tools CLIs from this checkout's
submodule into ignored `build_oracle_tlsf/`, offline and one job at a time.
`ACACIA_TLSF_TOOLS_BUILD` can select another build directory, but the oracle
rejects any build without matching source and OxiDD commits, executable hashes,
and frontend provenance. Use the script to refresh a stale build.
