#!/usr/bin/env python3
"""Require complete delivery evidence on solved and UNKNOWN exact-arm exits."""

from pathlib import Path
import os
import runpy
import sys
import tempfile


support = runpy.run_path(str(Path(__file__).with_name("check-attribution.py")))
audit = runpy.run_path(str(Path(__file__).resolve().parents[1]
                             / "benchmarking/attribution-completeness.py"))["audit_row"]


def main():
    binary, scratch, acknowledgement = (Path(value).resolve() for value in sys.argv[1:4])
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        root = Path(temporary)
        source = root / "source.tlsf"
        source.write_text('''INFO { TITLE: "records" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G (i -> o); } }
''')
        for family in ("obligation:spot", "parity:spot", "parity:oxidd"):
            arm = "both:" + family
            cases = ("real-on", "real-off", "unknown")
            if family == "parity:spot":
                cases += ("synthesis-on", "synthesis-off", "unknown-not-directory")
                if os.geteuid() != 0:
                    cases += ("unknown-read-only", "synthesis-no-staging")
            for case in cases:
                directory = root / (family + "-" + case)
                directory.mkdir()
                options = ["--arms", arm, "--r-prepass", "off" if case.endswith("-off") else "on",
                           "--equivariance", "off" if case.endswith("-off") else "on"]
                args = ["-T", str(source), *options]
                if case.startswith("synthesis-"):
                    args += ["-s", str(root / (case + ".aag"))]
                if case.startswith("unknown"):
                    if family == "obligation:spot":
                        args = ["-f", "G F (i <-> o)", "-i", "i", "-o", "o", *options]
                    elif family == "parity:spot":
                        parent = root / ("not-directory" if case.endswith("not-directory")
                                         else "read-only" if case.endswith("read-only") else "missing")
                        if case.endswith("not-directory"):
                            parent.write_text("not a directory\n")
                        elif case.endswith("read-only"):
                            parent.mkdir()
                            parent.chmod(0o500)
                        args += ["-s", str(parent / "controller.aag")]
                    else:
                        args = ["-f", "1", "-i", "i", "-o", "o", *options]
                if case == "synthesis-no-staging":
                    parent = root / "existing-read-only"
                    parent.mkdir()
                    target = parent / "controller.aag"
                    target.write_text("previous controller\n")
                    parent.chmod(0o500)
                    args[-1] = str(target)
                try:
                    run = support["execute"](binary, args, directory, {
                        "LD_PRELOAD": str(acknowledgement), "ACACIA_TEST_RECORD_ACK": "1"})
                finally:
                    if case in {"unknown-read-only", "synthesis-no-staging"}:
                        parent.chmod(0o700)
                unknown = case.startswith("unknown")
                assert run.returncode == (2 if unknown else 0), run
                if unknown:
                    assert not run.stdout, run
                if case == "synthesis-no-staging":
                    assert target.read_text().startswith("aag "), target.read_text()
                assert not list(root.glob("**/.acacia-private-*")), list(root.iterdir())
                records = support["rows"](directory)
                support["complete_records"](records, expected_winners=0 if unknown else 1)
                if family == "parity:spot":
                    attempts = [r for r in records if r.get("event") == "parity_reduction_attempt"]
                    assert bool(attempts) == (unknown or case.startswith("synthesis-")), records
                    assert all(r["worker_pid"] == r["emitter_pid"] for r in attempts), attempts
                terminal, = [r for r in records if r.get("event") == "parent_terminal"]
                summary, = [r for r in records if r.get("phase") == "record_summary"
                            and r["emitter_pid"] == terminal["worker_pid"]]
                assert summary["arm"] == arm, summary
                assert summary["dropped_records"] == terminal["dropped_records"] == 0, records
                if unknown:
                    expected = {"obligation:spot": "non_obligation",
                                "parity:spot": "controller_export_failed",
                                "parity:oxidd": "dpa_missing_ast"}[family]
                    final, = [r for r in records if r.get("event") == "terminal_result"]
                    assert final["reason"] == expected, final
                result = audit({"result": "UNKNOWN" if unknown else "REALIZABLE",
                                "flags": " ".join(options)}, directory)
                assert result["complete"], (family, case, result["issues"])
    print("Exact-arm solved and UNKNOWN exits have one summary and complete delivery evidence")


if __name__ == "__main__":
    main()
