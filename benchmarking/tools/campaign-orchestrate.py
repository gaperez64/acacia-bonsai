#!/usr/bin/env python3
"""Run frozen P4 standalone arms and mixed-size races, one scoped leg at a time.

The comparison JSON is frozen before timing.  Every resume checks its bytes,
binary bytes, corpus bytes, runner bytes and wrapper text.  Native legs use
run-syntcomp26-coverage.py; legacy converted-pair legs use run-subset.py. The
orchestrator holds the shared timed-run marker across the serial campaign.
Each series declares portfolio_size: null for legacy tools, 1 for an arm leg,
and its arm count for a race. A default-arm race pins Meson's build-options
file and omits --arms; this supports a five-arm E5 and six-arm final preset in
one comparison.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import HostSampler, timed_run_marker  # noqa: E402

NATIVE = ROOT / "benchmarking/run-syntcomp26-coverage.py"
SUBSET = ROOT / "benchmarking/run-subset.py"
SAFE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\Z")
HEX = re.compile(r"[0-9a-f]{64}\Z")


def load_coverage():
    spec = importlib.util.spec_from_file_location("coverage_p4_orchestrate", NATIVE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


coverage = load_coverage()


class CampaignError(ValueError):
    pass


def native_flags(entry):
    """Return the flags actually passed to a native series."""
    arms = "" if entry["use_default_arms"] else shlex.join(
        ["--arms", ",".join(entry["arms"])])
    return " ".join(part for part in (arms, entry["flags"]) if part)


def sha256_file(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def directory_digest(path):
    digest = hashlib.sha256()
    for item in sorted(path.rglob("*")):
        if not item.is_file():
            continue
        digest.update(json.dumps([str(item.relative_to(path)), sha256_file(item)],
                                 separators=(",", ":")).encode() + b"\n")
    return digest.hexdigest()


def preserved_runtime_identity(runtime, sums_file):
    """Verify the soname files against the preserved 2.15.1.dev goodset."""
    if runtime is None or not runtime.is_dir():
        raise CampaignError("pre-2.16 series needs an existing runtime_lib_dir")
    if sums_file is None or not sums_file.is_file():
        raise CampaignError("pre-2.16 series needs an existing runtime_sha256s")
    expected = {}
    for line in sums_file.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        expected[pathlib.Path(name).name] = digest
    libraries = {}
    for soname in ("libspot.so.0", "libbddx.so.0"):
        path = runtime / soname
        if not path.is_file():
            raise CampaignError(f"preserved runtime missing {path}")
        target = path.resolve()
        digest = sha256_file(path)
        if digest != expected.get(target.name):
            raise CampaignError(f"preserved runtime SHA-256 mismatch: {path}")
        libraries[soname] = digest
    return {"sha256s_path": str(sums_file),
            "sha256s_sha256": sha256_file(sums_file), "libraries": libraries,
            "verification": ("soname bytes match the preserved SHA256SUMS; "
                             "checksum-file authority is unverified")}


def resolve(base, value):
    path = pathlib.Path(value)
    return (path if path.is_absolute() else base / path).resolve()


def corpus_digest(instances, mapping, corpus):
    digest = hashlib.sha256()
    for name in instances:
        if name not in mapping:
            raise CampaignError(f"{name}: missing from TLSF map")
        path = resolve(corpus, mapping[name])
        if not path.is_file() or not path.is_relative_to(corpus):
            raise CampaignError(f"{name}: TLSF path outside or missing from corpus: {path}")
        digest.update(json.dumps([name, str(path.relative_to(corpus)), sha256_file(path)],
                                 separators=(",", ":")).encode() + b"\n")
    return digest.hexdigest()


def verify_observation_labels(entry, rows, *, tlsf_map, corpus, status_exceptions):
    """Check row source labels against the campaign's frozen input content."""
    native = entry["kind"] in {"arm", "race"}
    try:
        mapping = coverage.read_tlsf_map(pathlib.Path(tlsf_map))
        exceptions = (coverage.read_status_exceptions(
            pathlib.Path(status_exceptions), pathlib.Path(corpus), required=True)
            if native else {})
    except coverage.CoverageError as error:
        raise CampaignError(str(error)) from error
    for row in rows:
        name = row["instance"]
        if name not in mapping:
            raise CampaignError(f"{name}: absent from frozen TLSF map")
        tlsf = mapping[name]
        expected_file = (pathlib.Path(tlsf).name if native or entry["tool"] == "acacia"
                         else pathlib.Path(name).name)
        try:
            source = ("exception" if tlsf in exceptions else
                      "status" if coverage.expected_verdict(pathlib.Path(corpus) / tlsf)
                      is not None else "none") if native else "none"
        except coverage.CoverageError as error:
            raise CampaignError(str(error)) from error
        if row["tlsf_file"] != expected_file or row["expectation_source"] != source:
            raise CampaignError(f"{name}: observation TLSF or expected-status label "
                                "differs from frozen inputs")


def wrapper_text(binary, runtime):
    return ("#!/bin/sh\n"
            f"export LD_LIBRARY_PATH={shlex.quote(str(runtime))}:\"${{LD_LIBRARY_PATH-}}\"\n"
            f"exec {shlex.quote(str(binary))} \"$@\"\n")


def read_comparison(path, cap, *, selected=None, subset_list=None):
    if type(cap) is not int or cap <= 0:
        raise CampaignError("campaign cap must be a positive integer")
    path = path.resolve()
    spec = json.loads(path.read_text())
    if spec.get("schema") != 1 or not isinstance(spec.get("series"), list):
        raise CampaignError("comparison needs schema 1 and a series list")
    if spec.get("memory_max") != "8G" or spec.get("memory_swap_max") != "0":
        raise CampaignError("P4 requires MemoryMax=8G and MemorySwapMax=0")
    base = path.parent
    paths = {key: resolve(base, spec[key]) for key in
             ("list", "tlsf_map", "tlsf_corpus", "status_exceptions")}
    runtime_sha256s = (resolve(base, spec["runtime_sha256s"])
                      if spec.get("runtime_sha256s") else None)
    if runtime_sha256s is not None:
        paths["runtime_sha256s"] = runtime_sha256s
    if any(not value.exists() for value in paths.values()):
        raise CampaignError("a comparison input path does not exist")
    original_list = paths["list"]
    full_instances = coverage.read_instance_list(original_list)
    if not full_instances or len(set(full_instances)) != len(full_instances):
        raise CampaignError("frozen list is empty or contains duplicate instances")
    if subset_list is not None:
        subset_list = pathlib.Path(subset_list).resolve()
        if not subset_list.is_file():
            raise CampaignError(f"subset list missing: {subset_list}")
        paths["list"] = subset_list
    instances = coverage.read_instance_list(paths["list"])
    if not instances or len(set(instances)) != len(instances):
        raise CampaignError("comparison list is empty or contains duplicate instances")
    if not set(instances) <= set(full_instances):
        raise CampaignError("subset list contains an instance outside the frozen list")
    mapping = coverage.read_tlsf_map(paths["tlsf_map"])
    corpus_sha = corpus_digest(instances, mapping, paths["tlsf_corpus"])
    frozen = {"schema": 1, "comparison": str(path), "comparison_sha256": sha256_file(path),
              "frozen_list": str(original_list),
              "frozen_list_sha256": sha256_file(original_list),
              "cap_s": cap, "inputs": {key: {"path": str(value), "sha256": sha256_file(value)}
                                      for key, value in paths.items() if value.is_file()},
              "corpus": str(paths["tlsf_corpus"]), "corpus_sha256": corpus_sha,
              "instances": len(instances), "memory_max": "8G", "memory_swap_max": "0",
              "verification": ("input paths and bytes are checked on resume and execution; "
                               "comparison authority, source revisions, solver outcomes and "
                               "host sample accuracy are unverified"),
              "series": {}}
    names = set()
    for record in spec["series"]:
        label = record.get("label", "")
        if selected is not None and label not in selected:
            continue
        kind = record.get("kind", "")
        if not SAFE.fullmatch(label) or label in names:
            raise CampaignError(f"invalid or duplicate series label {label!r}")
        names.add(label)
        if kind not in {"arm", "race", "legacy"}:
            raise CampaignError(f"{label}: kind must be arm, race or legacy")
        binary = resolve(base, record["binary"])
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise CampaignError(f"{label}: executable missing: {binary}")
        binary_sha = sha256_file(binary)
        if not HEX.fullmatch(record.get("binary_sha256", "")) or \
                binary_sha != record["binary_sha256"]:
            raise CampaignError(f"{label}: frozen binary SHA-256 mismatch")
        runtime = resolve(base, record["runtime_lib_dir"]) if record.get("runtime_lib_dir") else None
        if not isinstance(record.get("pre_spot_216", False), bool):
            raise CampaignError(f"{label}: pre_spot_216 must be a boolean")
        pre_216 = record.get("pre_spot_216", False) or kind == "legacy" and \
            record.get("tool") == "acacia1x"
        runtime_identity = (preserved_runtime_identity(runtime, runtime_sha256s)
                            if pre_216 or runtime else None)
        arms = record.get("arms", [])
        if not isinstance(arms, list):
            raise CampaignError(f"{label}: arms must be an array")
        if "portfolio_size" not in record:
            raise CampaignError(f"{label}: portfolio_size must be declared")
        size = record["portfolio_size"]
        default_arms = record.get("use_default_arms", False)
        if not isinstance(default_arms, bool):
            raise CampaignError(f"{label}: use_default_arms must be a boolean")
        flags = record.get("flags", "")
        if not isinstance(flags, str):
            raise CampaignError(f"{label}: flags must be a string")
        try:
            flag_words = shlex.split(flags)
        except ValueError as error:
            raise CampaignError(f"{label}: invalid flags: {error}") from error
        if kind != "legacy" and any(word == "--arms" or word.startswith("--arms=")
                                    for word in flag_words):
            raise CampaignError(f"{label}: put --arms in the arms array")
        if kind != "legacy" and any(word.startswith(("-r", "-u")) and
                                    not word.startswith("--") for word in flag_words):
            raise CampaignError(f"{label}: flags cannot change the launched arms")
        if kind != "legacy" and (not all(isinstance(a, str) and a for a in arms) or
                                  len(set(arms)) != len(arms)):
            raise CampaignError(f"{label}: invalid arms")
        if kind == "arm" and len(arms) != 1:
            raise CampaignError(f"{label}: standalone leg needs one arm")
        if kind == "race":
            if len(arms) < 2:
                raise CampaignError(f"{label}: race needs distinct arms")
        if kind == "legacy":
            if size is not None:
                raise CampaignError(f"{label}: legacy tool needs portfolio_size null")
        elif type(size) is not int or size != len(arms):
            raise CampaignError(f"{label}: portfolio_size must match launched arms")
        if default_arms and kind != "race":
            raise CampaignError(f"{label}: only a race can use build default arms")
        if kind == "legacy" and arms:
            raise CampaignError(f"{label}: legacy tool does not accept arms")
        build_options = None
        if default_arms:
            if not record.get("build_options"):
                raise CampaignError(f"{label}: build_options required for default arms")
            build_options = resolve(base, record["build_options"])
            options = json.loads(build_options.read_text())
            matches = [item["value"] for item in options
                       if item.get("name") == "acacia_default_arms"]
            if len(matches) != 1 or not isinstance(matches[0], str) or \
                    matches[0].split(",") != arms:
                raise CampaignError(f"{label}: build default arms differ from declared arms")
        tool = record.get("tool", "acacia")
        if kind == "legacy" and tool not in {"ltlsynt", "acacia1x", "acacia"}:
            raise CampaignError(f"{label}: unsupported legacy tool {tool}")
        if kind != "legacy" and tool != "acacia":
            raise CampaignError(f"{label}: native series require Acacia")
        if kind == "legacy" and tool in {"ltlsynt", "acacia1x"}:
            if "instances_dir" not in record or "syfco_failures" not in record:
                raise CampaignError(f"{label}: converted tool needs frozen pairs and failures")
            if tool == "ltlsynt" and "semantics_map" not in record:
                raise CampaignError(f"{label}: ltlsynt converted pairs need a semantics_map")
        runner = SUBSET if kind == "legacy" else NATIVE
        entry = {
            "label": label, "kind": kind, "tool": tool, "binary": str(binary),
            "binary_sha256": binary_sha, "source_revision": record["source_revision"],
            "preset": record.get("preset", ""), "flags": flags,
            "arms": arms, "portfolio_size": size, "use_default_arms": default_arms,
            "build_options": str(build_options) if build_options else "",
            "build_options_sha256": sha256_file(build_options) if build_options else "",
            "runtime_lib_dir": str(runtime) if runtime else "",
            "runtime_lib_sha256": directory_digest(runtime) if runtime else "",
            "pre_spot_216": pre_216, "runtime_identity": runtime_identity,
            "wrapper_sha256": (hashlib.sha256(wrapper_text(binary, runtime).encode()).hexdigest()
                               if runtime else ""),
            "runner": str(runner), "runner_sha256": sha256_file(runner),
            "route_records": bool(record.get("route_records", False)),
            "source_revision_verification": "unverified declaration from comparison JSON",
            "pre_spot_216_verification": "unverified declaration; runtime bytes verified when enabled",
            "observation_verification": "runner output is checked against cap, panel and treatment; solver result is unverified",
        }
        if kind == "legacy" and tool in {"ltlsynt", "acacia1x"}:
            for key in ("instances_dir", "syfco_failures"):
                item = resolve(base, record[key])
                if not item.exists():
                    raise CampaignError(f"{label}: {key} missing: {item}")
                entry[key] = str(item)
                entry[f"{key}_sha256"] = (sha256_file(item) if item.is_file()
                                           else directory_digest(item))
            failures = set(coverage.read_instance_list(pathlib.Path(entry["syfco_failures"])))
            pairs = {}
            pair_root = pathlib.Path(entry["instances_dir"])
            for name in instances:
                if name in failures:
                    continue
                pairs[name] = {}
                input_path = (pair_root / name).resolve()
                if not input_path.is_relative_to(pair_root):
                    raise CampaignError(f"{label}: converted input outside pair directory: {name}")
                part_path = input_path.with_suffix(".part")
                for role, pair in (("input", input_path), ("part", part_path)):
                    if not pair.is_file():
                        raise CampaignError(f"{label}: converted pair missing: {pair}")
                    pairs[name][role] = {"path": str(pair), "sha256": sha256_file(pair)}
            entry["converted_pairs"] = pairs
            entry["conversion_verification"] = (
                "converted pair, SyFCo failure list and semantics map bytes are pinned; "
                "their origin and semantic correctness are unverified")
            if tool == "ltlsynt":
                semantics_path = resolve(base, record["semantics_map"])
                if not semantics_path.is_file():
                    raise CampaignError(f"{label}: semantics_map missing: {semantics_path}")
                with semantics_path.open(newline="") as stream:
                    reader = csv.DictReader(stream, delimiter="\t")
                    if reader.fieldnames != ["instance", "semantics"]:
                        raise CampaignError(f"{label}: invalid semantics_map header")
                    semantics = {}
                    for row in reader:
                        name, model = row["instance"], row["semantics"]
                        if not name or name in semantics or model not in {"Mealy", "Moore"}:
                            raise CampaignError(f"{label}: invalid semantics_map row {name!r}")
                        semantics[name] = model
                if set(instances) - failures - set(semantics):
                    raise CampaignError(f"{label}: semantics_map lacks a converted input")
                entry["semantics_map"] = str(semantics_path)
                entry["semantics_map_sha256"] = sha256_file(semantics_path)
        if kind == "legacy" and tool != "acacia":
            syfco_name = record.get("syfco", "syfco")
            syfco = shutil.which(syfco_name)
            if syfco is None:
                raise CampaignError(f"{label}: SyFCo executable missing: {syfco_name}")
            syfco = pathlib.Path(syfco).resolve()
            entry["syfco"] = str(syfco)
            entry["syfco_sha256"] = sha256_file(syfco)
        frozen["series"][label] = entry
    if "portfolio_size" in spec:
        raise CampaignError("portfolio_size belongs on each series")
    if selected is not None and names != set(selected):
        raise CampaignError(f"selected series absent from comparison: {sorted(set(selected) - names)}")
    if not names:
        raise CampaignError("no series selected")
    return frozen, paths


def command(entry, paths, cap, out, resume):
    label, kind = entry["label"], entry["kind"]
    output = out / f"{label}-{cap}s.tsv"
    binary = out / "wrappers" / f"{label}.sh" if entry["runtime_lib_dir"] else pathlib.Path(entry["binary"])
    cmd = [sys.executable, "-s", entry["runner"], "--bin", str(binary)]
    if entry["runtime_lib_dir"]:
        cmd += ["--binary-identity", entry["binary"]]
    cmd += ["--list", str(paths["list"]), "--memory-max", "8G", "--memory-swap-max", "0"]
    if kind == "legacy":
        cmd += ["--tool", entry["tool"], "--timeout", str(cap), "--systemd-scope",
                "--raw-tsv", str(output), "--solver-label", label,
                "--revision", entry["source_revision"], "--flags", entry["flags"]]
        if entry.get("syfco"):
            cmd += ["--syfco", entry["syfco"]]
        if entry.get("instances_dir"):
            cmd += ["--instances-dir", entry["instances_dir"],
                    "--syfco-failures", entry["syfco_failures"]]
            if entry.get("semantics_map"):
                cmd += ["--semantics-map", entry["semantics_map"]]
        else:
            cmd += ["--tlsf-map", str(paths["tlsf_map"]),
                    "--tlsf-corpus", str(paths["tlsf_corpus"]),
                    "--syfco-cache", str(out / "syfco-cache")]
    else:
        flags = native_flags(entry)
        cmd += ["--solver-label", label, "--tlsf-map", str(paths["tlsf_map"]),
                "--tlsf-corpus", str(paths["tlsf_corpus"]),
                "--status-exceptions", str(paths["status_exceptions"]),
                "--caps", str(cap), "--conflict-policy", "collect", "--flags", flags,
                "--acacia-sha", entry["source_revision"], "--preset", entry["preset"],
                "--phase-records-dir", str(out / "phase-records"), "--output", str(output)]
        if entry["route_records"]:
            cmd += ["--route-records", str(out / "route-records")]
    if resume:
        cmd += ["--resume"]
    return cmd, output


def prepare(comparison, cap, out, *, resume=False, dry_run=False, selected=None,
            subset_list=None):
    frozen, paths = read_comparison(comparison, cap, selected=selected,
                                    subset_list=subset_list)
    manifest = out / "manifest.json"
    content = json.dumps(frozen, indent=2, sort_keys=True) + "\n"
    if manifest.exists():
        if not resume or manifest.read_text() != content:
            raise CampaignError("existing manifest differs, or --resume was omitted")
    elif resume and out.exists() and any(out.iterdir()):
        raise CampaignError("output has files but no manifest; cannot resume")
    commands = []
    for entry in frozen["series"].values():
        output = out / f"{entry['label']}-{cap}s.tsv"
        if output.exists() and not resume:
            raise CampaignError(f"{output}: exists without --resume")
        commands.append(command(entry, paths, cap, out, output.exists()))
    if dry_run:
        return frozen, commands
    out.mkdir(parents=True, exist_ok=True)
    (out / "tmp").mkdir(exist_ok=True)
    (out / "wrappers").mkdir(exist_ok=True)
    for entry in frozen["series"].values():
        if not entry["runtime_lib_dir"]:
            continue
        wrapper = out / "wrappers" / f"{entry['label']}.sh"
        script = wrapper_text(entry["binary"], entry["runtime_lib_dir"])
        if wrapper.exists() and wrapper.read_text() != script:
            raise CampaignError(f"{wrapper}: frozen runtime wrapper changed")
        if not wrapper.exists():
            wrapper.write_text(script)
            wrapper.chmod(0o755)
    if not manifest.exists():
        manifest.write_text(content)
    return frozen, commands


def execute(frozen, commands, out, *, marker_dir, invoke=subprocess.run):
    def check_frozen_inputs():
        if "comparison" not in frozen:
            return
        current, _ = read_comparison(
            pathlib.Path(frozen["comparison"]), frozen["cap_s"],
            selected=list(frozen["series"]),
            subset_list=frozen["inputs"]["list"]["path"])
        if current != frozen:
            raise CampaignError("campaign inputs changed since manifest preparation")

    check_frozen_inputs()
    environment = os.environ.copy()
    environment["TMPDIR"] = str((out / "tmp").resolve())
    with timed_run_marker(marker_dir, f"P4 campaign {out.resolve()}", announce=print), \
            HostSampler(out / "host-samples.tsv") as sampler:
        for entry, (cmd, output) in zip(frozen["series"].values(), commands):
            sampler.set_leg(entry["label"], output)
            sampler.sample_once()
            print(f"running {entry['label']} at {frozen['cap_s']} s", flush=True)
            with (out / f"{entry['label']}-{frozen['cap_s']}s.log").open("a") as log:
                result = invoke(cmd, cwd=ROOT, env=environment, stdout=log,
                                stderr=subprocess.STDOUT)
            sampler.sample_once()
            if result.returncode:
                raise CampaignError(f"{entry['label']}: runner exited {result.returncode}; "
                                    f"inspect its log and resume after adjudication")
            rows = coverage.load_output(output)
            if len(rows) != frozen["instances"] or \
                    {row["cap_s"] for row in rows} != {str(frozen["cap_s"])}:
                raise CampaignError(f"{output}: incomplete leg after runner returned success")
            if "inputs" in frozen:
                expected_panel = set(coverage.read_instance_list(
                    pathlib.Path(frozen["inputs"]["list"]["path"])))
                native = entry["kind"] in {"arm", "race"}
                expected_flags = native_flags(entry) if native else entry["flags"]
                expected_preset = entry["preset"] if native else entry["tool"]
                if len({row["instance"] for row in rows}) != len(rows) or \
                        {row["instance"] for row in rows} != expected_panel:
                    raise CampaignError(f"{output}: observation panel differs from selected list")
                if any(row["solver_label"] != entry["label"] or
                       row["binary_sha256"] != entry["binary_sha256"] or
                       row["acacia_sha"] != entry["source_revision"] or
                       row["flags"] != expected_flags or row["preset"] != expected_preset or
                       row["memory_max"] != frozen["memory_max"] or
                       row["memory_swap_max"] != frozen["memory_swap_max"]
                       for row in rows):
                    raise CampaignError(f"{output}: observation identity differs from manifest")
                verify_observation_labels(
                    entry, rows, tlsf_map=frozen["inputs"]["tlsf_map"]["path"],
                    corpus=frozen["corpus"],
                    status_exceptions=frozen["inputs"]["status_exceptions"]["path"])
            check_frozen_inputs()
    print("all frozen series complete")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison", required=True, type=pathlib.Path)
    parser.add_argument("--cap", required=True, type=int)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--series", action="append", help="select named series; repeatable")
    parser.add_argument("--subset-list", type=pathlib.Path,
                        help="frozen-list subset for deadline reruns or validation samples")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="validate and print commands only")
    parser.add_argument("--marker-dir", required=True, type=pathlib.Path,
                        help="shared timed-run marker directory")
    args = parser.parse_args(argv)
    if args.cap <= 0:
        parser.error("--cap must be positive")
    try:
        frozen, commands = prepare(args.comparison, args.cap, args.out,
                                   resume=args.resume, dry_run=args.dry_run,
                                   selected=args.series, subset_list=args.subset_list)
        if args.dry_run:
            print(json.dumps(frozen, indent=2, sort_keys=True))
            for cmd, _ in commands:
                print(shlex.join(cmd))
        else:
            execute(frozen, commands, args.out, marker_dir=args.marker_dir)
    except (CampaignError, coverage.CoverageError, OSError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
