# At a glimpse: suggested process

In the coming sections of this README, all of the following commands are
explained and justified. We sum up the process we suggest
here for convenience.
```
  $ ./self-benchmark.sh -b ab/syntcomp21/crit -t 1
```
Wait for completion of benchmarking of multiple versions of Acacia-Bonsai.
This can take a few hours!

```
  $ mkdir mkplottable

  $ for f in _bm-logs/*.json; do \
      meson-to-mkplot.sh $(basename $f .json) $f > mkplottable/$(basename $f); \
    done

  $ mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
```
Now `plot.pdf` contains a plot of the benchmarking of the
different configurations of Acacia-Bonsai.

## Dependency
Above, we are using `mkplot.py` (https://github.com/alexeyignatiev/mkplot), a
tool to produce cactus plots.

  
# Generating the plots

Once a few JSON files have been produced in _bm-logs/, one can convert the files
to a format that mkplot understands.  To convert one JSON from the test output
to the mkplot format, one can use:
```
  $ meson-to-mkplot.sh 'Title of Plot' testlog.json > mkplottable.json
```

Survival, a.k.a. cactus, plots are then generated using, for instance:
```
  $ mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
```

# Ranking configurations by PAR-2

`rank_bm_logs.py` prints a table of all configurations in `_bm-logs/`
sorted by PAR-2 score (each OK charged its wall-clock, each TIMEOUT
charged `2 × timeout`). Useful for picking the strongest configurations
after adding or re-running benchmarks:
```
  $ benchmarking/rank_bm_logs.py              # reads ../_bm-logs by default
  $ benchmarking/rank_bm_logs.py path/to/logs # or a custom directory
```
The per-instance timeout cap defaults to the largest `duration` observed
across the TIMEOUT entries in the logs; override it with
`--timeout SECONDS` if you want a specific value.
