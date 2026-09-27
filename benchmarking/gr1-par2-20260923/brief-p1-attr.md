# Brief P1-attr — checker attribution harness (L0 vs P1 tlsfcertcheck on frozen target artifacts)

Read plan.md §4 "P1 reporting and admission", §6.4, §11.2. Another codex task is concurrently
editing generalize_gr1.py / param-lift-campaign.py / s0 files — do NOT touch those; only create
files under benchmarking/gr1-par2-20260923/p1-attribution/. No compiling, no solver or checker runs
by you except a <5 s smoke on one tiny control (arbiter n=6) without systemd-run. I run the harness.
Write p1-attribution/run.py (Python 3, stdlib only, ruff clean) that:
- discovers target check inputs (game.aag, policy.aag, certificate.aag for the TARGET n — not the
  seed files) under the frozen, read-only roots given as arguments:
  _bm-logs.gr1-par2-p0-replay-L0/ and _bm-logs.gr1-par2-s0-run1/runs/ (inspect them; the target
  is the file triple whose n equals the run's target; UNREAL rows use an environment certificate —
  find out from the generalizer code how the target check is invoked for REAL and for UNREAL, i.e.
  the exact tlsfcertcheck argv incl. --method and any options, and reproduce it exactly);
- dedupes identical triples by sha256 (replay and S0 runs overlap), keeping provenance of both;
- for each target and each checker binary (--checker NAME=PATH, repeatable; I will pass
  L0=subprojects/tlsf-tools/build-L0/tlsfcertcheck and
  P1=/home/gperez/GIT-repos/tlsf-tools/build-P1-0435385/tlsfcertcheck), runs the check cold inside
  `systemd-run --user --scope --quiet --collect -p MemoryMax=8G -p MemorySwapMax=0 --unit=<unique>`
  with a --timeout (default 300 s, enforced with `timeout -s KILL`), reading the scope's
  memory.peak from /sys/fs/cgroup/user.slice/.../<unit>.scope/memory.peak BEFORE the scope is
  collected (use a small wrapper shell inside the scope that execs the checker and then cats its own
  cgroup's memory.peak to a file after the child exits: `sh -c 'cmd; rc=$?; cat
  /sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup)/memory.peak > peakfile; exit $rc'`);
- order: rounds (--rounds, default 2); within a round, targets in fixed order, and per target the
  checker order alternates ABBA across rounds; strictly serial;
- records per run: target, side, n, checker name, binary sha256, round, argv, exit code, status
  word from stdout, stdout sha256, wall seconds (monotonic), memory.peak bytes, and for checkers
  supporting --stats a second, separate run is NOT made — instead pass --stats only if the brief
  argument --stats-for NAME is given (it changes output; keep timed runs identical in flags to L0
  except that). Default: no --stats.
- writes an append-only TSV (refuses to overwrite) and a JSON summary: per target, per checker:
  statuses (must agree wherever both decide — flag any disagreement loudly), median wall, max
  memory.peak, ratio P1/L0.
- --dry-run prints the plan (targets, argv) without running.
Also p1-attribution/README.md with the exact command I should run, and the list of targets found.
Finish with a VERDICT including the discovered target list with sides and the checker argv.
