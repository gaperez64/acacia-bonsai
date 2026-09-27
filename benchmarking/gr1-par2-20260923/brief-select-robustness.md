# Brief — robustness bound in perarm-select.py (so the owner can pick before thermal re-runs)

Timing worktree /home/gperez/GIT-repos/acacia-gr1-par2-timing, benchmarking/gr1-par2-20260923/
campaign/perarm-select.py and README. NEVER stage or commit. Timed legs are running: dry runs on the
existing fixture only, serial, python3 -s, never /tmp.

Owner decision: the owner picks the portfolio straight after the legs, unless the thermal re-runs
could change the choice. Add `--robustness`, reusing the selection-relevant re-run candidate
logic already in the selector and thermal-annotate.py (legs flagged hot or "thermal unknown"):
- For the top-ranked 4-arm subset T4 and the top-ranked 5-arm subset T5 separately, and for every
  challenger C of the same size, compare T's WORST case with C's BEST case under the usual
  (solved, PAR-2) order:
  - T's worst case: each re-run candidate on T's arms moves against T. A candidate solve in
    [0.8·cap, cap] becomes unsolved (120 s), unless another arm of T solves that instance.
    Candidate timeouts stay unsolved.
  - C's best case: each re-run candidate on C's arms moves in C's favour. A candidate
    timeout/memout/error becomes a solve at 0.8·cap with the reference verdict (from any other
    leg or reference that solved it). Instances with no known verdict stay unsolved.
- Report ROBUST if T beats every challenger this way. Otherwise list the challengers that could
  overtake T, and the instances that decide it; those are the re-runs that must happen before the
  pick.
- Also report, per size, the solved and PAR-2 gap between T and the runner-up in the nominal
  ranking.
Dry-run on the fixture (it substitutes legs for missing arms) and on the real legs available now,
stating which is which. Update the README's selection step to: run the selector with
`--robustness`; if ROBUST, present the ranking to the owner at once and move the re-runs to after
the obfuscated runs. Append the dry-run output to build_scratch/postleg/REPORT.md. VERDICT at end.
