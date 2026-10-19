#!/usr/bin/env python3
"""Check opt-in normalization at the actual frontend/worker boundaries."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


binary, inspector, scratch = (Path(arg).resolve() for arg in sys.argv[1:4])
default_arms = sys.argv[4].split(",")
source_root = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "native_fixtures", source_root / "check-native-gr1-lift-cli.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)

MATCH = '''INFO { TITLE: "normalization" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEE { G (!i || F o); } }
'''
STRICT = '''INFO { TITLE: "strict implication" SEMANTICS: Strict,Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; }
REQUIRE { i; } ASSERT { o; } GUARANTEE { G (!i || F o); } }
'''


def run(*args, records=None, prefix=()):
    env = os.environ.copy()
    env.pop("ACACIA_PHASE_RECORDS", None)
    if records:
        env["ACACIA_PHASE_RECORDS"] = str(records)
    return subprocess.run([*prefix, str(binary), *map(str, args)], env=env,
                          capture_output=True, text=True, timeout=20)


def normalization(result):
    return [json.loads(line) for line in result.stderr.splitlines()
            if line.startswith('{"stage":"tlsf_normalization"')]


def memory_shares(result):
    return {arm: int(value) for arm, value in
            re.findall(r"memory share \(bytes\): (\S+)=(\d+)", result.stderr)}


def rows(directory, event):
    return [row for path in directory.glob("*.jsonl")
            for line in path.read_text().splitlines()
            if (row := json.loads(line)).get("event") == event]


def inspect(path, *options):
    result = subprocess.run([str(inspector), *options, str(path)],
                            capture_output=True, check=True, timeout=10)
    if options and options != ("--normalization", "off"):
        report, = result.stderr.decode().splitlines()
        prefix, schedule, changed, rejected, before, after, elapsed = report.split("\t")
        assert prefix == "normalization" and schedule != "off", report
        assert changed in ("0", "1") and rejected == "0", report
        assert all(int(value) > 0 for value in (before, after, elapsed)), report
    else:
        assert not result.stderr, result
    return result.stdout.split(b"\0")


with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    source = root / "spec.tlsf"
    checked_identities = set()
    delayed = MATCH.replace("G (!i || F o);", "G (!i || F o); G (i <-> X o);")
    for text in (MATCH, MATCH.replace("Mealy", "Moore"), STRICT, delayed):
        source.write_text(text)
        original = inspect(source)
        assert original == inspect(source, "--normalization", "off")
        for route, arm in (("parity", "both:parity:spot"), ("legacy", "real:small:backward")):
            baseline_records = root / "baseline"
            baseline_records.mkdir(exist_ok=True)
            baseline = run("-T", source, "--arms", arm, "-v", records=baseline_records)
            assert baseline.returncode == 0, baseline
            assert not normalization(baseline), baseline
            assert "memory share" not in baseline.stderr, baseline
            baseline_identities = rows(baseline_records, "work_identity")
            if route == "parity":
                baseline_identity, = baseline_identities
                assert (baseline_identity["preprocessing_options_sha256"] ==
                        hashlib.sha256(b"parity-route").hexdigest()), baseline_identity
            for path in baseline_records.glob("*.jsonl"):
                path.unlink()
            for policy in ("match-safe", "route-safe"):
                effective = inspect(source, "--normalization", policy)
                assert effective[1:] == original[1:], (original, effective)
                if text == MATCH and policy == "match-safe":
                    assert effective[0] != original[0], (original, effective)
                records = root / "normalized"
                records.mkdir(exist_ok=True)
                result = run("-T", source, "--arms", arm, f"--{route}-normalization", policy,
                             "-v", records=records)
                assert result.returncode == baseline.returncode, result
                report, = normalization(result)
                assert report["route"] == route and report["schedule"] != "off", report
                assert report["nodes_before"] > 0 and report["nodes_after"] > 0, report
                assert report["time_ns"] > 0 and not report["bounds_rejected"], report
                if text == MATCH and policy == "match-safe":
                    assert report["changed"], report
                identities = rows(records, "work_identity")
                if baseline_identities:
                    baseline_identity, = baseline_identities
                    identity, = identities
                    assert (identity["preprocessing_options_sha256"] !=
                            baseline_identity["preprocessing_options_sha256"]), identity
                    checked_identities.add(route)
                if route == "parity":
                    identity, = identities
                    worker, = rows(records, "worker_start")
                    assert worker["worker_pid"] == identity["worker_pid"], (worker, identity)
                for path in records.glob("*.jsonl"):
                    path.unlink()
            quiet = run("-T", source, "--arms", arm, f"--{route}-normalization", "match-safe")
            assert quiet.returncode == 0 and not normalization(quiet), quiet
            other = "legacy" if route == "parity" else "parity"
            unrelated = run("-T", source, "--arms", arm, "-v",
                            f"--{other}-normalization", "match-safe")
            assert unrelated.returncode == 0 and not normalization(unrelated), unrelated
    assert checked_identities == {"parity", "legacy"}, checked_identities

    # Reuse the native lifting fixture; its source identity must stay byte-for-byte raw.
    source.write_text(native.REAL)
    native_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    for options in ([], ["--legacy-normalization", "match-safe"],
                    ["--parity-normalization", "route-safe"]):
        records = root / "native"
        records.mkdir(exist_ok=True)
        result = run("-T", source, "--arms", "both:gr1-real-lift:oxidd", "-v",
                     *options, records=records)
        assert result.returncode == 0 and not normalization(result), result
        assert "memory share" not in result.stderr, result
        identity, = rows(records, "native_work_identity")
        assert identity["source_sha256"] == native_hash, identity
        for path in records.glob("*.jsonl"):
            path.unlink()

    # The unreal-only legacy arm cannot decide this realizable fixture.
    mixed_arms = "unreal:automaton:forward,both:gr1-real-lift:oxidd"
    baseline = run("-T", source, "--arms", mixed_arms, "-v")
    assert baseline.returncode == 0 and not normalization(baseline), baseline
    records = root / "mixed"
    records.mkdir()
    mixed = run("-T", source, "--arms", mixed_arms,
                "--legacy-normalization", "match-safe", "-v", records=records)
    assert mixed.returncode == baseline.returncode, mixed
    assert mixed.stdout.splitlines()[-1] == baseline.stdout.splitlines()[-1] == "REALIZABLE", mixed
    report, = normalization(mixed)
    assert report["route"] == "legacy" and report["schedule"] != "off", report
    identity, = rows(records, "native_work_identity")
    assert identity["source_sha256"] == native_hash, identity
    winner, = rows(records, "parent_winner")
    assert winner["worker_pid"] == identity["worker_pid"], (winner, identity)
    assert winner["route"] == "R" and winner["effective_backend"] == "oxidd", winner

    for option in ("--parity-normalization", "--legacy-normalization"):
        invalid = run("-T", source, option, "invalid")
        assert invalid.returncode == 3 and "expects off, match-safe, or route-safe" in invalid.stderr
        for policy in ("off", "match-safe", "route-safe"):
            invalid = run("-T", source, option, policy, "-s", root / "out.aag")
            assert invalid.returncode == 3 and "controller synthesis" in invalid.stderr, invalid
            assert not (root / "out.aag").exists()
    for value in ("0", "-1", "abc", "2x", "18446744073709551615"):
        invalid = run("-T", source, "--memory-share-workers", value)
        assert invalid.returncode == 3 and "positive integer" in invalid.stderr, invalid
    for arm in ("both:parity:spot", "both:obligation:spot", "both:gr1:oxidd",
                "both:gr1-real-lift:oxidd", "real:param-lift:oxidd"):
        shares = []
        for count in (1, 7):
            result = run("-T", source, "--arms", arm, "-v",
                         "--memory-share-workers", count)
            assert result.returncode == 0, result
            applied = memory_shares(result)
            key = {"both:parity:spot": "parity",
                   "both:obligation:spot": "obligation"}.get(arm, arm)
            assert applied.keys() == {key}, result
            shares.append(applied[key])
        assert 0 < shares[1] < shares[0], shares
        quiet = run("-T", source, "--arms", arm, "--memory-share-workers", 6)
        assert quiet.returncode == 0 and "memory share" not in quiet.stderr, quiet

    if (len(default_arms) == 7 and "both:obligation:spot" in default_arms
            and "both:parity:spot" in default_arms):
        scope = ["systemd-run", "--user", "--scope", "--quiet", "-p", "MemoryMax=4G"]
        if shutil.which("systemd-run"):
            probe = subprocess.run([*scope, sys.executable, "-c",
                                    "from pathlib import Path; "
                                    "group = Path('/proc/self/cgroup').read_text().split('::', 1)[1].strip(); "
                                    "print((Path('/sys/fs/cgroup' + group) / 'memory.max').read_text().strip())"],
                                   capture_output=True, text=True, timeout=10)
            reason = probe.stderr.strip() or f"scope memory.max={probe.stdout.strip()}"
        else:
            probe = None
            reason = "systemd-run unavailable"
        limit = 4 * 1024**3
        if probe is None or probe.returncode or probe.stdout.strip() != str(limit):
            print(f"SKIP full-portfolio memory equality: {reason}")
        else:
            baseline = run("-T", source, "-v", prefix=scope)
            fixed = run("-T", source, "-v", "--memory-share-workers", 6, prefix=scope)
            assert baseline.returncode == fixed.returncode == 0, (baseline, fixed)
            assert "memory share" not in baseline.stderr, baseline
            # main removes the helper before native budgets count ordinary arms.
            ordinary = len(default_arms) - 1
            rss_share = min(limit, 8 * 1024**3) // (ordinary + 1) // 2
            expected = {"both:gr1-real-lift:oxidd": limit // ordinary // 2,
                        "parity": rss_share, "obligation": rss_share}
            assert memory_shares(fixed) == expected, (expected, fixed)
            print(f"Full-portfolio applied allowances match default formulas: {expected}")
    else:
        print("SKIP full-portfolio memory equality: full default portfolio not configured")

print("Normalization routes, identities, timing/strictness and memory shares passed")
