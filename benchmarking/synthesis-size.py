#!/usr/bin/env python3
"""Measure synthesized controllers, then independently check them outside solve time."""

import argparse
import csv
from functools import partial
import math
import json
import os
from pathlib import Path
import re
import statistics
import sys

from benchlib import (
    ROOT, classify_run, load_tool, run_process_group, run_systemd_scope, tlsf_corpus_dir,
)

subset = load_tool("benchmarking/run-subset.py")
coverage = load_tool("benchmarking/run-syntcomp26-coverage.py")
sys.path.insert(0, str(ROOT / "tests"))
from moore_aiger_oracle import Aiger  # noqa: E402


COLUMNS = (
    "instance", "member", "binary_sha256", "expected", "expectation_source", "verdict",
    "wall_seconds", "exit_code", "failure_kind", "M", "I", "L", "O", "A", "REF_SIZE",
    "verification", "verify_seconds", "verify_exit_code",
)
SUMMARY_FAILURES = {
    "failed_verification": "failed_verification",
    "synthesis_timeout": "timeout",
    "unrealizable": "unrealizable",
    "synthesis_resource_limit": "resource_limit",
    "synthesis_error": "error",
    "synthesis_unknown": "unknown",
    "invalid_controller": "invalid_controller",
    "verify_timeout": "verify_timeout",
    "verify_resource_limit": "verify_resource_limit",
    "verify_error": "verify_error",
    "verify_unsupported": "verify_unsupported",
}
SUMMARY_COLUMNS = (
    "member", "attempted", "realizable", "controllers_produced", "verified",
    *SUMMARY_FAILURES, "commonly_verified", "total_ands_latches",
    "median_ands_latches",
)


def positive_seconds(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and positive")
    return number


def ref_size(path):
    match = re.search(r"^\s*//\s*REF_SIZE\s*:\s*(\d+)\s*$", path.read_text(), re.M)
    return match[1] if match else ""


def measure(instance, source, member, binary, digest, checker, directory, timeout,
            verify_timeout, launch, expected, expectation_source, verify_max_states=200000):
    directory.mkdir(parents=True)
    controller = directory / "controller.aag"
    run = launch([str(binary), "-T", str(source), "-s", str(controller)], timeout)
    (directory / "synthesis.stdout").write_text(run.stdout)
    (directory / "synthesis.stderr").write_text(run.stderr)
    result = classify_run(run)
    row = dict.fromkeys(COLUMNS, "")
    row.update(instance=instance, member=member, binary_sha256=digest, expected=expected or "",
               expectation_source=expectation_source,
               verdict=result if result in {"REALIZABLE", "UNREALIZABLE"} else "UNKNOWN",
               wall_seconds=f"{run.seconds:.6f}", exit_code=run.returncode,
               failure_kind="" if result == "REALIZABLE" else result.lower(),
               REF_SIZE=ref_size(source), verification="unverified")
    if result != "REALIZABLE":
        return row
    try:
        # Reuse the existing independent parser before handing the circuit to Spot.
        Aiger.read(controller)
        header = controller.read_text().splitlines()[0].split()[1:]
        row.update(zip(("M", "I", "L", "O", "A"), map(int, header)))
    except (OSError, ValueError, IndexError) as error:
        row.update(failure_kind="invalid_controller", verification="unparsable")
        (directory / "verify.stderr").write_text(str(error) + "\n")
        return row
    check = launch([str(checker), str(source), str(controller),
                    "--max-states", str(verify_max_states)], verify_timeout)
    (directory / "verify.stdout").write_text(check.stdout)
    (directory / "verify.stderr").write_text(check.stderr)
    verification = "verify_error"
    if check.timed_out:
        verification = "verify_timeout"
    elif check.resource_limited:
        verification = "verify_resource_limit"
    elif check.returncode == 0 and check.stdout.strip() == "verified":
        verification = "verified"
    elif check.returncode == 1 and check.stdout.strip() == "failed_verification":
        verification = "failed_verification"
    elif check.returncode == 3 and check.stdout.strip() == "verify_unsupported":
        verification = "verify_unsupported"
    row.update(verification=verification, verify_seconds=f"{check.seconds:.6f}",
               verify_exit_code=check.returncode,
               failure_kind="" if verification == "verified" else verification)
    return row


def summarize(rows, members):
    by_member = {member: [r for r in rows if r["member"] == member] for member in members}
    common = set.intersection(*(
        {r["instance"] for r in group if r["verification"] == "verified"}
        for group in by_member.values()))
    summaries = []
    for member, group in by_member.items():
        sizes = [int(r["A"]) + int(r["L"]) for r in group if r["instance"] in common]
        summaries.append(dict(
            member=member, attempted=len(group),
            realizable=sum(r["verdict"] == "REALIZABLE" for r in group),
            controllers_produced=sum(r["A"] != "" for r in group),
            verified=sum(r["verification"] == "verified" for r in group),
            **{column: sum(r["failure_kind"] == kind for r in group)
               for column, kind in SUMMARY_FAILURES.items()},
            commonly_verified=len(common), total_ands_latches=sum(sizes) if sizes else "",
            median_ands_latches=statistics.median(sizes) if sizes else "",
        ))
    return summaries


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", action="append", required=True, metavar="MEMBER=PATH")
    parser.add_argument("--checker", type=Path, required=True)
    parser.add_argument("--list", type=Path, required=True)
    parser.add_argument("--tlsf-map", type=Path, required=True)
    parser.add_argument("--tlsf-corpus", type=Path)
    parser.add_argument("--status-exceptions", type=Path,
                        default=Path(__file__).with_name("syntcomp26-status-exceptions.tsv"))
    parser.add_argument("--expected-status", choices=("REALIZABLE", "UNREALIZABLE", "UNKNOWN", "all"),
                        default="REALIZABLE")
    parser.add_argument("--timeout", type=positive_seconds, default=17)
    parser.add_argument("--verify-timeout", type=positive_seconds, default=60)
    parser.add_argument("--verify-max-states", type=int, default=200000,
                        help="reachable latch valuations allowed per controller (default: 200000)")
    parser.add_argument("--systemd-scope", action="store_true")
    parser.add_argument("--memory-max", default="5G")
    parser.add_argument("--memory-swap-max", default="0")
    parser.add_argument("--output", type=Path, required=True,
                        help="new result directory (refuses to overwrite an existing run)")
    args = parser.parse_args(argv)
    if not 0 < args.verify_max_states <= 2**32 - 1:
        parser.error("--verify-max-states must be a positive 32-bit unsigned integer")
    binaries = {}
    for entry in args.bin:
        member, separator, path = entry.partition("=")
        if not separator or not member or member in binaries:
            parser.error("--bin requires a unique nonempty MEMBER=PATH")
        binary = Path(path).resolve(strict=True)
        if not binary.is_file() or not os.access(binary, os.X_OK):
            parser.error(f"binary is not executable: {binary}")
        binaries[member] = binary, coverage.sha256_file(binary)
    checker = args.checker.resolve(strict=True)
    if not checker.is_file() or not os.access(checker, os.X_OK):
        parser.error(f"checker is not executable: {checker}")
    corpus = tlsf_corpus_dir(args.tlsf_corpus, next(iter(binaries.values()))[0].parent.parent)
    if corpus is None:
        parser.error("no TLSF corpus; supply --tlsf-corpus")
    instances = subset.read_instance_list(args.list)
    if len(set(instances)) != len(instances):
        parser.error("duplicate instance in --list")
    mapping = subset.read_tlsf_map(args.tlsf_map, corpus)
    exceptions = coverage.read_status_exceptions(args.status_exceptions, corpus, required=True)
    selected = []
    for instance in instances:
        source = Path(mapping[instance]).resolve(strict=True)
        if source.name in exceptions:
            expected = exceptions[source.name]
            origin = "exception" if expected is not None else "none"
        else:
            expected, origin = coverage.expected_verdict(source), "status"
            if expected is None:
                origin = "none"
        if args.expected_status == "all" or (expected or "UNKNOWN") == args.expected_status:
            selected.append((instance, source, expected, origin))
    if not selected:
        parser.error("no inputs match --expected-status")
    args.output.mkdir(parents=True)
    metadata = dict(vars(args), checker_sha256=coverage.sha256_file(checker),
                    sources={instance: dict(path=str(source),
                                            sha256=coverage.sha256_file(source))
                             for instance, source, _, _ in selected})
    (args.output / "run.json").write_text(json.dumps(metadata, default=str, indent=2) + "\n")
    launch = (partial(run_systemd_scope, memory_max=args.memory_max,
                      memory_swap_max=args.memory_swap_max) if args.systemd_scope
              else run_process_group)
    rows = []
    with (args.output / "rows.tsv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, COLUMNS, delimiter="\t")
        writer.writeheader()
        for index, (instance, source, expected, origin) in enumerate(selected):
            for member_index, (member, (binary, digest)) in enumerate(binaries.items()):
                row = measure(instance, source, member, binary, digest, checker,
                              args.output.resolve() / f"{index:05d}-{member_index}", args.timeout,
                              args.verify_timeout, launch, expected, origin, args.verify_max_states)
                rows.append(row)
                writer.writerow(row)
                stream.flush()
                print(f"{instance}\t{member}\t{row['failure_kind'] or 'verified'}", flush=True)
    with (args.output / "summary.tsv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, SUMMARY_COLUMNS, delimiter="\t")
        writer.writeheader()
        writer.writerows(summarize(rows, binaries))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
