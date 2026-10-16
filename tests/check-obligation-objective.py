#!/usr/bin/env python3
"""Check research captures against the shared arm preparation and frontend."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tool", type=Path)
    parser.add_argument("inspector", type=Path)
    parser.add_argument("scratch", type=Path)
    args = parser.parse_args()
    tool = str(args.tool.resolve())

    def capture(formula, inputs="i,unused_input", outputs="o,unused_output", *extra):
        result = subprocess.run([tool, "--formula", formula, "--inputs", inputs,
                                 "--outputs", outputs, *extra], capture_output=True,
                                text=True, check=True)
        return json.loads(result.stdout)

    for formula, inputs, outputs in [("1", "", ""), ("0", "", ""),
                                     ("G (i <-> o)", "i,unused_input", "o,unused_output"),
                                     ("G (r -> X g)", "r", "g"),
                                     ("G o", "", "o"), ("F i", "i", "")]:
        row = capture(formula, inputs, outputs)
        assert row["eligible"] and row["effective_timing"] == "Mealy", row
        assert row["ap_count"] <= row["dag_nodes"], row
        assert set(row["first_registered_inputs"]) <= set(inputs.split(",")), row
        assert set(row["used_outputs_in_traversal_order"]) <= set(outputs.split(",")), row
    row = capture("G (i <-> o)")
    assert row["mapping"] and row["objective"] == "1", row
    raw = capture("G (i <-> o)", "i", "o", "--synthesis")
    assert not raw["simplified"] and not raw["decision_only"], raw
    assert raw["first_registered_inputs"] == ["i"], raw
    renamed = capture("G (r <-> g)", "r", "g", "--no-simplify")
    assert renamed["dag_nodes"] == raw["dag_nodes"], renamed
    bad = subprocess.run([tool, "--formula", "G o", "--inputs", "o", "--outputs", "o"],
                         capture_output=True, text=True)
    assert bad.returncode == 2 and not bad.stdout, bad

    for semantics in ("Mealy", "Moore", "Mealy,Strict", "Moore,Strict"):
        with tempfile.TemporaryDirectory(dir=args.scratch) as location:
            source = Path(location) / "generated.tlsf"
            source.write_text('INFO { TITLE: "generated" DESCRIPTION: "generated" '
                              f'SEMANTICS: {semantics} TARGET: {semantics.split(",")[0]} }} '
                              'MAIN { INPUTS { i; } OUTPUTS { o; } '
                              'ASSUMPTIONS { G i; } GUARANTEES { G (i <-> o); } }')
            row = json.loads(subprocess.check_output([tool, "--tlsf", str(source)], text=True))
            expected = subprocess.check_output([str(args.inspector.resolve()), "--rsimp",
                                                str(source)]).split(b"\0")
            assert row["objective"] == expected[0].decode(), (row, expected)
            assert row["inputs"] == expected[1].decode().split(","), row
            assert row["outputs"] == expected[2].decode().split(","), row
            assert row["source_semantics"] == expected[3].decode(), row
    print("obligation objective captures passed")


if __name__ == "__main__":
    main()
