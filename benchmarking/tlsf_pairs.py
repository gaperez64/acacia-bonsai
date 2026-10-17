"""SyFCo pairs with an explicit, already-Mealy native fallback."""
from __future__ import annotations

import pathlib
import subprocess

from benchlib import read_part


def native_inspect(executable, source, timeout):
    result = subprocess.run([str(executable), str(source)], check=True, capture_output=True,
                            timeout=timeout)
    fields = result.stdout.split(b"\0")
    if len(fields) != 7 or fields[-1]:
        raise RuntimeError(f"malformed native inspector output for {source.name}")
    return tuple(field.decode("utf-8") for field in fields[:-1])


def convert_pair(syfco, source, part, timeout, target=None, canonicalizer=None,
                 fallback_inspect=None):
    """Return (formula, converter, SyFCo error), falling back only on SyFCo rejection."""
    def run(cmd, **kwargs):
        return subprocess.run(cmd, check=True, capture_output=True, text=True,
                              timeout=timeout, **kwargs).stdout
    try:
        formula = run([syfco, "--format", "ltlxba", "--mode", "fully",
                       "--part-file", str(part), str(source)])
    except subprocess.CalledProcessError as error:
        if fallback_inspect is None or target != "Mealy":
            raise
        formula, inputs, outputs, _, _, _ = native_inspect(fallback_inspect, source, timeout)
        part.write_text(".inputs" + (" " + inputs.replace(",", " ") if inputs else "") +
                        "\n.outputs" + (" " + outputs.replace(",", " ") if outputs else "") + "\n")
        reason = (error.stderr or error.stdout or str(error)).strip()
        return formula.rstrip() + "\n", "tlsf-tools", reason
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
    return formula.rstrip() + "\n", "syfco", ""
