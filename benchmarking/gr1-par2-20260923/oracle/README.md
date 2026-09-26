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
