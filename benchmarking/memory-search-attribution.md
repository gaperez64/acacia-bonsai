# Attributing games after compact action construction

`memory-search-panel.py` prepares a panel without running a solver, runs its jobs
serially when asked, and writes a per-job table and recommendation template. Raw
outputs belong in `_bm-logs.i251/`. The frozen closing candidate's executable digest
comes from `candidate-freeze.json`; inputs are bound to their current source bytes.
Fresh 60 s records are joined through the measured row's `source_run` and verified
source digest, never through the derived or validation series.

Prepare with explicit restored locations:

```sh
PYTHON=/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3
$PYTHON benchmarking/memory-search-panel.py prepare \
  --campaign <closing>/build_scratch/cov/wt-pm-p0/_bm-logs.pm-final \
  --legacy <issue-sweep>/_bm-logs.legacy \
  --corpus <TLSF-corpus> --binary <restored-PMC4>/src/acacia-bonsai \
  --output _bm-logs.i251/panel
$PYTHON benchmarking/memory-search-panel.py run \
  --panel _bm-logs.i251/panel/panel.json --output _bm-logs.i251/run-plan
```

The final command prints commands and writes a plan. Add `--execute` to launch the
60 s panel. For the authorized single-input smoke, use `--one --cap 10 --execute`
and a separate output directory. Each invocation uses the existing scope runner,
8 GiB, zero swap, no solver-visible deadline, phase records, invocation-bound Spot
captures and their milestone history. The binary's default portfolio and all K,
row and memo limits are preserved. The campaign scope guard rejects overlapping
solver campaigns. Scratch for the scope owner goes in `build_scratch/i251/`.

Before every invocation (including the first launch and resumed retries), the driver
uses the closing campaign's package sensor `/sys/class/hwmon/hwmon6/temp1_input` and
waits until it reads at most 70 C. It polls every second for at most 180 seconds;
unreadable/invalid temperatures or an expired wait stop before solver launch. This
policy is recorded in `run-plan.json`; timestamped readings and readiness/failure
states go to `cooldown.jsonl`. Cooldown is outside the solver's cap.

Resume an executed panel by repeating the same command with `--resume --execute`.
The saved plan must match the panel digest, executable path/digest, treatment, cap,
flags, selected job count, observation settings and cooldown policy. Current source
bytes and every saved attempt's provenance are checked before any launch. Matching
completed jobs (including TIMEOUT/MEMOUT observations) are skipped. Interrupted
attempts retain their files in `job-NN`; retries use `job-NN-attempt-NN` and distinct
invocation IDs. Each attempt gets an `attempt.json` before launch; only a finished
scope with memory-cgroup evidence publishes `run.json`. Analysis includes completed
attempts and leaves interrupted raw evidence separate. Existing plans are preserved;
a changed treatment or a plan created before this policy needs a fresh output directory.

Selection requires a legacy construction/search stage still unfinished at a fresh
17 or 60 s cap, or a paired nonsolve-to-solve gain in an i208 screen. Native declines
and native preparation alone do not qualify a legacy construction transition.
Historical MEMOUT and paired gains come first. Up to three slots cover fresh
construction/search evidence outside the earlier screens; other slots cycle through
historical transition, unfinished-stage and source-size strata. Source byte count
and digest break ties. Names identify manifest rows and never select a solver route.
The capacities are evaluation choices, not solver thresholds. `panel.json` retains
both closing observations, old-screen evidence, source hashes and selection flags;
`classification.tsv` covers every fresh 17/60 s row.

The shared P0 mapping is `benchlib.phase_plan_stage`; `PhaseRecords.worker_events`
joins native summaries by emitting PID and explicit native arm without changing raw
wire objects. `legacy-phase-table.py` adds native lifecycle intervals and `plan_stage`.
`classify-coverage.py` exposes `last_plan_stage`. Refresh an old P0 table with:

```sh
$PYTHON benchmarking/phase_attribution.py --rows <campaign>/17/candidate-17s.tsv \
  --phase-root <campaign>/17/phase-records --output _bm-logs.i251/p0-reclassification.tsv
```

Native preparation maps to IO/action construction, direct/seed solver work to
K-search, and explicit target checks to independent verification. Original-game
obligation work maps to K-search; its completed outcome includes preparation and
its same-library check, whose separate timing remains unknown. A native route can
contain nested work and has no K identity unless one was actually recorded.
`summary-only` durations have no reconstructible endpoints and can include zero
counters for unentered work; they do not establish a completed search or checker.
Only explicit completion events establish completion. Missing endpoints stay
unknown. Only the last entered native interval whose stage and route match the
signalled terminal record, or an explicitly cancelled obligation interval, gets a
censored lower bound. Enclosing or earlier missing completions are never filled in.
Writer failures, live drop counters and sequence gaps preserve partial delivery.

P0 invocation attribution treats explicit MEMOUT separately. On nonsolves, only
surviving/signalled workers vote; earlier completed declines do not. Their latest
stage evidence maps to translation, construction, search or verification; a newer
terminal stage takes precedence when its entry packet is missing. Different
known categories yield `mixed`; absent worker/stage evidence yields `unknown`.
Observed terminal route labels can identify an obstruction without supplying an
entry time or duration. This rule makes no claim about the longest critical path.

Frozen PMC4 has `acacia_enable_diagnostics=false`. Its existing sparse counter
`first_useful_rank_query_ms` measures the first successful oracle query that uses a
rank, from row-store factory creation; boundary shortcuts and failed queries do not
count. Its phase metrics and captures are retained, including incomplete searches.
The frozen executable cannot emit the new dense marker. Missing dense query/growth
observations remain unknown in that treatment.

The additional dense marker is behind `ACACIA_PHASE_RECORDS`, independent of the
compile-time diagnostics option. A useful dense query is one completed standard
rank/action application during search. `first_useful_action_query_ns` records its
absolute monotonic time. `query_snapshot_ns`, `completed_action_applications`,
`query_rank_support` and `query_rank_maximum` sample the source rank after the
application, at the first four applications and then powers of two (at most 65
samples per stage). A stage change resets the count. Observation off performs no
rank scan, counter update, clock or resource sample. These are rank samples, not
unique-rank counts or total retained memory. Timestamp metrics are excluded from
cap-independence equality; deterministic sampled counts and rank values remain
compared.

Use `--observation-binary <binary>` for a separately built supplemental treatment
containing the marker and, for existing dense demand checkpoints,
`acacia_enable_diagnostics=true`. It must support native TLSF and use the intended
portfolio; this worktree predates the obligation-arm source, so its generic
correctness builds are not replacements for frozen PMC4. The driver must build the
observation treatment from the intended shipping source/configuration. The script
records its separate executable digest and label and never substitutes it for the
frozen baseline. No new solver option or limit was introduced here.

External `memory.current` and `memory.peak` samples poll every 20 ms in the workload
cgroup. Verification entry is joined to samples immediately before and after it,
with a global maximum bracket of 100 ms. Values are bracketed observations, not
exact instantaneous boundary memory or process RSS. Missing brackets and unentered
checkers remain unknown. Final scope peak/events come from the existing retained
cgroup observer. Byte-estimate counters retain their names and are not RSS.

`analyze --output <run-directory>` rebuilds `per-job.tsv`, `analysis.json`, each
job's phase table and `REPORT.md`. K sequences contain one entry per `k-attempt`
occurrence, retaining repeated K values for distinct attempts; an unmatched nested
`search` supplies a fallback when the enclosing attempt was not recorded. Nested
search/check stages remain separate. Worker identities, query definitions/origins,
completed search/check stages, raw rank/support metrics, existing diagnostic snapshots
and memory brackets remain separate. Partial capture history tails are discarded
without repairing them or inferring completion. The recommendation section is a blank
evidence-based template; it changes no K, row or memo setting.
