#!/usr/bin/env python3
"""Generated ordering controls with exact Mealy controller validation."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


ORDERS = ("incumbent", "typed-interleaved", "role-grouped")
ARMS = ("real:small:backward", "real:small:forward",
        "unreal:formula:spot-guarded-sparse", "unreal:automaton:forward",
        "both:gr1-real-lift:oxidd")


def cases():
    for label, semantics, formula, expected in (
        ("copy", "Mealy", "G (p <-> x); G (q <-> y);", 0),
        ("future", "Mealy", "G (p <-> X x); G (q <-> y);", 1),
        ("recurrence", "Mealy", "G F (p && !x); G (q <-> y);", 1),
        ("delay", "Moore", "G (X p <-> x); G (X q <-> y);", 0),
    ):
        source = (f'INFO {{ TITLE: "generated ordering control" SEMANTICS: {semantics} '
                  f'TARGET: {semantics} }}\nMAIN {{ INPUTS {{ x; y; }} '
                  f'OUTPUTS {{ p; q; }} GUARANTEE {{ {formula} }} }}\n')
        yield label, source, expected
    source = ('INFO { TITLE: "indexed control" SEMANTICS: Mealy TARGET: Mealy }\n'
              'GLOBAL { PARAMETERS { n = 2; } }\n'
              'MAIN { INPUTS { x[n]; y[n]; } OUTPUTS { p[n]; q[n]; } '
              'GUARANTEE { &&[0 <= k < n] G (p[k] <-> x[k]); '
              '&&[0 <= k < n] G (q[k] <-> y[k]); } }\n')
    yield "indexed", source, 0


def twins(source):
    yield "source", source
    names = {"x": "zz", "y": "aa", "p": "jj", "q": "bb"}
    yield "rename", re.sub(r"\b(x|y|p|q)\b", lambda match: names[match[0]], source)
    yield "reorder", (source.replace("x; y;", "y; x;").replace("p; q;", "q; p;")
                      .replace("x[n]; y[n];", "y[n]; x[n];")
                      .replace("p[n]; q[n];", "q[n]; p[n];"))


# These are the original target formulas, before Moore adaptation.
SYNTHESIS_CASES = {
    "copy": ("Mealy", "G(p <-> x) & G(q <-> y)", "x,y", "p,q"),
    "indexed": ("Mealy", "G(p_0 <-> x_0) & G(p_1 <-> x_1) & "
                "G(q_0 <-> y_0) & G(q_1 <-> y_1)", "x_0,x_1,y_0,y_1", "p_0,p_1,q_0,q_1"),
    "delay": ("Moore", "G(X p <-> x) & G(X q <-> y)", "x,y", "p,q"),
}


class ControllerSemanticError(AssertionError):
    """The exact checker found a trace violating the original target formula."""


def validate_controller(checker, controller, semantics, formula, inputs, outputs):
    result = run(checker, controller, semantics, formula, inputs, outputs)
    if result.returncode == 1:
        raise ControllerSemanticError(result.stdout.decode())
    assert result.returncode == 0, (result.stdout, result.stderr)


def run(binary, *args):
    return subprocess.run([str(binary), *map(str, args)], capture_output=True,
                          timeout=30, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--checker", type=Path, required=True)
    args = parser.parse_args()
    calls = 0
    with tempfile.TemporaryDirectory(prefix="variable-order-") as scratch:
        root = Path(scratch)
        for label, source, expected in cases():
            for control, text in twins(source):
                path = root / f"{label}-{control}.tlsf"
                path.write_text(text)
                for arm in ARMS:
                    outcomes = []
                    for order in ORDERS:
                        result = run(args.binary, "--arms", arm, "--var-order", order, "-T", path)
                        assert result.returncode in (0, 1, 2), result.stderr
                        if result.returncode != 2:
                            assert result.returncode == expected, (label, control, arm, order)
                        outcomes.append((result.returncode, result.stdout))
                        calls += 1
                    assert outcomes[0] == outcomes[1] == outcomes[2], (label, control, arm)
                incumbent = run(args.binary, "-T", path)
                explicit = run(args.binary, "--var-order", "incumbent", "-T", path)
                assert incumbent.returncode == explicit.returncode == expected
                assert incumbent.stdout == explicit.stdout
                if args.baseline:
                    previous = run(args.baseline, "-T", path)
                    assert previous.returncode == incumbent.returncode
                    assert previous.stdout == incumbent.stdout
        # Raw partitions have no typed metadata: each policy retains the input
        # declaration fallback. Permute only the partition, leaving LTL fixed.
        for order in ORDERS:
            for inputs, outputs in (("x,y", "p,q"), ("y,x", "q,p"), ("x,y", "q,p")):
                result = run(args.binary, "--var-order", order, "-f", "G(p <-> x) & G(q <-> y)",
                             "-i", inputs, "-o", outputs)
                assert result.returncode == 0, result.stderr
        # Mealy controllers must satisfy the original formula. Moore synthesis
        # has an inherited defect under separate investigation (wt-moore-synth);
        # only byte/verdict identity is asserted here. Pytest has strict xfails
        # for its exact semantic validation, including the minimal delay case.
        for label, source, expected in cases():
            if expected:
                continue
            path = root / f"{label}.tlsf"
            path.write_text(source)
            controller = root / "controller.aag"
            previous = root / "previous.aag"
            default = run(args.binary, "-T", path, "-s", controller)
            assert default.returncode == 0, default.stderr
            default_bytes = controller.read_bytes()
            specification = SYNTHESIS_CASES[label]
            if specification[0] == "Mealy":
                validate_controller(args.checker, controller, *specification)
            explicit = run(args.binary, "--var-order", "incumbent", "-T", path, "-s", controller)
            assert (explicit.returncode, explicit.stdout, explicit.stderr) == (
                default.returncode, default.stdout, default.stderr)
            assert controller.read_bytes() == default_bytes
            if args.baseline:
                result = run(args.baseline, "-T", path, "-s", previous)
                assert (result.returncode, result.stdout, result.stderr) == (
                    default.returncode, default.stdout, default.stderr)
                assert previous.read_bytes() == default_bytes
            for order in ORDERS[1:]:
                result = run(args.binary, "--var-order", order, "-T", path, "-s", controller)
                assert result.returncode == default.returncode, result.stderr
                if specification[0] == "Mealy":
                    validate_controller(args.checker, controller, *specification)
                else:
                    assert controller.read_bytes() == default_bytes, (label, order)
        bad = run(args.binary, "--var-order", "name-guessed", "-f", "true", "-i", "", "-o", "")
        assert bad.returncode == 3 and b"invalid --var-order" in bad.stderr
    print(f"{calls} generated arm/order checks; raw LTL and incumbent bytes passed; "
          "Mealy controllers verified exactly; Moore controller/verdict identity passed "
          "(semantic validation is a separate strict pytest xfail)")


if __name__ == "__main__":
    main()
