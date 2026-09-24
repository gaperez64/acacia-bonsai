# Maintained GR(1) lifting route

The Acacia-owned lifting code lives here. The dated commands in
`benchmarking/param-lift-20260922/` forward to these modules so old campaigns
remain reproducible.

The portfolio wrapper defaults to `python -m acacia_lift.runner`. Run it through
`scripts/acacia-lift-portfolio.py` so the lifting attempt and unchanged Acacia
fallback share one outer deadline and process tree. To call the route directly
from the repository root, put `scripts` on `PYTHONPATH`:

```sh
PYTHONPATH=scripts python3 -m acacia_lift.runner --request-mode source \
    -T requested.tlsf --budget 120 --tlsf-tools-build BUILD_DIR
```

`data/capabilities-v1.json` is the validated source capability registry. Every
template is bound by SHA-256. Schema decisions come from its stable regime,
role class, invariant arity, and move arity fields. The dated `m4-*.tsv` files
remain experiment evidence and are never runtime inputs. 
`generalizer.py --results-out PATH` writes the current invocation's result
table. It never reads an existing results table. The historical
`GENERALIZE_GR1_RESULTS` environment variable remains an output-only alias.
