#!/usr/bin/env python3
"""Screen/replay the existing Posets harness; preserve every rejected cell."""

import argparse
from collections import Counter
import csv
from dataclasses import asdict
import hashlib
import json
import math
import os
from pathlib import Path
import re
import resource
import random
import shlex
import subprocess

from benchlib import RunResult, campaign_scope_guard, classify_run, run_systemd_scope

ROOT = Path(__file__).resolve().parents[1]
MODES = ("compatible", "native", "max")
CASES = ("vectors", "nonempty", "empty", "zero")
BACKENDS = {"bboxtree_backed", "comptrie_backed", "kdtree_backed", "vector_or_kdtree_backed",
            "vector_backed", "rank_bucketed_vector_backed", "filtered_vector_backed", "vector_backed_bin",
            "vector_backed_one_dim_split_intersection_only", "sharingtree_backed", "simple_sharingtree_backed",
            "sharingtrie_backed", "skiplist_backed", "cst_backed"}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def regular(path):
    require(path.is_file() and not path.is_symlink(), f"Missing/nonregular input: {path}")
    return path


def libraries(binary):
    result = subprocess.run(["ldd", str(binary)], cwd=ROOT, capture_output=True, text=True, check=True,
                            env={**os.environ, "LC_ALL": "C"})
    require(not result.stderr.strip(), f"ldd: {result.stderr}")
    rows = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or re.fullmatch(r"linux-vdso\.so\.\d+ \(0x[0-9a-fA-F]+\)", line):
            continue
        require("not found" not in line, f"Missing dynamic dependency: {line}")
        match = re.fullmatch(r"(?:\S+ => )?(.+) \(0x[0-9a-fA-F]+\)", line)
        require(match is not None and "/" in match[1], f"Unrecognized dynamic dependency: {line}")
        path = Path(match[1])
        if not path.is_absolute():
            path = ROOT / path
        rows.append({"path": str(path), "realpath": str(path.resolve(strict=True)), "sha256": sha(path)})
    require(rows, "No dynamic dependencies resolved")
    return sorted(rows, key=lambda row: row["path"])


def frozen_identity(path, binary_sha256):
    document = json.loads(regular(path).read_text())
    require(document["binary_sha256"] == binary_sha256 and
            isinstance(document["source_revision"], str) and re.fullmatch(r"[0-9a-f]{40}", document["source_revision"]),
            "Frozen provenance does not bind this binary/source revision")
    options = document["meson_options"]
    require(len({row["name"] for row in options}) == len(options), "Duplicate frozen build option")
    require({row["name"]: row["value"] for row in options}.get("acacia_enable_diagnostics") is True,
            "Frozen binary provenance does not enable diagnostics")


def capture_identity(path, instance):
    """Validate the serial capture helper's immutable invocation/file binding."""
    document = json.loads(regular(path).read_text())
    require(type(document.get("schema_version")) is int and document["schema_version"] == 1 and
            document.get("kind") == "posets-operation-capture" and document.get("instance") == instance,
            "Capture schema/instance identity mismatch")
    sources = {str(path): sha(path)}
    for name in ("input", "binary", "frozen_provenance"):
        entry = document[name]
        source = regular(Path(entry["path"]))
        require(source.is_absolute() and sha(source) == entry["sha256"], f"Capture {name} bytes changed")
        sources[str(source)] = entry["sha256"]
    frozen_identity(Path(document["frozen_provenance"]["path"]), document["binary"]["sha256"])
    for name, expected in document["implementation_sha256"].items():
        require(Path(name).is_absolute() and sha(regular(Path(name))) == expected, "Capture implementation changed")
        sources[name] = expected
    require({Path(name).name for name in document["implementation_sha256"]} ==
            {"posets-capture.py", "posets-matrix.py", "benchlib.py", "scope_memory.py"}, "Incomplete capture implementation identity")
    require(set(document["implementation_source_paths"]) == {Path(name).name for name in document["implementation_sha256"]} and
            all(Path(source).is_absolute() and Path(source).name == name
                for name, source in document["implementation_source_paths"].items()), "Invalid historical helper source paths")
    for library in document["runtime_libraries"]:
        file = Path(library["path"])
        require(str(file.resolve(strict=True)) == library["realpath"] and sha(file) == library["sha256"],
                "Capture runtime dependency changed")
        sources[library["realpath"]] = library["sha256"]
    require(document["runtime_libraries"], "Missing capture runtime dependency identities")
    snapshot = Path(document["snapshot_directory"])
    require(snapshot.is_absolute() and snapshot.is_dir() and not snapshot.is_symlink(), "Invalid snapshot directory")
    actual = {}
    for file in snapshot.rglob("*"):
        require(not file.is_symlink() and (file.is_file() or file.is_dir()), "Nonregular snapshot evidence")
        if file.is_file():
            actual[str(file.relative_to(snapshot))] = sha(file)
            sources[str(file)] = sha(file)
    require(actual == document["snapshot_files_sha256"], "Capture snapshot inventory/hash mismatch")
    binary, tlsf = document["binary"]["path"], document["input"]["path"]
    require(document["command"] == [binary, "--arms", "real:small:backward", "--spot-fast", "off",
                                    "--r-prepass", "off", "--equivariance", "off", "-T", tlsf],
            "Capture command does not bind the isolated classic-backward input/binary")
    env = document["environment"]
    require(all(isinstance(name, str) and isinstance(value, str) for name, value in env.items()), "Invalid capture environment")
    required = {"ACACIA_DIAG": "1", "ACACIA_DIAG_INSTANCE": instance, "ACACIA_DIAG_PROGRESS_EVERY": "0",
                "ACACIA_ANTICHAIN_SNAPSHOT_DIR": str(snapshot), "ACACIA_ANTICHAIN_SNAPSHOT_CPRE": "1",
                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_FRONTIERS": "1", "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS": "1",
                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX": "8", "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS": "4096",
                "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS_MAX": "16"}
    require(all(env.get(name) == value for name, value in required.items()), "Capture instrumentation mismatch")
    result = document["result"]
    require(type(result["returncode"]) is int and type(result["timed_out"]) is bool and
            type(result["resource_limited"]) is bool, "Missing raw capture termination subtype")
    for name in ("seconds", "cpu_seconds"):
        value = result.get(name)
        require((name == "cpu_seconds" and value is None) or
                (type(value) in (int, float) and math.isfinite(value) and value >= 0), "Invalid capture duration")
    for name in ("stdout_bytes", "stderr_bytes", "memory_peak_bytes", "max_process_rss_bytes"):
        value = result.get(name)
        require(value is None or (type(value) is int and value >= 0), "Invalid capture byte counter")
    require(type(document["treatment"]["timeout"]) in (int, float) and type(document["treatment"]["serial"]) is bool and
            document["treatment"] == {"timeout": 17, "memory_max": "8G", "memory_swap_max": "0", "serial": True},
            "Unexpected capture budget")
    for name in ("stdout", "stderr"):
        file = regular(path.parent / name)
        require(sha(file) == document["logs_sha256"][name + "_sha256"], "Capture log changed")
        sources[str(file)] = sha(file)
    require(not (set(result) - (RunResult.__dataclass_fields__.keys() - {"stdout", "stderr"})), "Unknown capture process field")
    observed = RunResult((path.parent / "stdout").read_text(), (path.parent / "stderr").read_text(), **result)
    require(document["classification"] == classify_run(observed), "Capture classification disagrees with raw observation")
    return document, sources


def load_traces(plan):
    """Hash the declared, fixed split and all capture evidence, including failures."""
    document = json.loads(regular(plan).read_text())
    require(type(document.get("schema_version")) is int and document["schema_version"] == 1,
            "Unsupported trace plan schema")
    rows, sources, directories, groups, instances, inputs, owners, seen, captures = [], {str(plan): sha(plan)}, {}, {}, {}, {}, {}, set(), {}
    for number, entry in enumerate(document["traces"]):
        require(set(entry) == {"instance", "family", "split", "path", "capture_manifest"},
                "Each trace needs instance/family/split/path/capture_manifest")
        require(all(isinstance(value, str) and value for value in entry.values()) and
                entry["split"] in ("train", "heldout"),
                "Missing instance/family or invalid split")
        require(groups.setdefault(entry["family"], entry["split"]) == entry["split"],
                "A family crosses the declared train/held-out boundary")
        require(instances.setdefault(entry["instance"], (entry["family"], entry["split"])) ==
                (entry["family"], entry["split"]), "An instance crosses family/split assignments")
        path = regular(plan.parent / entry["path"]).resolve()
        require(re.fullmatch(r"(?:cpre|contains)-\d+\.tsv", path.name), "Not a finalized operation trace")
        require(path not in seen, "Repeated trace in plan")
        seen.add(path)
        capture = regular(plan.parent / entry["capture_manifest"]).resolve()
        if str(capture) not in captures:
            captures[str(capture)], bound = capture_identity(capture, entry["instance"])
            sources.update(bound)
        invocation = captures[str(capture)]
        require(invocation["instance"] == entry["instance"] and path.is_relative_to(invocation["snapshot_directory"]),
                "Operation is outside its declared instance/capture")
        require(inputs.setdefault(entry["instance"], invocation["input"]["sha256"]) == invocation["input"]["sha256"],
                "An instance refers to different TLSF bytes")
        directory = path.parent
        require(owners.setdefault(str(directory), (entry["instance"], str(capture))) ==
                (entry["instance"], str(capture)), "An automaton directory has multiple instance/capture owners")
        if str(directory) not in directories:
            files = sorted(directory.iterdir())
            require(all(file.is_file() and not file.is_symlink() for file in files),
                    "Nonregular entry in frozen automaton capture")
            hashes = {str(file): sha(file) for file in files}
            sources.update(hashes)
            with regular(directory / "meta.tsv").open() as stream:
                meta, = csv.DictReader(stream, delimiter="\t")
            require(meta["schema_version"] == "3", "Unsupported capture metadata")
            require(meta["instance"] == entry["instance"], "Snapshot metadata instance differs from invocation")
            regular(directory / "automaton.hoa")
            counts = None
            if (count_file := directory / "operation-counts.tsv").exists():
                with count_file.open() as stream:
                    raw, = csv.DictReader(stream, delimiter="\t")
                require(count_file.read_bytes().endswith(b"\n"), "Truncated operation counts")
                require(set(raw) == {"complete", "contains", "hits", "misses", "cpre_calls"},
                        "Malformed operation count table")
                counts = {name: int(value) for name, value in raw.items()}
                require(counts["complete"] == 1 and all(value >= 0 for value in counts.values()) and
                        counts["contains"] == counts["hits"] + counts["misses"], "Incomplete/inconsistent counts")
                require(counts["contains"] >= sum(bool(re.fullmatch(r"contains-\d+\.tsv", file.name)) for file in files) and
                        counts["cpre_calls"] >= sum(bool(re.fullmatch(r"cpre-\d+\.(?:tsv|skipped)", file.name)) for file in files),
                        "Complete operation counts are below observed captures")
            directories[str(directory)] = {"files_sha256": hashes, "metadata": meta,
                                            "complete_operation_counts": counts,
                                            "frequency_scope": "configured automaton only" if counts else "unknown/censored"}
        lines = path.read_text().splitlines()
        header = dict(re.findall(r"(?:^|\s)(\w+)=([^\s]+)", lines[0]))
        kind = "contains" if path.name.startswith("contains-") else "cpre"
        counts = directories[str(directory)]["complete_operation_counts"]
        if kind == "contains" and counts:
            ordinal, hits = int(header["ordinal"]), int(header["hits"])
            require(ordinal <= counts["contains"] and hits <= counts["hits"] and
                    ordinal - hits <= counts["misses"], "Query ordinal exceeds complete operation counts")
        begin = lines.index("[generators]" if kind == "contains" else "[before]") + 1
        end = lines.index("[end]" if kind == "contains" else "[actions]")
        generators = [tuple(map(int, line.split("\t"))) for line in lines[begin:end]]
        meta = directories[str(directory)]["metadata"]
        dimension, numeric = int(meta["states"]), int(meta["bool_threshold"])
        require(dimension > 0 and 0 <= numeric <= dimension and
                all(len(value) == dimension for value in generators), "Invalid logical dimension")
        coordinates = len(generators) * dimension
        rows.append({**entry, "id": number, "path": str(path), "capture_manifest": str(capture),
                     "kind": kind, "header": header,
                     "features": {"dimension": dimension, "numeric_prefix": numeric,
                                  "boolean_tail": dimension - numeric, "maxima": len(generators),
                                  "bottom_fraction": sum(value == -1 for row in generators for value in row) / coordinates if coordinates else None,
                                  "unique_numeric_prefixes": len({row[:numeric] for row in generators}),
                                  "unique_boolean_suffixes": len({row[numeric:] for row in generators}),
                                  "recorded_answer": int(header["expected"]) if kind == "contains" else None}})
    require(rows, "Empty trace plan")
    return rows, {"plan": str(plan), "sources_sha256": sources, "directories": directories, "captures": captures,
                  "sampling": "first CPre / hit / miss per frontier bin; unweighted point samples",
                  "split_policy": "fixed plan; complete families stay together; held-out data cannot tune thresholds"}


def unchanged(metadata, traces=None):
    for row in metadata["modes"].values():
        require(sha(Path(row["binary"])) == row["binary_sha256"], "A harness executable changed")
        require(libraries(Path(row["binary"])) == row["libraries"], "Resolved runtime dependencies changed")
    for name, expected in metadata["source_sha256"].items():
        require(sha(ROOT / "subprojects/posets" / name) == expected, "A Posets source changed")
    require(sha(Path(__file__).resolve()) == metadata["driver_sha256"], "The matrix driver changed")
    for name, expected in metadata["support_sha256"].items():
        require(sha(ROOT / "benchmarking" / name) == expected, "Runner support changed")
    if traces:
        for name, expected in traces["sources_sha256"].items():
            require(sha(regular(Path(name))) == expected, f"Capture evidence changed: {name}")
        for name, entry in traces["directories"].items():
            require({str(path) for path in Path(name).iterdir()} == set(entry["files_sha256"]),
                    "Capture file inventory changed")


def save_logs(output, prefix, stdout, stderr):
    hashes = {}
    for suffix, text in (("stdout", stdout), ("stderr", stderr)):
        path = output / (prefix + "." + suffix)
        path.write_bytes(text.encode() if isinstance(text, str) else text)
        hashes[suffix + "_sha256"] = sha(path)
    return hashes


def process_record(result):
    return {key: value for key, value in asdict(result).items() if key not in ("stdout", "stderr")}


def status(result):
    return "timeout" if result.timed_out else "resource_limit" if result.resource_limited else "pass" if result.returncode == 0 else "rejected"


def measurement_build(metadata):
    common = None
    for mode in metadata["modes"].values():
        command = mode["compiler_command"]
        flags = command.get("arguments") or shlex.split(command["command"])
        optimization = [flag for flag in flags if re.fullmatch(r"-O(?:[0-3sgz]|fast)", flag)]
        require(mode.get("ndebug") is True and mode.get("stdlib_assertions") is False and
                optimization and optimization[-1] in ("-O3", "-Ofast") and
                any(flag.startswith("-flto") for flag in flags),
                "Timing requires release/O3-or-Ofast/LTO with NDEBUG and no stdlib assertions")
        comparable = (flags[0], mode["compiler"], tuple(flag for flag in flags
                      if flag.startswith(("-O", "-march=", "-mtune=", "-flto", "-std=", "-fno-"))))
        require(common is None or common == comparable, "Matrix compiler/target/optimization flags differ")
        common = comparable


def metrics(stdout):
    values = {}
    for name, value in re.findall(r"^  - ([\w]+): (\S+)$", stdout, flags=re.MULTILINE):
        require(name not in values, "Duplicate harness metric")
        values[name] = float(value)
    require(values and all(math.isfinite(value) and value >= 0 for value in values.values()),
            "Missing/nonfinite/negative harness metrics")
    return values


def output_identity(stdout, backend, vector):
    seen = re.findall(r"^\[--\] running tests for (.+)$", stdout, flags=re.MULTILINE)
    return seen == [f"posets::downsets::{backend}<posets::vectors::{vector}<test_value_type>>"]


def replay(args, metadata, traces, output):
    rows, accepted = [], []
    scalar_mode = next(mode for mode in metadata["modes"] if metadata["aliases"][mode] == mode)
    cells = [(mode, backend, vector, entry) for mode, entry in metadata["modes"].items()
             if metadata["aliases"][mode] == mode for backend in entry["backends"] if backend in args.backends
             for vector in ("vector_backed", "simd_vector_backed") if vector != "vector_backed" or mode == scalar_mode]
    with campaign_scope_guard("posets-matrix"):
        for trace in traces:
            source_command = [cells[0][3]["binary"], "--trace", trace["path"], "--check=trace-source"]
            source_result = run_systemd_scope(source_command, args.source_timeout, args.memory_max, "0",
                                              unit_prefix="acacia-posets", scratch_root=output / "scopes")
            source_prefix = f"source-{trace['id']}"
            source_logs = save_logs(output, source_prefix, source_result.stdout, source_result.stderr)
            source_status = status(source_result)
            if source_status == "pass" and source_result.stdout != "[TRACE SOURCE OK]\n":
                source_status = "malformed_output"
            source_ok = source_status == "pass"
            rows.append({"trace": trace["id"], "phase": "source_oracle", "status": source_status,
                         "command": source_command, "log_prefix": source_prefix,
                         "logs_sha256": source_logs, "process": process_record(source_result)})
            for mode, backend, vector, entry in cells:
                if not source_ok:
                    rows.append({"trace": trace["id"], "mode": mode, "backend": backend, "vector": vector,
                                 "phase": "check", "status": "source_unverified", "source_log_prefix": source_prefix})
                    continue
                command = [entry["binary"], "--trace", trace["path"], "--check=replay",
                           "--params=rounds=1", backend, vector + "<test_value_type>"]
                prefix = f"check-{trace['id']}-{mode}-{backend}-{vector}"
                result = run_systemd_scope(command, args.check_timeout, args.memory_max, "0",
                                           unit_prefix="acacia-posets", scratch_root=output / "scopes")
                logs = save_logs(output, prefix, result.stdout, result.stderr)
                outcome = status(result)
                identity = output_identity(result.stdout, backend, vector)
                require(outcome != "pass" or identity, "Passing cell did not invoke exactly the requested backend/vector")
                rows.append({"trace": trace["id"], "mode": mode, "backend": backend, "vector": vector,
                             "phase": "check", "status": outcome, "process": process_record(result),
                             "command": command, "log_prefix": prefix, "logs_sha256": logs,
                             "output_identity_verified": identity})
                if outcome == "pass" and vector == "simd_vector_backed":
                    accepted.append((trace, mode, backend, vector, entry))
        if args.measure:
            order = []
            for repetition in range(args.repetitions):
                rng = random.Random(args.seed + repetition)
                groups = [[cell for cell in accepted if cell[0]["id"] == trace["id"]] for trace in traces]
                rng.shuffle(groups)
                for group in groups:
                    rng.shuffle(group)
                    order.extend((repetition, *cell) for cell in group)
            planned = [{"repetition": repetition, "trace": trace["id"], "mode": mode, "backend": backend,
                        "vector": vector} for repetition, trace, mode, backend, vector, _ in order]
            (output / "measurement-order.json").write_text(json.dumps(planned, indent=2) + "\n")
            for repetition, trace, mode, backend, vector, entry in order:
                command = [entry["binary"], "--trace", trace["path"],
                           f"--params=rounds={args.rounds},detail={int(args.detail)}",
                           backend, vector + "<test_value_type>"]
                prefix = f"measure-{repetition}-{trace['id']}-{mode}-{backend}-{vector}"
                result = run_systemd_scope(command, args.case_timeout, args.memory_max, "0",
                                           unit_prefix="acacia-posets", scratch_root=output / "scopes")
                logs = save_logs(output, prefix, result.stdout, result.stderr)
                outcome = status(result)
                identity = output_identity(result.stdout, backend, vector)
                require(outcome != "pass" or identity, "Passing cell did not invoke exactly the requested backend/vector")
                values = metrics(result.stdout) if outcome == "pass" else None
                if values:
                    require(values["rounds"] == args.rounds and
                            values["dimension"] == trace["features"]["dimension"] and
                            values["before_maxima"] == trace["features"]["maxima"] and
                            values["boolean_tail"] == trace["features"]["boolean_tail"],
                            "Metric/source identity mismatch")
                rows.append({"trace": trace["id"], "mode": mode, "backend": backend, "vector": vector,
                             "phase": "measure", "repetition": repetition, "status": outcome,
                             "command": command, "log_prefix": prefix, "logs_sha256": logs, "metrics": values,
                             "output_identity_verified": identity,
                             "process": process_record(result)})
    return rows


def provenance(build, output):
    commands = json.loads((build / "compile_commands.json").read_text())
    modes, aliases, seen = {}, {}, {}
    for mode in MODES:
        binary = build / "tests" / f"downset-matrix-{mode}"
        metadata = json.loads(subprocess.check_output([str(binary), "--metadata"], text=True))
        assembly = subprocess.check_output(
            ["objdump", "--disassemble=posets_matrix_compare", str(binary)], text=True)
        (output / f"{mode}-compare.asm").write_text(assembly)
        instructions = [match[1] for line in assembly.splitlines()
                        if (match := re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)", line))]
        require(bool(instructions), "Missing emitted SIMD comparison kernel")
        instruction_hash = hashlib.sha256(bytes.fromhex(" ".join(instructions))).hexdigest()
        key = (metadata["simd_type"], metadata["alignment"], instruction_hash, metadata.get("hybrid_policy"))
        aliases[mode] = seen.setdefault(key, mode)
        selected = [row for row in commands if f"downset-matrix-{mode}.p/" in row.get("output", "")]
        require(len(selected) == 1, f"Missing/ambiguous compiler command for {mode}")
        require(len(metadata["backends"]) == 14 and set(metadata["backends"]) == BACKENDS,
                "The requested14-backend inventory changed")
        modes[mode] = {**metadata, "binary": str(binary), "binary_sha256": sha(binary),
                       "libraries": libraries(binary),
                       "comparison_instructions_sha256": instruction_hash, "compiler_command": selected[0]}
    source = ROOT / "subprojects/posets"
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                                    cwd=source, text=True).splitlines()
    return {"modes": modes, "aliases": aliases,
            "posets_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
            "source_sha256": {name: sha(source / name) for name in names if (source / name).is_file()},
            "posets_dirty": subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True),
            "support_sha256": {name: sha(ROOT / "benchmarking" / name) for name in ("benchlib.py", "scope_memory.py")},
            "driver_sha256": sha(Path(__file__).resolve()), "affinity": sorted(os.sched_getaffinity(0)),
            "cwd": str(Path.cwd()),
            "environment": {name: value for name, value in os.environ.items()
                            if name.startswith(("ACACIA_", "LD_", "MALLOC_", "GLIBC_", "OMP_"))},
            "cpu": subprocess.check_output(["lscpu", "-J"], text=True)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case-timeout", type=float, default=15)
    parser.add_argument("--check-timeout", type=float, help="Separate backend correctness cap; defaults to --case-timeout")
    parser.add_argument("--source-timeout", type=float, help="Separate scalar source-oracle cap; defaults to --check-timeout")
    parser.add_argument("--trace-plan", type=Path, help="Fixed JSON capture/split plan; otherwise run synthetic semantic cases")
    parser.add_argument("--measure", action="store_true", help="Explicitly enable serial actual-trace timing after correctness checks")
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--detail", action="store_true", help="Instrument apply/union/intersection separately; not clean CPre timings")
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--seed", type=int, default=0, help="Fixed within-trace measurement-order shuffle seed")
    parser.add_argument("--backends", nargs="+", choices=sorted(BACKENDS), default=sorted(BACKENDS),
                        help="Explicit subset for later hybrid/profile follow-ups; default retains all14")
    args = parser.parse_args()
    no_core()
    args.check_timeout = args.case_timeout if args.check_timeout is None else args.check_timeout
    args.source_timeout = args.check_timeout if args.source_timeout is None else args.source_timeout
    require(all(math.isfinite(cap) and cap > 0 for cap in (args.case_timeout, args.check_timeout, args.source_timeout)) and
            args.rounds > 0 and args.repetitions > 0, "Positive finite timeout/rounds/repetitions required")
    require(not args.measure or args.trace_plan, "Timing requires a fixed actual-trace plan")
    require(not args.detail or args.measure, "Detailed instrumentation requires --measure")
    build, output = args.build_dir.resolve(), args.output.resolve()
    if args.trace_plan:
        args.trace_plan = args.trace_plan.resolve()
    os.chdir(ROOT)  # Also used by ldd when a library override contains relative paths.
    require(not output.exists(), "Output exists; preserve it and choose a new directory")
    require(subprocess.run(["git", "check-ignore", "-q", str(output)], cwd=ROOT).returncode == 0,
            "Raw matrix evidence must start in an ignored output directory")
    output.mkdir(parents=True)
    metadata = provenance(build, output)
    if args.measure:
        measurement_build(metadata)
    trace_source = None
    if args.trace_plan:
        traces, trace_source = load_traces(args.trace_plan.resolve())
        (output / "inputs.json").write_text(json.dumps({"provenance": trace_source, "traces": traces}, indent=2) + "\n")
        rows = replay(args, metadata, traces, output)
        unchanged(metadata, trace_source)
        report = {"scope": "Instrumented operation diagnosis" if args.detail else "Clean whole-CPre / sampled actual-query replay" if args.measure else "Actual-trace correctness only",
                  "provenance": metadata, "inputs_sha256": sha(output / "inputs.json"),
                  "measurement_order_sha256": sha(output / "measurement-order.json") if args.measure else None,
                  "treatment": {"memory_max": args.memory_max, "memory_swap_max": "0", "serial": True,
                                "core_limit_bytes": 0,
                                "rounds": args.rounds, "repetitions": args.repetitions,
                                "backends": args.backends,
                                "seed": args.seed,
                                "detail": args.detail, "case_timeout": args.case_timeout},
                  "semantic_caps": {"source_timeout": args.source_timeout, "check_timeout": args.check_timeout},
                  "limits": ["Point samples are unweighted; full search/portfolio frequencies are not reconstructed.",
                             "Scalar vectors run as correctness controls once per backend/trace; timed cells cover unique SIMD treatments.",
                             "Each replay round reconstructs the source frontier; sharing history outside the sampled operation is unknown.",
                             "Build/rebuild include logical-to-vector materialization and backend canonicalization. CPre includes backend conversions.",
                             "CPre membership is a derived initial-state control; contains trace membership is an actual recorded query.",
                             "Process/cgroup peak and CPU include input parsing and result checks. Operation timers exclude them.",
                             "Allocation counts/cache events require separate explicitly instrumented profiles; not collected here.",
                             "The scalar representation is a correctness control; compiler loop vectorization is possible.",
                             "All-Boolean positive-dimensional traces remain distinct from zero-dimensional sets; packed-tail integration requires its own oracle."],
                  "traces": traces, "rows": rows,
                  "counts": dict(Counter(row["status"] for row in rows)),
                  "performance_measurements": sum(row["phase"] == "measure" for row in rows)}
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"output": str(output), "aliases": metadata["aliases"], "counts": report["counts"]}, indent=2))
        return
    rows = []
    for mode, entry in metadata["modes"].items():
        if metadata["aliases"][mode] != mode:
            continue
        for backend in entry["backends"]:
            # Scalar is an independent semantic control, not a promise about the
            # compiler's automatic vectorization of ordinary integer loops.
            for vector in ("vector_backed", "simd_vector_backed"):
                for case in CASES:
                    command = [entry["binary"], "--check=" + case, backend, vector + "<test_value_type>"]
                    prefix = f"{mode}-{backend}-{vector}-{case}"
                    try:
                        result = subprocess.run(command, capture_output=True, text=True,
                                                timeout=args.check_timeout, preexec_fn=no_core)
                        status, code = ("pass" if result.returncode == 0 else "rejected"), result.returncode
                        stdout, stderr = result.stdout, result.stderr
                    except subprocess.TimeoutExpired as error:
                        status, code = "timeout", None
                        stdout, stderr = error.stdout or b"", error.stderr or b""
                    logs = save_logs(output, prefix, stdout, stderr)
                    identity = output_identity(stdout.decode() if isinstance(stdout, bytes) else stdout, backend, vector)
                    require(status != "pass" or identity, "Passing cell did not invoke exactly the requested backend/vector")
                    rows.append({"mode": mode, "backend": backend, "vector": vector,
                                 "case": case, "status": status, "exit": code, "log_prefix": prefix,
                                 "logs_sha256": logs,
                                 "output_identity_verified": identity})
    unchanged(metadata)
    report = {"scope": "Semantic screen only; no operation timings sampled",
              "cases": "empty downset, zero-dimensional singleton and all-minus-one bottom are distinct",
              "policy": "Every rejection/crash/timeout is retained. Constructor preconditions, empty results and zero-dimensional domains stay distinct; actual-trace eligibility is checked separately.",
              "provenance": metadata, "rows": rows,
              "counts": dict(Counter(row["status"] for row in rows)), "performance_measurements": 0}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"output": str(output), "aliases": metadata["aliases"], "counts": report["counts"]}, indent=2))


if __name__ == "__main__":
    main()
