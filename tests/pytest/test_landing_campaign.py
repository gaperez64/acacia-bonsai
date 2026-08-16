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
