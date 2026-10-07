"""SyFCo pairs adapted once to a raw-LTL solver's timing convention."""
from __future__ import annotations

import pathlib
import subprocess

from benchlib import read_part


def convert_pair(syfco, source, part, timeout, target=None, canonicalizer=None):
    def run(cmd, **kwargs):
        return subprocess.run(cmd, check=True, capture_output=True, text=True,
                              timeout=timeout, **kwargs).stdout
    formula = run([syfco, "--format", "ltlxba", "--mode", "fully",
                   "--part-file", str(part), str(source)])
    if target:
        # SyFCo's overwrite-target changes metadata; overwrite-semantics can
        # introduce unsupported strong-next. Adapt its plain infinite-LTL AST.
        semantics = run([syfco, "--print-semantics", str(source)])
        tokens = {token.strip() for token in semantics.strip().split(",")}
        if not tokens <= {"Mealy", "Moore", "Strict"} or len(tokens & {"Mealy", "Moore"}) != 1:
            raise ValueError(f"unsupported source semantics: {semantics.strip()}")
        model = "Moore" if "Moore" in tokens else "Mealy"
        if model != target:
            if canonicalizer is None:
                raise ValueError("target adaptation requires --canonicalizer")
            inputs, outputs = read_part(pathlib.Path(part))
            formula = run([str(canonicalizer), "--delay-aps",
                           inputs if model == "Moore" else outputs], input=formula)
    return formula.rstrip() + "\n"
