# Native GR(1) and parameter lifting arms

Design against acacia-bonsai's current `src/` and tlsf-tools `generic-provenance`
`8b158d7` (also the pinned subproject). This document is an implementation plan,
not an implemented interface. The decisions at the end of `decisions.md` govern it:
Acacia is the sole entry point, tlsf-tools owns the TLSF and GR(1) algorithms,
there is no Python on the solver path, and lifting is generic and applies only to
the input's own `PARAMETERS`.

## User-facing arms and execution

Add exactly these three spellings to `--arms`:

| Arm | Permitted result | Work |
| --- | --- | --- |
| `real:gr1:oxidd` | REALIZABLE | Exact GR(1) reduction, solve, system certificate and policy check. |
| `unreal:gr1:oxidd` | UNREALIZABLE | The same exact reduction and solve, environment certificate and counterstrategy check. |
| `real:param-lift:oxidd` | REALIZABLE | Generic parameter lifting, then independent target proof. |

These preserve `polarity:transform:backend[:provider]`. `oxidd` is a new backend
token valid **only** with `gr1` or `param-lift`; it names the GR(1) game engine,
not Acacia's backward/forward safety game backend. No `:provider` field is valid
for native arms, because Spot monitor reduction is intrinsic to this backend;
reject it, including `:frozen-graph`, rather than silently ignoring it. Reject
`unreal:param-lift:oxidd`, native arms with `-f`/`-F`, and native arms with `-s`
until a checked controller conversion is implemented. Keep the existing four
arm spellings and their provider behavior unchanged. Error text and `--help`
should list the three native forms, require `-T FILE`, explain that direct GR(1)
has one arm per answer polarity and that parameter lifting is realizability
only. A native arm that cannot prove its side exits UNKNOWN; an exact solver's
opposite-side result must never be translated into that arm's verdict. Strict
reduction may be used only by the lifting arm for a REAL proof; no strict loss
may produce UNREALIZABLE.

An explicit `--arms real:param-lift:oxidd -T x.tlsf` runs one child and only
lifting. It does not silently run direct GR(1) or a legacy Acacia arm on a
decline. An explicit comma list launches precisely those children, concurrently
under the same outer deadline/cgroup. The direct real and unreal arms may do
duplicate reductions and solves if both are selected; do not share mutable
solver state across forked children. This is a measurable cost, and coalescing
must be a separate, attribution-preserving optimization if it proves worthwhile.
The initial shipped default remains the existing portfolio; the three new
arms are opt-in until the standalone and full-portfolio measurements below
justify changing `acacia_default_arms`. The proposed experimental full list is
the four B arms plus all three native arms, with no implicit fallback inside
the lifting child.

`src/portfolio_arm.hh`: add an arm kind (`legacy`, `gr1`, `param_lift`) and a
native backend representation so equality includes kind and polarity. Avoid
using a dummy `TRANSLATION_PREF_T`, `UNREAL_X_T`, or `game_backend` as native
configuration. Extend `parse_portfolio_arms` to dispatch native transforms
before the legacy transform/backend/provider checks, retaining the current
duplicate and malformed-field checks. `src/arg_parser.hh`: update help and
errors; validate the native build gate, `-T` and `-s`; skip legacy provider and
backend validation for native arms. Preserve the source bytes read for `-T` in
`arg_parse_result` as one immutable snapshot (with SHA-256) before any fork.
Today `process_tlsf_file` eagerly calls `tlsf_frontend::load` and only stores
its LTL decomposition. For native-only selection, avoid requiring that legacy
conversion; for a mixed portfolio, perform it for legacy children and give
native children the same snapshotted bytes. If legacy conversion fails in a
mixed list, mark those legacy arms unavailable and still launch the selected
native arms; a native-only list must never require legacy conversion. Malformed
TLSF still declines in the native API.
Defer `-T` conversion until after all options are parsed so `-T`/`--arms` order
does not affect this choice. Use the snapshot rather than reopening the
pathname or parsing its basename.

`src/acacia-bonsai.cc`: dispatch on arm kind in the existing forked child, call
the new C API for native arms, and map **only** a verified result of the child's
declared polarity to `EXIT_CODE_REAL`/`EXIT_CODE_UNREAL`. Decline, timeout,
unsupported shape, certificate failure, invalid artifact, and native internal
error return `EXIT_CODE_UNKNOWN` with a structured diagnostic; reserve Acacia's
`EXIT_CODE_ERROR` for command/build misuse. Keep the existing parent `wait` /
publish-first-verdict / terminate-and-reap pattern; signal-killed children must
never count as decisions. The child should pass the outer monotonic deadline
and memory limits into the library. Parent parsing, logging, and snapshotting
must be charged to wall time in benchmark runs. No Python subprocess, CLI
subprocess, temporary output file, or family dispatch is part of an arm.

## tlsf-tools C ABI

New installed header `include/tlsf/native.h`, implemented behind `src/native/`
as a C ABI with C++ internals. Names below are proposed signatures; names and
enum values should be frozen with an ABI version before Acacia integration.
All byte spans are length-delimited, so embedded NUL is rejected explicitly.
`source_new` copies its input; all other handles are immutable to callers.
`*_free(NULL)` is safe, and a caller must free each returned handle exactly
once. Exported byte pointers remain valid until their owning handle is freed.

```c
#include <stddef.h>
#include <stdint.h>
#define TLSF_NATIVE_ABI_VERSION 1
#ifdef __cplusplus
extern "C" {
#endif

typedef struct TlsfNativeSource TlsfNativeSource;
typedef struct TlsfNativeInstance TlsfNativeInstance;
typedef struct TlsfNativeGame TlsfNativeGame;
typedef struct TlsfNativeSolution TlsfNativeSolution;
typedef struct TlsfNativeProof TlsfNativeProof;
typedef struct TlsfNativeCheck TlsfNativeCheck;
typedef struct { const uint8_t *data; size_t size; } TlsfNativeBytes;
typedef struct { const char *name; int64_t value; } TlsfNativeOverride;
typedef enum { TLSF_N_OK, TLSF_N_UNSUPPORTED, TLSF_N_DECLINED,
               TLSF_N_LIMIT, TLSF_N_DEADLINE, TLSF_N_CANCELLED,
               TLSF_N_INVALID, TLSF_N_ERROR } TlsfNativeStatus;
typedef enum { TLSF_N_EXACT, TLSF_N_STRICT } TlsfNativeSemantics;
typedef enum { TLSF_N_REAL, TLSF_N_UNREAL, TLSF_N_NO_DECISION }
    TlsfNativeVerdict;
typedef enum { TLSF_N_CHECK_VERIFIED, TLSF_N_CHECK_REGION_VERIFIED,
               TLSF_N_CHECK_CERT_FAILED, TLSF_N_CHECK_REFUTED,
               TLSF_N_CHECK_UNKNOWN, TLSF_N_CHECK_INVALID }
    TlsfNativeCheckVerdict;
typedef struct { TlsfNativeStatus status; char stage[48];
                 char message[256]; } TlsfNativeError;
typedef struct {
  uint32_t abi_version;
  uint64_t deadline_mono_ns;  /* absolute CLOCK_MONOTONIC; 0 = no limit */
  int (*cancelled)(void *); void *cancel_ctx;
  size_t solver_nodes, solver_cache, checker_nodes, checker_cache;
  size_t schema_nodes, schema_cache, max_artifact_bytes;
  uint32_t max_sizes_per_axis, max_predicate_arity,
           max_subsets_per_predicate;
} TlsfNativeOptions;

TlsfNativeStatus tlsf_native_source_new(TlsfNativeBytes bytes,
    const TlsfNativeOptions *, TlsfNativeSource **, TlsfNativeError *);
TlsfNativeStatus tlsf_native_instantiate(const TlsfNativeSource *,
    const TlsfNativeOverride *, size_t count, const TlsfNativeOptions *,
    TlsfNativeInstance **, TlsfNativeError *);
TlsfNativeStatus tlsf_native_reduce(const TlsfNativeInstance *,
    TlsfNativeSemantics, const TlsfNativeOptions *, TlsfNativeGame **,
    TlsfNativeError *);
TlsfNativeStatus tlsf_native_solve_gr1(const TlsfNativeGame *,
    const TlsfNativeOptions *, TlsfNativeSolution **, TlsfNativeError *);
TlsfNativeVerdict tlsf_native_solution_verdict(const TlsfNativeSolution *);
TlsfNativeStatus tlsf_native_export_proof(const TlsfNativeGame *,
    const TlsfNativeSolution *, const TlsfNativeOptions *, TlsfNativeProof **,
    TlsfNativeError *);
TlsfNativeStatus tlsf_native_check_proof(const TlsfNativeGame *,
    const TlsfNativeProof *, const TlsfNativeOptions *, TlsfNativeCheck **,
    TlsfNativeError *);
TlsfNativeStatus tlsf_native_lift(const TlsfNativeSource *,
    const TlsfNativeOptions *, TlsfNativeGame **target,
    TlsfNativeProof **candidate, TlsfNativeError *);
TlsfNativeStatus tlsf_native_check_region(const TlsfNativeGame *,
    const TlsfNativeProof *, const TlsfNativeOptions *, TlsfNativeCheck **,
    TlsfNativeError *);
TlsfNativeCheckVerdict tlsf_native_check_verdict(const TlsfNativeCheck *);
TlsfNativeBytes tlsf_native_source_sha256(const TlsfNativeSource *);
TlsfNativeBytes tlsf_native_game_sha256(const TlsfNativeGame *);
TlsfNativeBytes tlsf_native_proof_aag(const TlsfNativeProof *);
TlsfNativeBytes tlsf_native_proof_metadata_json(const TlsfNativeProof *);
TlsfNativeBytes tlsf_native_check_evidence_json(const TlsfNativeCheck *);
void tlsf_native_source_free(TlsfNativeSource *);
void tlsf_native_instance_free(TlsfNativeInstance *);
void tlsf_native_game_free(TlsfNativeGame *);
void tlsf_native_solution_free(TlsfNativeSolution *);
void tlsf_native_proof_free(TlsfNativeProof *);
void tlsf_native_check_free(TlsfNativeCheck *);
#ifdef __cplusplus
}
#endif
```

`TlsfNativeProof` owns the certificate, optional policy, sidecar data, source
hash, game hash, reduction semantics and claimed side. Add analogous getters
for policy bytes and metadata, proof side/semantics, check method and measured
resources when implementing the header. Proof/check functions accept in-memory
objects, never caller-selected paths; optional test-only serializers can write
the existing AAG/JSON formats for differential tests. A handle references its
source/game through retained immutable ownership, or copies everything it
needs, so freeing an earlier handle cannot leave a dangling pointer. On every
non-OK return the out pointer is NULL; for `lift` that means both output
pointers. `lift` itself selects exact or, when appropriate, strict target
reduction and returns the matching target game for a separate check. No
exception, Rust panic, parser
`exit()`, or stderr-only error may cross this boundary. Preserve a structured
`status`, `stage`, and bounded diagnostic; `NO_DECISION` is distinct from a
proved loss, and `CERT_FAILED` is not a negative verdict.

`src/native/source.c` should reuse the parser, `expand()` with
`ParamOverride`, and `provenance_write` code currently wired only in
`src/main_tlsf2tlsf.c`. Move the shared parse/expand/provenance workflow into
libtlsf, without calling CLI `main` or re-parsing through `tlsf2ltl` and
`tlsfinfo`. Keep the original parameter declarations and source bytes inside
`TlsfNativeSource`; `Instance` owns the expanded instance, concrete parameter
values and frontend provenance v1. Require a non-ambiguous source-origin map
for lifting. Overrides must name existing `PARAMETERS`, validate ranges and
duplicates, and never act on file names or external templates. This also makes
direct lowering from the original unmodified input possible.

`src/native/gr1_reduction.cc` ports
`scripts/gr1_monitor_game.py` using **Spot's C++ library inside tlsf-tools**.
Use the same source-origin conjunct splitting, MP-class eligibility, complete
deterministic state-based Büchi monitors, parity-to-DBA fallback, rejecting
SCC computation, canonical role-and-declaration-order AP encoding, one-hot
monitor latches, and exact/strict AIGER objective encoding. Exact semantics
turn assumption monitors into fairness and guarantees into justice. Strict
semantics handles safety assumptions/guarantees with the release and bad-state
construction. Reject non-Mealy semantics/target as the current reducer does;
reject unsupported MP classes and any nondeterministic/incomplete monitor.
Never guess monitor-to-source provenance: ambiguous frontend/monitor matching
declines lifting. Export the current monitor provenance v3 shape (or a
versioned, losslessly equivalent typed view) so the C++ lifting port can use
source formula/node IDs, binder coordinates, declaration IDs, signal roles,
monitor roles and latch encoding. The ownership encoding fixed in `8b158d7`
must remain disjoint and injective after alpha renaming; no game input is
classified by an unescaped source-name prefix.

`src/native/gr1_service.c` should extract profile validation and the
`solve_gr1_oxidd_ex_with_certificate` call from `src/main_tlsfsolve.c` into
libtlsf. `src/gr1_oxidd.c` remains the PPS tri-nested fixpoint implementation.
Represent the exact winning or losing result, including environment
counterstrategy export, without shelling out. An OxiDD allocation/profile
failure is UNKNOWN. `src/native/gr1_check.c` should library-ise
`src/main_tlsfcertcheck.c`: its certificate check proves one-step invariant,
rank and policy obligations; its `gr1-region-v1` route proves a policy-free
winning region. It may reuse AIGER parsing and bounded BDD helpers, but must
**not** call the solver's winning-region algorithm or trust its verdict.
For direct arms, require exact semantics, a proof and policy for the solved
side, hash/side/semantics matching, and a certificate-method VERIFIED result.
For lifting, accept system-side certificate-method VERIFIED; try region
verification only when policy construction/checking hit a resource or deadline
limit, never to rescue a refuted/invalid candidate. Strict UNREAL is always
discarded. The checker must recompute and validate game/proof metadata and
return a method-specific result; neither the native arm nor Acacia treats an
unverified solver answer as final.

`src/native/param_lift.cc` and companions own the Stage C port, based on
`benchmarking/gr1-par2-20260923/oracle/acacia_lift/lifting/{source,provenance,schema,proof}.py` and global
`settings.py`. Gate on actual source `PARAMETERS` before any seed work. Lower
target once (exact, then strict only where permitted), probe each parameter
axis online by overriding the *same source snapshot*, with the current global
bounded rule: sizes 1..6 below target; adjacent stable sizes, optional third
confirmation, then first structurally valid axis. Match changed element buses
using frontend provenance, never encoded-width bits. Reject absent/ambiguous
provenance, changed declaration ABI, unstable source/monitor shape, changed
fairness or role classes, unsupported multi-index justice, and any unmatched
coordinate. Derive position roles from declaration membership and incident
source origins as in `role_signatures`, including the all-except-self degree
normalization. No capability registry, family names, formula-name switches,
stored templates or parameter-specific choices are allowed.

Use **OxiDD** for schema reconstruction. It is already the native GR(1)
engine and has capped managers, existential quantification and substitution;
port the Python BuDDy projection/relabel/equality algorithm over its C API.
This avoids a second BDD manager family and any risk of `bdd_init` resetting
Spot's `libbddx` global state. The Python BuDDy route remains a differential
oracle, not a runtime dependency. Reconstruct every seed invariant and X-rank
predicate exactly from normalized projections; compare schemas across seeds,
then instantiate at target coordinates. Search arity online from 0 through 4,
with at most 2,000 subsets per predicate, bounded by the shared deadline and
node caps. Reject any missing target variable or role template. Import target
justice predicates directly from the target game. Derive move relations from
the **actual target transitions** plus lifted invariant/ranks, as the owner
decided; learned move schemas are optional research, disabled initially.
Export the same target AIG certificate and sidecar semantics. Skolemize a
policy under a bounded portion of the remaining time, check it independently,
and use the checker region method only for capacity/deadline fallback. The
lift function produces a candidate, never an accepted verdict; Acacia must
call the checker and check side, method, hashes and source binding.

Use one absolute `CLOCK_MONOTONIC` deadline for reduction, seed discovery,
solving, reconstruction, exports and checks. Check it and `cancelled` in every
potentially long loop (monitor transitions, seed enumeration, fixpoint
iterations, projection subsets, AIG expansion, checker quantification).
OxiDD node/cache limits are required, not optional for these arms; cap schema
nodes and AIG bytes separately. Return LIMIT/DEADLINE on exhaustion, with no
partial accepted artifact. The outer Acacia child stays independently killable
by its parent and the benchmark cgroup. Rust/OxiDD's static FFI may have
internal allocations beyond node counts, so node caps are not an RSS guarantee:
also set a conservative child address-space limit or enforce the invocation's
8 GiB cgroup, and measure the entire process tree. All native work remains in
the forked child; the parent never holds a live Spot or OxiDD manager before
forking. Stop all children and reap them on a winner or signal as today.

## Build and rollout

The current `meson.build` asks for `libspot` and Spot's `libbddx` and adds
`dependency('tlsf', fallback: ['tlsf-tools','tlsf_dep'])` with
`oxidd=disabled` when the TLSF frontend is on. In tlsf-tools `meson.build`,
`tlsf_dep` is a static libtlsf dependency, while the OxiDD solver/checker
sources and Rust static archive are currently linked only into CLI targets.
Add a tlsf-tools Meson feature `native_gr1` that requires OxiDD and Spot and
builds the new C/C++ objects into **one** static libtlsf (or one dedicated
static library linked once by `tlsf_dep`). Acacia's native-arm build option
should imply the TLSF frontend and request `native_gr1=enabled,oxidd=enabled`
through the subproject; fail configuration clearly if the pinned Rust archive
and headers are missing. Keep the legacy frontend-only build available when
native arms are disabled. Ensure the same Spot `libspot` and `libbddx` dependency
instances reach the final executable once; do not vendor or statically link a
second BuDDy copy. OxiDD is a distinct Rust static archive, linked once with
its required pthread, math and platform `dl` libraries. Check `pkg-config`
transitive link flags for installed libtlsf as well as the Meson subproject.
CI must build both feature modes, initialize/build the OxiDD submodule with a
locked Rust toolchain, exercise the C ABI under C and C++ linkers, and run
the native differential and checker-independence suites. No Python runtime or
Spot Python binding is required for the production binary; Python is used only
by the comparison tests. Record Spot, OxiDD, compiler and tlsf-tools commit
versions with benchmark binaries.

Reviewable steps (estimates are engineer working days, including tests and
review, not CPU campaign time):

| Step | Files and acceptance test | Estimate |
| --- | --- | ---: |
| 1. C boundary | Add `include/tlsf/native.h`, `src/native/source.c`, `src/native/gr1_service.c`, `src/native/gr1_check.c`; refactor the three CLIs to call shared library functions. Compare parse/override/provenance bytes, solve side, AAG/JSON exports and checker method results with `tlsf2tlsf`, `tlsfsolve`, `tlsfcertcheck` on positive, negative, malformed and OOM cases. | 5–8 d |
| 2. Native reduction | Add `src/native/gr1_reduction.cc` and monitor provenance support. Differential-test exact/strict AAG semantics and provenance against `gr1_monitor_game.py`, plus independent Spot language equivalence on small cases. Cover deterministic fallback, non-Mealy, unsupported MP, safety release and alpha-renamed/adversarial `controllable_*` signals. A different gate order is acceptable; a different game language or ownership is not. | 7–12 d |
| 3. Acacia direct arms | Update `src/{portfolio_arm.hh,arg_parser.hh,acacia-bonsai.cc,meson.build}` and root `meson.build`/options. Prove one-arm polarity, `-T` gating, duplicate/provider errors, UNKNOWN on failed verification, process kill/reap, and exact side/hash binding. Compare direct arm verdicts with `benchmarking/gr1-par2-20260923/oracle/acacia_lift/direct.py` and the three CLIs on a frozen sample; run each direct arm alone. | 3–5 d |
| 4. Native Stage C | Add `src/native/{param_lift,role_schema,proof}.cc` (or equivalent) with capped OxiDD schema operations. Differential-test seed choices, roles, arities, instantiated predicates, policy/region method and final verdicts against `benchmarking/gr1-par2-20260923/oracle/acacia_lift/lifting/*` on generated parametric instances and development corpus; compare functions, not only serialized AAGs. Include no-`PARAMETERS`, unsupported bus widths, unstable ranks, large arity, invalid certificate and capacity/deadline cases. | 12–20 d |
| 5. Acacia lifting arm | Wire `real:param-lift:oxidd` into the same fork model, with no direct fallback. Test standalone selection, fail-closed checker results and no verdict change under randomized basenames and alpha-renamed signals. Test the exact adversarial prefix and enum-bus declarations. | 3–5 d |
| 6. Ship-path cleanup | Remove Python wrapper/lifting invocations from production launch/build docs and shipped packaging; keep the Python route and CLI differential tests as reference oracles in research/test locations. Re-run C/C++ CI and reproducible benchmark-build checks. | 2–3 d |

The critical path is about 32–53 engineer days, plus review and full campaign
runtime. Each step lands with its oracle comparison before the next step uses
it. Never infer correctness from matching corpus verdicts alone: test bad
certificates, negative cases, renaming, source mutation and resource failures.

## Measurement and admission

Use the same corpus definitions and reporting as the current campaign, but
freeze a new binary/commit manifest and mark all results as a new series.
Run serial instances under one 8 GiB/no-swap scope, fixed host regime, outer
wall caps of **60 s and 17 s**, and record entire-cgroup peak RSS, CPU time,
OOM events and termination reason. At each cap and on both original and
obfuscated (random basename plus alpha-renamed signals) corpora, run seven
standalone legs using `-T FILE --arms SPEC`: B's four exact spellings
`real:small:backward`, `real:small:forward`,
`unreal:formula:spot-guarded-sparse`, `unreal:automaton:forward`, then the
three new spellings above. Run B's four-arm portfolio as a fresh reference and
the seven-arm full portfolio as separate legs. Do not synthesize full-portfolio
rows from per-arm minima: concurrent CPU/RSS contention and duplicated direct
work can change both coverage and times. Preserve historical B 60 s censored
rows only as context, clearly labelled derived rather than a fresh control.

For every instance store cap, verdict, elapsed wall/CPU, cgroup peak/current
memory and OOM count, arm exit/signal, winner, source/game/proof hashes,
reduction semantics, proof method, checker verdict, native stage timings,
node peaks and decline reason. Compare standalone coverage, solved counts,
PAR-2, time-to-verdict distributions, disagreement pairs and per-arm unique
contributions; report false/unknown outcomes separately. Compare original to
obfuscated cases by stable source-pair ID and require identical eligibility,
seed choices modulo renaming, proof side and verdict. Include generated unseen
parameter sizes and adversarial identifiers so this checks generic behavior,
not just known corpus names. Inspect any discrepancy before admitting a result.

The key contention comparison is B alone versus B plus each native arm and
the full seven-arm portfolio, especially B-solved cases near 17 s and cases
near the 8 GiB limit. Attribute B regressions to CPU competition, OxiDD/Rust
memory, Spot reduction, duplicate direct workers, checker cost or OOM using
per-child CPU/RSS alongside cgroup totals. Native arms get the same full
deadline as B; there is no sequential lifting slice or family-specific
budget. Change defaults only after these measured, verified full-portfolio
legs improve the declared 60 s and 17 s objectives without unacceptable
contention losses, and record the exact chosen default list.
