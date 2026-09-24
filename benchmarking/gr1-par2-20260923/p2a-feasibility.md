# P2a feasibility — native simultaneous BuDDy composition

## Decision

Use route **(b), a small native C++ adapter against the already loaded
`libbddx.so.0`**.  Route (a) is not usable: `buddy.py` exports
`bdd_veccompose` and `bdd_setbddpair`, but SWIG generated its constructible
`bdd_pair` proxy for `std::pair<bdd,bdd> *`; both native functions require
`bddPair *` and reject that proxy with `TypeError`.  No OxiDD kernel is
needed.

## Binding and library identity

The configured interpreter is `/usr/bin/python3.13`, with `buddy.py` and
`_buddy.cpython-313-x86_64-linux-gnu.so` loaded from
`/usr/local/lib64/python3.13/site-packages`.  `ldd` resolves `_buddy`'s
`libbddx.so.0` dependency to `/usr/local/lib/libbddx.so.0.0.0`, matching the
P0 dependency manifest.  The library exports `bdd_newpair`,
`bdd_setbddpair`, `bdd_veccompose`, `bdd_freepair`, and the reference-count
entry points.

The feasibility probe linked its adapter to that SONAME, imported `buddy`
before loading the adapter, and then checked both `/proc/self/maps` and the
runtime address of `bdd_versionnum`.  Exactly one mapped `libbddx` remained
(`/usr/local/lib/libbddx.so.0.0.0`), and the address obtained through
`_buddy` was identical to the address used by the adapter.  The production
loader must repeat these checks and refuse to operate if either identity test
fails; it must never open another BuDDy implementation by an arbitrary path.

## Ownership and failure boundary

Each Python `buddy.bdd` proxy owns a C++ `bdd` object and exposes its pointer
through the SWIG `this` handle.  The adapter can accept those `bdd *` values,
build a temporary `bddPair`, call the C++ `bdd_veccompose` overload, and
assign the result into a freshly constructed Python `buddy.bdd` proxy.  That
assignment uses the binding's normal C++ copy/move and destructor behavior,
so the returned proxy owns the result with the same `bdd_addref_nc` /
`bdd_delref_nc` discipline as every other binding result.  Replacements stay
live for the duration of the native call, and the pair is freed on every
return path.  The production adapter installs a scoped `bdd_error_hook`,
records checked-API errors without invoking BuDDy's aborting default handler,
keeps pair cleanup inside that scope, and restores the exact prior handler on
every exit.  This distinguishes a legitimate `bddfalse` result from BuDDy's
error sentinel.  Python and native code independently reject variables
outside `bdd_varnum`; negative pair-operation status, hook errors, and caught
native failures are reported to Python as exceptions.

The scratch probe successfully performed a two-variable swap and a
non-injective substitution and showed identical proxy results to the
expected BuDDy functions.  The implementation will also expose a test-only
garbage-collection call so repeated substitutions can be checked across
`bdd_gbc`, which the installed Python wrapper does not itself expose.

The adapter is built only by `native/build.py`, in one compiler invocation.
Its sidecar binds the shared object to source, compiler/version/flags,
resolved `libbddx`, installed header, and binding interpreter.  Runtime code
only validates and loads a path configured by `--buddy-adapter` or
`ACACIA_BUDDY_ADAPTER`; otherwise it selects the retained two-pass route.
