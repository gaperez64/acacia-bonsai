# Brief Stage B fix + game namespace fix (tlsf-tools, branch generic-provenance, uncommitted)

NEVER stage or commit by any path. One job; never /tmp. Read
/home/gperez/GIT-repos/tlsf-tools/build-stageB-review/REPORT.md (REJECT: P0 name collisions give false
origins; P1 duplicate origin keys for distinct expanded conjuncts; P1 provenance SHA not bound to the
snapshot that built the game) and /home/gperez/GIT-repos/acacia-bonsai/build_scratch/review-stageA/
REPORT.md finding 1 plus /home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/campaign/
stageA-review-remediation.md (the ownership and monitor/latch symbol-collision defects).
Produce two patch files under build-stageB-fix/ that apply in order on c956847 plus the current
uncommitted Stage B work already in the tree (i.e. A = the Stage B work INCLUDING these fixes,
B = namespace fix on top), and leave the tree in the final A+B state:
A. Stage B with all three review findings fixed exactly as the reviewer describes: reject (fail
   closed, ambiguous=true, consumer declines) any expanded-name collision and validate one-to-one
   names again in the Python consumer; give every distinct expanded conjunct a unique, stable
   structural coordinate (generated-position / call-site path for temporal ranges and repeated
   macro/generator sites) or mark it ambiguous; snapshot and hash the input once in
   gr1_monitor_game.py and feed that exact snapshot to every lowering/provenance call, verifying the
   emitted SHA against it. Regression tests for each reproducer in the report.
B. Injective game-symbol namespace: signal symbols in the emitted AIG must be an injective encoding
   in which environment inputs and system outputs occupy disjoint namespaces that no internal
   symbol (monitor latches, helper latches, fairness/justice names) can collide with, regardless of
   the TLSF signal names. E.g. outputs keep `controllable_<name>` and inputs become
   `uncontrollable_<name>` (so no input symbol can start with `controllable_`), and internal latches
   keep names that start with neither prefix; or use typed ownership metadata if the solver/checker
   already support it — keep solver/checker ownership classification correct and prove it. Any code
   in tlsf-tools that maps game symbols back to TLSF names (certificate/policy export, provenance
   metadata, docs) must decode consistently. Regression tests: a TLSF input named
   `controllable_a` (the reviewer's `G !a` vs renamed pair must now give the same verdict), inputs
   and outputs named like monitor latches, and alpha-renaming invariance of solve/check verdicts on
   ≥20 small generated GR(1) specs. Document the AIG symbol change (games are no longer
   byte-identical to before for patch B only; patch A must keep them byte-identical).
Then point the Acacia tests at the new build and report: run
`cd /home/gperez/GIT-repos/acacia-bonsai && ACACIA_TLSF_TOOLS_BUILD=<your build dir> .venv/bin/python -m
pytest tests/pytest/test_acacia_lift_direct.py tests/pytest/test_acacia_lift_obfuscation.py
tests/pytest/test_acacia_lift_generic_guard.py -q` (do not edit Acacia files; report whether the two
strict xfails now XPASS, and anything in Acacia that decodes game symbols and would need updating).
Build in build-stageB-fix/ (meson setup -Dbuildtype=release -Doxidd=enabled -Dresearch_tools=true;
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig); full suite `meson test --num-processes 1`; clang-format.
VERDICT at end.
