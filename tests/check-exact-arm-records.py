#!/usr/bin/env python3
"""Require complete delivery evidence on solved and UNKNOWN exact-arm exits."""

from pathlib import Path
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
            for case in ("real-on", "real-off", "unknown"):
                directory = root / (family + "-" + case)
                directory.mkdir()
                options = ["--arms", arm, "--r-prepass", "off" if case == "real-off" else "on",
                           "--equivariance", "off" if case == "real-off" else "on"]
                args = ["-T", str(source), *options]
                if case == "unknown":
                    if family == "obligation:spot":
                        args = ["-f", "G F (i <-> o)", "-i", "i", "-o", "o", *options]
                    elif family == "parity:spot":
                        args += ["-s", str(root / "missing" / "controller.aag")]
                    else:
                        args = ["-f", "1", "-i", "i", "-o", "o", *options]
                run = support["execute"](binary, args, directory, {
                    "LD_PRELOAD": str(acknowledgement), "ACACIA_TEST_RECORD_ACK": "1"})
                unknown = case == "unknown"
                assert run.returncode == (2 if unknown else 0), run
                records = support["rows"](directory)
                support["complete_records"](records, expected_winners=0 if unknown else 1)
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
