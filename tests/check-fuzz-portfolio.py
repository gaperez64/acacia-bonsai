#!/usr/bin/env python3
"""P4: replay generated TLSF against the build's actual default portfolio arms."""

import argparse
from collections import Counter
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarking"))
from benchlib import classify_acacia_run, run_process_group, run_systemd_scope  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--replayer", type=Path, required=True)
    parser.add_argument("--kind", choices=("roundtrip", "dpa"), default="roundtrip")
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("corpus", type=Path)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.replayer = args.replayer.resolve()
    args.work_dir = args.work_dir.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    arms = subprocess.run([str(args.replayer), "--default-arms"], check=True,
                          capture_output=True, text=True, timeout=10,
                          cwd=args.work_dir).stdout.splitlines()
    if len(arms) < 2:
        print("P4 SKIP: fewer than two default arms")
        return 77
    files = sorted(p for p in args.corpus.rglob("*") if p.is_file()) if args.corpus.is_dir() \
        else [args.corpus]
    if not files:
        parser.error("empty corpus")

    def scoped(command):
        return run_systemd_scope(command, args.timeout, "8G", unit_prefix="acacia-fuzz",
                                 scratch_root=args.work_dir / "fuzz-scopes")

    use_scopes = False
    if shutil.which("systemd-run"):
        probe = scoped(["/usr/bin/true"])
        use_scopes = probe.returncode == 0 and not probe.timed_out
    # RLIMIT_AS can reject OxiDD/Rust's virtual reservations. RLIMIT_DATA bounds
    # writable allocations while allowing PROT_NONE reservations. This fallback
    # is inherited per process, not an aggregate cgroup memory bound.
    # ASan's writable shadow also exceeds RLIMIT_DATA: use scopes or skip, using
    # the sanitizer workflow's existing marker rather than breaking its arms.
    if not use_scopes and os.environ.get("ACACIA_TEST_SKIP_RLIMIT_AS") == "1":
        print("P4 SKIP: sanitizer shadow memory needs systemd scopes; "
              "8 GiB RLIMIT_DATA is incompatible")
        return 77
    print("P4: each arm in its own 8 GiB scope" if use_scopes else
          "P4: systemd-run --user unavailable; using per-process 8 GiB RLIMIT_DATA and timeouts",
          flush=True)
    checked = skipped = failures = variants = 0
    outcomes = Counter()
    for file in files:
        original = subprocess.run([str(args.replayer), "decode", args.kind, str(file.resolve())],
                                  check=True, capture_output=True, text=True, timeout=10).stdout
        texts = [("", original)]
        if "SEMANTICS: Finite," in original:
            # Use the same companion rewrite as P2/P3, retaining the finite skip.
            infinite = subprocess.run([str(args.replayer), "decode-infinite", args.kind,
                                       str(file.resolve())], check=True, capture_output=True,
                                      text=True, timeout=10).stdout
            texts.append(("-infinite", infinite))
            variants += 1
        for suffix, text in texts:
            with tempfile.TemporaryDirectory(prefix="fuzz-portfolio-", dir=args.work_dir) as work:
                source = Path(work) / "spec.tlsf"
                source.write_text(text)
                answers = {}
                for arm in arms:
                    command = [str(args.binary), "--arms", arm, "-T", str(source)]
                    run = scoped(command) if use_scopes else run_process_group(
                        ["prlimit", "--data=8589934592", "--", *command], args.timeout)
                    outcome = classify_acacia_run(run)
                    outcomes[outcome] += 1
                    if outcome in {"REALIZABLE", "UNREALIZABLE"}:
                        answers[arm] = outcome
                if len(answers) < 2:
                    skipped += 1
                    continue
                checked += 1
                # P4: Sound arms on the same game cannot prove opposite verdicts.
                # Declines, unsupported/finite semantics, limits and timeouts supply no verdict.
                if len(set(answers.values())) > 1:
                    failures += 1
                    finding = f"P4 {file}{suffix}: {answers}\n{text}\n"
                    print(finding, file=sys.stderr)
                    with (args.work_dir / "findings-p2.txt").open("a") as output:
                        output.write(finding)
                    destination = args.work_dir / "findings"
                    destination.mkdir(exist_ok=True)
                    shutil.copyfile(file, destination / (file.name + "-p4" + suffix))
                    (destination / (file.name + "-p4" + suffix + ".tlsf")).write_text(text)
    print(f"P4: checked={checked} skipped={skipped} seeds={len(files)} arms={len(arms)} "
          f"finite-variants={variants} outcomes={dict(outcomes)}", flush=True)
    return 1 if failures else 0 if checked else 77


if __name__ == "__main__":
    sys.exit(main())
