#!/usr/bin/env python3
"""The binary's short and long help options must be successful CLI requests."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1]).resolve()
build_options = {option["name"]: option["value"] for option in json.loads(
    (binary.parent.parent / "meson-info/intro-buildoptions.json").read_text())}
build_effort = build_options["acacia_default_translation_effort"]


def worker_arm(row):
    polarity = row["requested_polarity"].lower()
    transform = row["translation"] if polarity == "real" else row["transform"]
    return f"{polarity}:{transform}:{row['requested_backend']}"


def expected_effort(value, arm, provider="frozen-graph"):
    entries = value.split(",")
    level = next((entry for entry in entries if "=" not in entry), "high")
    for entry in entries:
        name, equal, override = entry.partition("=")
        if equal and provider == "frozen-graph" and name in (arm, arm + ":" + provider):
            level = override
    return level


with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix="check-help-") as directory:
    cwd = Path(directory)
    for option in ("-h", "--help"):
        for invalid_deadline in (False, True):
            env = os.environ.copy()
            if invalid_deadline:
                env["ACACIA_OUTER_DEADLINE_MONOTONIC"] = "invalid"
            else:
                env.pop("ACACIA_OUTER_DEADLINE_MONOTONIC", None)
            result = subprocess.run([str(binary), option], cwd=cwd, env=env,
                                    capture_output=True, text=True, check=False,
                                    timeout=10)
            assert result.returncode == 0, (option, invalid_deadline, result)
            assert result.stdout.startswith("Usage:"), (option, invalid_deadline, result)
            assert "--arms LIST" in result.stdout, (option, invalid_deadline, result)
            assert "--equivariance-budget F|DURATION|unbounded (default 3s)" in result.stdout
            assert "[scan|grouped] (default: grouped)" in result.stdout
            assert "--spot-fast VAL   use Spot NBA fast path from [off|det]\n" in result.stdout
            assert ("--translation-effort VAL  native translation effort "
                    "[high|medium|low]\n" in result.stdout)
            assert f"build default: {build_effort}\n" in result.stdout
            assert "when given, replaces the build default entirely (implicit level high)" in result.stdout
            assert "build overrides without an active frozen-graph arm are ignored" in result.stdout
            assert "runtime overrides for inactive or unsupported arms are errors" in result.stdout
            assert "comma-separated LEVEL and ARM=LEVEL entries" in result.stdout
            assert "overrides apply only to translation-based providers (frozen-graph)" in result.stdout
            assert "ARM must match a selected translation arm exactly" in result.stdout
            assert "be omitted only if unique" in result.stdout
            assert "--obligation-helper" not in result.stdout
            assert "--obligation-memory-share VAL [equal|alone] (default equal)" in result.stdout
            assert "--parity-memory-share VAL [equal|headroom] (default headroom)" in result.stdout
            assert "builds the tlsf-tools DPA" in result.stdout
            assert "validated offline on a recorded sample against a construction" in result.stdout
            assert ("both:obligation:spot: syntactic obligations, trusted Spot synthesis"
                    in result.stdout)
            assert "membership follows the build default; starts only when eligible" in result.stdout
            assert result.stderr == "", (option, invalid_deadline, result)
            assert list(cwd.iterdir()) == [], (option, invalid_deadline)

    for value in ("alone", "pressure", "unknown", "Headroom", ""):
        result = subprocess.run([str(binary), "--parity-memory-share=" + value],
                                cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 3, result
        assert "--parity-memory-share expects equal or headroom" in result.stderr, result
        assert list(cwd.iterdir()) == []

    for value in ("equal", "headroom"):
        result = subprocess.run([str(binary), "--parity-memory-share", value,
                                 "--arms", "both:parity:spot", "-f", "G o", "-i", "i", "-o", "o"],
                                cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 0, result
        assert list(cwd.iterdir()) == []

    for value in ("", "High", "fast", "high,low", "low,", ",low",
                  "real:small:backward=fast", "real:small:backward="):
        result = subprocess.run([str(binary), "--translation-effort=" + value],
                                cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 3, result
        assert "--translation-effort expects high, medium or low" in result.stderr, result

    for name in ("missing", "real:any:backward", "real:small:backward:spot-eager", ""):
        result = subprocess.run([str(binary), "--translation-effort", name + "=low",
                                 "--arms", "real:small:backward", "-f", "G o", "-i", "i",
                                 "-o", "o"], cwd=cwd, capture_output=True, text=True,
                                check=False, timeout=10)
        assert result.returncode == 3, result
        assert f"unknown translation arm '{name}' in --translation-effort" in result.stderr, result

    # Exact arms use their own construction routes, not the native translator.
    with tempfile.TemporaryDirectory(dir=cwd) as input_directory:
        spec = Path(input_directory) / "effort.tlsf"
        spec.write_text('''INFO { TITLE: "effort" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G(i <-> X o); } }
''')
        for arm in ("both:parity:spot", "both:parity:oxidd", "both:obligation:spot",
                    "both:gr1:oxidd", "both:gr1-lift:oxidd", "both:gr1-real-lift:oxidd",
                    "real:param-lift:oxidd"):
            result = subprocess.run([str(binary), "--arms", arm, "-T", str(spec),
                                     "--translation-effort", arm + "=low"], cwd=cwd,
                                    capture_output=True, text=True, check=False, timeout=10)
            assert result.returncode == 3, result
            assert f"unknown translation arm '{arm}' in --translation-effort" in result.stderr, result

    # Keep the full default portfolio with -T. Parent specs cover losing workers
    # too. A runtime level or override must replace every build-default setting.
    tlsf = cwd / "effort.tlsf"
    tlsf.write_text('INFO { TITLE: "effort" DESCRIPTION: "test" '
                    'SEMANTICS: Mealy TARGET: Mealy }\n'
                    'MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G(i <-> X o); } }\n')
    runtime_values = [None, "high", "medium"]
    for effort in runtime_values:
        with tempfile.TemporaryDirectory(dir=cwd) as capture_directory:
            env = dict(os.environ, ACACIA_PHASE_RECORDS=capture_directory)
            command = [str(binary), "-T", str(tlsf), "--spot-fast", "off", "-K", "4"]
            if effort is not None:
                command += ["--translation-effort", effort]
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                    text=True, check=False, timeout=10)
            assert result.returncode in (0, 1, 2), result
            specs = [row for p in Path(capture_directory).glob("*.jsonl")
                     for line in p.read_text().splitlines()
                     if (row := json.loads(line)).get("event") == "worker_spec"]
            native_translations = [r for r in specs if r["kind"] == "legacy"]
            assert native_translations, specs
            for row in specs:
                expected = "unused"
                if row["kind"] == "legacy":
                    expected = expected_effort(
                        build_effort if effort is None else effort, worker_arm(row), row["provider"])
                assert row["translation_effort"] == expected, specs
            if effort is None:
                selected = build_options["acacia_default_arms"].split(",")
                if selected != [""]:
                    assert {worker_arm(r) for r in native_translations} == {
                        arm for arm in selected if not arm.startswith("both:")}, specs
                # An override alone starts from high and discards build defaults.
                runtime_values.append(worker_arm(native_translations[0]) + "=medium")
    tlsf.unlink()

    # Each arm runs in a forked worker. Check its captured effective option with
    # the default and each explicit effort level. A bounded arm may return UNKNOWN
    # depending on the build; its verdict is independent of effort propagation.
    defaults = {}
    for effort in (None, "high", "medium", "low", "override", "provider"):
        for arm in ("real:small:backward", "unreal:formula:backward",
                    "unreal:automaton:backward"):
            with tempfile.TemporaryDirectory(dir=cwd) as capture_directory:
                env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=capture_directory,
                           ACACIA_PHASE_RECORDS=capture_directory)
                formula = "G(i <-> X(o))" if arm.startswith("real:") else "G(o <-> X(i))"
                command = [str(binary), "--arms", arm, "--spot-fast", "off", "-K", "4",
                           "-f", formula, "-i", "i", "-o", "o"]
                if effort in ("override", "provider"):
                    name = arm + (":frozen-graph" if effort == "provider" else "")
                    command += ["--translation-effort", name + "=low"]
                elif effort is not None:
                    command += ["--translation-effort", effort]
                level = ("low" if effort in ("override", "provider") else
                         expected_effort(build_effort if effort is None else effort, arm))
                result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                        text=True, check=False, timeout=10)
                assert result.returncode in (0, 1, 2), (command, result)
                records = [json.loads(p.read_text())
                           for p in Path(capture_directory).glob("*.json")]
                assert len(records) == 1, (command, records)
                assert records[0]["translation_effort"] == level, records
                if effort is None:
                    defaults[arm] = {k: v for k, v in records[0].items()
                                     if k not in ("worker_pid", "run_id")}
                elif effort == expected_effort(build_effort, arm):
                    assert {k: v for k, v in records[0].items()
                            if k not in ("worker_pid", "run_id")} == defaults[arm], records
                specs = [row for p in Path(capture_directory).glob("*.jsonl")
                         for line in p.read_text().splitlines()
                         if (row := json.loads(line)).get("event") == "worker_spec"]
                assert len(specs) == 1, (command, specs)
                assert specs[0]["translation_effort"] == level, specs
        assert list(cwd.iterdir()) == []

    # Losing checks exhaust K, so every child finishes and writes its capture.
    # This checks mixed levels in one invocation without relying on race timing.
    for arms, formula, key, levels in (
        (["real:small:backward", "real:any:backward:frozen-graph"],
         "G(o <-> X(i))", "translation_pref", ["small", "any"]),
        (["unreal:formula:backward:frozen-graph", "unreal:automaton:backward"],
         "G(i <-> X(o))", "transform", ["formula", "automaton"]),
    ):
        for values, expected in (
            ([arms[0] + "=low"], ["low", "high"]),
            (["medium," + arms[0] + "=low"], ["low", "medium"]),
            ([arms[0] + "=low,medium"], ["low", "medium"]),
            ([arms[0] + "=low", "medium", arms[0] + "=high"], ["high", "medium"]),
        ):
            with tempfile.TemporaryDirectory(dir=cwd) as capture_directory:
                env = dict(os.environ, ACACIA_SPOT_CAPTURE_DIR=capture_directory,
                           ACACIA_PHASE_RECORDS=capture_directory)
                command = [str(binary), "--arms", ",".join(arms), "--spot-fast", "off",
                           "-K", "4", "-f", formula, "-i", "i", "-o", "o"]
                for value in values:
                    command += ["--translation-effort", value]
                result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                        text=True, check=False, timeout=10)
                assert result.returncode in (0, 1, 2), (command, result)
                records = [json.loads(p.read_text())
                           for p in Path(capture_directory).glob("*.json")]
                assert len(records) == 2, (command, records)
                assert {r[key]: r["translation_effort"] for r in records} == dict(
                    zip(levels, expected)), (command, records)
                specs = [row for p in Path(capture_directory).glob("*.jsonl")
                         for line in p.read_text().splitlines()
                         if (row := json.loads(line)).get("event") == "worker_spec"]
                assert [r["translation_effort"] for r in sorted(
                    specs, key=lambda r: r["worker"])] == expected, (command, specs)

    # -F takes a filename, including one that begins with the help spelling.
    result = subprocess.run([str(binary), "-F", "-h-missing-formula"], cwd=cwd,
                            capture_output=True, text=True, check=False, timeout=10)
    assert result.returncode != 0, result
    assert result.stdout == "", result
    assert "unable to open file -h-missing-formula" in result.stderr, result
    assert list(cwd.iterdir()) == []
