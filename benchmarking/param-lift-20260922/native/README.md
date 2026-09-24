# BuDDy simultaneous-compose adapter

Build the optional adapter explicitly, before a benchmark run:

```sh
/usr/bin/python3.13 benchmarking/param-lift-20260922/native/build.py
```

The serial build performs exactly one compiler invocation and writes
`build_scratch/buddy-adapter/libbuddy_veccompose_adapter.so` plus its JSON
sidecar.  The sidecar binds the binary to the adapter source, compiler and
version, flags, resolved `libbddx`, installed BuDDy header (`bddx.h`, this
installation's `bdd.h` equivalent), and binding interpreter.

Select it explicitly with either:

```sh
--buddy-adapter build_scratch/buddy-adapter/libbuddy_veccompose_adapter.so
```

or `ACACIA_BUDDY_ADAPTER`.  With neither setting, the generalizer uses its
retained two-pass simultaneous-substitution route.  Runtime code only loads
and validates a configured adapter; it never invokes a compiler.  Run
`generalize_gr1.py --probe` (with the normal tool configuration arguments) to
validate the sidecar, dependency resolution, mapped `libbddx` object, and
shared symbol address before timing.
