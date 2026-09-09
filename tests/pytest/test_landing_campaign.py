import csv
import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "landing-campaign.sh"


def write_csv(path, result, *, seconds=1):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=["instance", "result", "seconds", "exit"]
        )
        writer.writeheader()
        writer.writerow(
            {"instance": "one.ltl", "result": result, "seconds": seconds, "exit": 0}
        )


def run_campaign(tmp_path, baseline_result, candidate_result, *, outer_cgroup=True,
                 scope_mode="campaign", previous_scope_mode=None):
    manifest = tmp_path / "panel.list"
    manifest.write_text("one.ltl\n")
    output = tmp_path / "output"
    output.mkdir()
    write_csv(output / "baseline-demo.csv", baseline_result)
    write_csv(output / "candidate-demo.csv", candidate_result)
    if previous_scope_mode is not None:
        (output / "meta.txt").write_text(f"scope_mode={previous_scope_mode}\n")

    fake_binary = tmp_path / "must-not-run"
    fake_binary.write_text("#!/bin/sh\nexit 99\n")
    fake_binary.chmod(0o755)
    env = os.environ.copy()
    if outer_cgroup:
        env["ACACIA_OUTER_CGROUP"] = "1"
    else:
        env.pop("ACACIA_OUTER_CGROUP", None)
    result = subprocess.run(
        [
            SCRIPT,
            "--baseline-bin",
            fake_binary,
            "--candidate-bin",
            fake_binary,
            "--suite",
            "demo",
            "--list",
            manifest,
            "--timeout",
            "17",
            "--scope-mode",
            scope_mode,
            "--output",
            output,
        ],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
    )
    return result, output


def test_resume_keeps_complete_csvs(tmp_path):
    result, output = run_campaign(tmp_path, "REALIZABLE", "REALIZABLE")

    assert result.returncode == 0, result.stderr
    assert (output / "status.txt").read_text() == "COMPLETE PASS\n"
    assert (output / "summary.txt").read_text().startswith("GATE PASS\n")
    assert list(csv.DictReader((output / "baseline-demo.csv").open()))[0][
        "result"
    ] == "REALIZABLE"


def test_gate_failure_reaches_status(tmp_path):
    result, output = run_campaign(tmp_path, "REALIZABLE", "TIMEOUT")

    assert result.returncode == 1
    assert (output / "status.txt").read_text() == "COMPLETE FAIL exit=1\n"
    assert (output / "summary.txt").read_text().startswith("GATE FAIL\n")
    assert "GATE FAIL" in (output / "landing-demo.txt").read_text()


def test_native_panel_remeasurement_keeps_tlsf_when_ltl_also_exists(tmp_path):
    suite = tmp_path / "tests" / "suites" / "benchmarks" / "demo"
    suite.mkdir(parents=True)
    manifest = suite / "panel.list"
    manifest.write_text("one.ltl\n")
    (suite / "sources.tsv").write_text("instance\tsource\none.ltl\tcontent.ltl\n")
    (suite / "tlsf-sources.tsv").write_text("instance\ttlsf\none.ltl\tone.tlsf\n")
    ltl_root = tmp_path / "tests" / "ltl"
    ltl_root.mkdir()
    (ltl_root / "content.ltl").write_text("G request\n")
    (ltl_root / "content.part").write_text(".inputs request\n.outputs grant\n")
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    (corpus / "one.tlsf").write_text("INFO {}\nMAIN {}\n")
    output = tmp_path / "output"
    output.mkdir()
    write_csv(output / "baseline-demo.csv", "REALIZABLE", seconds=16)
    write_csv(output / "candidate-demo.csv", "TIMEOUT", seconds=17)
    solver = tmp_path / "record-argv"
    solver.write_text(
        '#!/bin/sh\nprintf "%s\\n" "$@" >> "$ARGS_LOG"\n'
        'echo REALIZABLE\n'
    )
    solver.chmod(0o755)
    argv_log = tmp_path / "argv.txt"
    env = dict(os.environ, ACACIA_OUTER_CGROUP="1", ARGS_LOG=str(argv_log))

    result = subprocess.run(
        [SCRIPT, "--baseline-bin", solver, "--candidate-bin", solver,
         "--suite", "demo", "--list", manifest, "--timeout", "17",
         "--tlsf-corpus", corpus, "--output", output],
        cwd=ROOT, env=env, capture_output=True, text=True,
    )

    assert result.returncode == 0, result.stderr
    assert argv_log.read_text().splitlines() == [
        "-T", str(corpus / "one.tlsf"), "-T", str(corpus / "one.tlsf"),
    ]
    assert "GATE PASS" in (output / "landing-demo.txt").read_text()


def test_tlsf_corpus_survives_the_scope_re_exec(tmp_path):
    """The campaign re-execs itself inside a systemd scope and rebuilds argv by
    hand, so an option that is parsed but not forwarded is silently dropped and
    the panel falls back to a .ltl route that syntcomp26 does not have."""
    text = SCRIPT.read_text()
    forwarded = text[text.index("scope_command=(") : text.index("exec \"${scope_command[@]}\"")]

    assert "--tlsf-corpus" in forwarded, (
        "--tlsf-corpus is not added to the re-exec argv"
    )


def test_parent_sweeps_once_after_scoped_re_exec(tmp_path, monkeypatch):
    """Simulate a scope surviving the re-exec without contacting systemd."""
    monkeypatch.delenv("ACACIA_CAMPAIGN_SCOPE_GUARD")
    monkeypatch.delenv("ACACIA_ALLOW_STRAY_SCOPES", raising=False)
    monkeypatch.setenv("SCOPE_TEST_DIR", str(tmp_path))
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    monkeypatch.setenv("PATH", f"{bin_dir}:{os.environ['PATH']}")
    fake_systemd_run = bin_dir / "systemd-run"
    fake_systemd_run.write_text('''#!/usr/bin/env bash
set -eu
printf '%s\\n' "$@" > "$SCOPE_TEST_DIR/scope-argv"
touch "$SCOPE_TEST_DIR/running"
while [[ $1 != env ]]; do shift; done
"$@"
''')
    fake_systemd_run.chmod(0o755)
    fake_systemctl = bin_dir / "systemctl"
    fake_systemctl.write_text('''#!/usr/bin/env bash
set -eu
printf '%s\\n' "$2" >> "$SCOPE_TEST_DIR/systemctl-calls"
case $2 in
  list-units)
    if [[ -f $SCOPE_TEST_DIR/running ]]; then
      echo 'acacia-landing-campaign-123.scope loaded active running Campaign'
    fi
    ;;
  stop)
    rm "$SCOPE_TEST_DIR/running"
    ;;
  show)
    printf 'LoadState=not-found\\nActiveState=inactive\\nSubState=dead\\n'
    ;;
  *) exit 99 ;;
esac
''')
    fake_systemctl.chmod(0o755)

    result, output = run_campaign(tmp_path, "REALIZABLE", "REALIZABLE", outer_cgroup=False)

    assert result.returncode == 0, result.stderr
    assert (output / "status.txt").read_text() == "COMPLETE PASS\n"
    assert (tmp_path / "systemctl-calls").read_text().splitlines() == [
        "list-units", "list-units", "stop", "show",
    ]
    assert not (tmp_path / "running").exists()
    assert "surviving running scope acacia-landing-campaign-123.scope" in result.stderr
    assert "--unit=acacia-landing-campaign-" in (tmp_path / "scope-argv").read_text()


def test_a_suite_with_a_tlsf_map_takes_the_tlsf_route(tmp_path):
    """syntcomp26's panel has no .ltl pair for any of its 180 rows, so the
    campaign has to hand run-subset.py the TLSF map rather than a source map."""
    manifest = tmp_path / "panel.list"
    manifest.write_text("one.ltl\n")
    (tmp_path / "tlsf-sources.tsv").write_text("instance\ttlsf\none.ltl\tone.tlsf\n")
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    (corpus / "one.tlsf").write_text("INFO {}\n")
    output = tmp_path / "output"
    output.mkdir()

    # A binary that records how it was invoked and reports a verdict, so the
    # route is observable without a real solve.
    fake_binary = tmp_path / "record-argv"
    fake_binary.write_text(
        "#!/bin/sh\n"
        f'printf "%s\\n" "$@" >> "{tmp_path}/argv.txt"\n'
        "echo REALIZABLE\nexit 0\n"
    )
    fake_binary.chmod(0o755)

    env = os.environ.copy()
    env["ACACIA_OUTER_CGROUP"] = "1"
    subprocess.run(
        [SCRIPT, "--baseline-bin", fake_binary, "--candidate-bin", fake_binary,
         "--suite", "demo", "--list", manifest, "--timeout", "17",
         "--tlsf-corpus", corpus, "--output", output],
        cwd=ROOT, env=env, capture_output=True, text=True,
    )

    argv = (tmp_path / "argv.txt").read_text().splitlines()
    assert "-T" in argv, f"the TLSF route was not taken: {argv}"
    assert str(corpus / "one.tlsf") in argv


def test_cannot_resume_with_a_different_scope_mode(tmp_path):
    result, _ = run_campaign(
        tmp_path, "REALIZABLE", "REALIZABLE", outer_cgroup=False,
        scope_mode="instance", previous_scope_mode="campaign",
    )
    assert result.returncode == 2
    assert "cannot resume with a different scope mode" in result.stderr


def test_instance_resume_requires_scope_provenance(tmp_path):
    result, _ = run_campaign(
        tmp_path, "REALIZABLE", "REALIZABLE", outer_cgroup=False,
        scope_mode="instance",
    )
    assert result.returncode == 2
    assert "without scope provenance" in result.stderr


def test_instance_resume_keeps_matching_scope_results(tmp_path):
    result, output = run_campaign(
        tmp_path, "REALIZABLE", "REALIZABLE", outer_cgroup=False,
        scope_mode="instance", previous_scope_mode="instance",
    )
    assert result.returncode == 0, result.stderr
    assert "scope_mode=instance\n" in (output / "meta.txt").read_text()


def test_instance_scopes_survive_a_solver_oom(tmp_path, monkeypatch):
    """An OOM belongs to one solver invocation; the driver must finish both panels."""
    monkeypatch.delenv("ACACIA_OUTER_CGROUP", raising=False)
    monkeypatch.setenv("SCOPE_TEST_DIR", str(tmp_path))
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    monkeypatch.setenv("PATH", f"{bin_dir}:{os.environ['PATH']}")
    fake_systemd_run = bin_dir / "systemd-run"
    fake_systemd_run.write_text('''#!/usr/bin/env bash
set -eu
printf '%s\\n' "$@" >> "$SCOPE_TEST_DIR/scope-argv"
while [[ $1 == --* ]]; do shift; done
exec "$@"
''')
    fake_systemd_run.chmod(0o755)
    fake_systemctl = bin_dir / "systemctl"
    fake_systemctl.write_text('''#!/usr/bin/env bash
case $2 in
  show) printf 'Result=oom-kill\\nMemoryPeak=8589934592\\nLoadState=not-found\\nActiveState=inactive\\nSubState=dead\\n' ;;
  stop|list-units) ;;
  *) exit 99 ;;
esac
''')
    fake_systemctl.chmod(0o755)
    fake_binary = bin_dir / "solver"
    fake_binary.write_text('''#!/usr/bin/env bash
case $2 in
  */one.tlsf) exit 137 ;;
  *) echo REALIZABLE ;;
esac
''')
    fake_binary.chmod(0o755)
    listing = tmp_path / "panel.list"
    listing.write_text("one.ltl\ntwo.ltl\n")
    (tmp_path / "tlsf-sources.tsv").write_text(
        "instance\ttlsf\none.ltl\tone.tlsf\ntwo.ltl\ttwo.tlsf\n"
    )
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    for name in ["one", "two"]:
        (corpus / f"{name}.tlsf").write_text("INFO {}\n")
    output = tmp_path / "output"
    result = subprocess.run(
        [SCRIPT, "--baseline-bin", fake_binary, "--candidate-bin", fake_binary,
         "--suite", "demo", "--list", listing, "--timeout", "17",
         "--tlsf-corpus", corpus, "--scope-mode", "instance", "--output", output],
        cwd=ROOT, capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr
    for side in ["baseline", "candidate"]:
        rows = list(csv.DictReader((output / f"{side}-demo.csv").open()))
        assert [row["result"] for row in rows] == ["RESOURCE_LIMIT", "REALIZABLE"]
    scope_args = (tmp_path / "scope-argv").read_text().splitlines()
    units = [arg for arg in scope_args if arg.startswith("--unit=")]
    assert len(units) == len(set(units)) == 4
    assert all(unit.startswith("--unit=acacia-subset-") for unit in units)
    assert scope_args.count("--property=MemoryMax=8G") == 4
    assert scope_args.count("--property=MemorySwapMax=0") == 4
