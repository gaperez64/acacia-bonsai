import csv
import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "landing-campaign.sh"


def write_csv(path, result):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=["instance", "result", "seconds", "exit"]
        )
        writer.writeheader()
        writer.writerow(
            {"instance": "one.ltl", "result": result, "seconds": 1, "exit": 0}
        )


def run_campaign(tmp_path, baseline_result, candidate_result):
    manifest = tmp_path / "panel.list"
    manifest.write_text("one.ltl\n")
    output = tmp_path / "output"
    output.mkdir()
    write_csv(output / "baseline-demo.csv", baseline_result)
    write_csv(output / "candidate-demo.csv", candidate_result)

    fake_binary = tmp_path / "must-not-run"
    fake_binary.write_text("#!/bin/sh\nexit 99\n")
    fake_binary.chmod(0o755)
    env = os.environ.copy()
    env["ACACIA_OUTER_CGROUP"] = "1"
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


def test_tlsf_corpus_survives_the_scope_re_exec(tmp_path):
    """The campaign re-execs itself inside a systemd scope and rebuilds argv by
    hand, so an option that is parsed but not forwarded is silently dropped and
    the panel falls back to a .ltl route that syntcomp26 does not have."""
    text = SCRIPT.read_text()
    forwarded = text[text.index("scope_command=(") : text.index("exec \"${scope_command[@]}\"")]

    assert "--tlsf-corpus" in forwarded, (
        "--tlsf-corpus is not added to the re-exec argv"
    )


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
