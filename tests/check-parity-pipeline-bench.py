#!/usr/bin/env python3
"""Check comparison polarity and feature-only execution on independently decided games."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="pipeline-bench-") as temporary:
        source = Path(temporary) / "fixture.tlsf"
        # The output player can always choose o=true in the first game. The input
        # player can always choose i=true to falsify the second one, regardless of o.
        for formula, verdict in (("G(i || o)", "REALIZABLE"),
                                 ("F(!i && !o)", "UNREALIZABLE")):
            source.write_text('INFO { TITLE: "polarity" SEMANTICS: Mealy TARGET: Mealy } '
                              'MAIN { INPUTS { i; unused_i; } OUTPUTS { o; unused_o; } '
                              'GUARANTEES { ' + formula + '; } }')
            for order in ([], ["--reverse"]):
                run = subprocess.run([str(binary), str(source), "--verify", *order],
                                     capture_output=True, text=True, timeout=40, check=True)
                rows = [json.loads(line) for line in run.stdout.splitlines()]
                graph, = [r for r in rows if r["event"] == "shared_graph"]
                assert graph["states"] > 0 and graph["edges"] > 0
                results = [r for r in rows if r["event"] == "route_result"]
                assert len(results) == 2 and {r["mode"] for r in results} == {"positive", "classified"}
                assert all(r["verdict"] == verdict and r["checked"] for r in results), results
                exits = [r for r in rows if r["event"] == "route_exit"]
                assert len(exits) == 2
                assert all(r["exit_code"] == (0 if verdict == "REALIZABLE" else 1)
                           and r["signal"] == 0 and not r["timeout_censored"]
                           and r["wall_ns"] > 0 for r in exits), exits
            run = subprocess.run([str(binary), str(source), "--features"],
                                 capture_output=True, text=True, timeout=10, check=True)
            rows = [json.loads(line) for line in run.stdout.splitlines()]
            features, = [r for r in rows if r["event"] == "cached_features"]
            assert features["inputs"] == features["outputs"] == 2
            assert not any(r["event"] in {"shared_graph", "route_begin"} for r in rows)
        invalid = subprocess.run([str(binary), str(source), "--seconds", "0"],
                                 capture_output=True, text=True, timeout=10)
        assert invalid.returncode == 3 and json.loads(invalid.stdout)["event"] == "error"
    print("Both dispatcher orders preserve known verdicts and checked strategies")


if __name__ == "__main__":
    main()
