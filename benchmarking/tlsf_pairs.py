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
    semantics = subprocess.run([syfco, "--print-semantics", str(source)],
                               capture_output=True, text=True, timeout=timeout)
    tokens = {token.strip() for token in semantics.stdout.strip().split(",")}
    models = tokens & {"Mealy", "Moore"}
    valid_semantics = tokens <= {"Mealy", "Moore", "Strict"} and len(models) == 1
    model = next(iter(models)) if valid_semantics else None
    # Preserve the source timing model here; target adaptation follows below.
    # Pull next outwards to avoid SyFCo's unsupported strong-next printing.
    strict_flags = (["--overwrite-semantics", model, "-pxo"]
                    if valid_semantics and "Strict" in tokens else [])
    try:
        formula = run([syfco, "--format", "ltlxba", "--mode", "fully",
                       *strict_flags, "--part-file", str(part), str(source)])
    except subprocess.CalledProcessError as error:
        if fallback_inspect is None or target != "Mealy":
            raise
        formula, inputs, outputs, _, _, _ = native_inspect(fallback_inspect, source, timeout)
        part.write_text(".inputs" + (" " + inputs.replace(",", " ") if inputs else "") +
                        "\n.outputs" + (" " + outputs.replace(",", " ") if outputs else "") + "\n")
        reason = (error.stderr or error.stdout or str(error)).strip()
        return formula.rstrip() + "\n", "tlsf-tools", reason
    # Only formula-conversion rejection permits the native fallback. A metadata
    # failure after successful conversion must still be reported as an error.
    semantics.check_returncode()
    if not valid_semantics:
        raise ValueError(f"unsupported source semantics: {semantics.stdout.strip()}")
    if target:
        # SyFCo's overwrite-target changes metadata. Adapt the infinite-LTL AST.
        if model != target:
            if canonicalizer is None:
                raise ValueError("target adaptation requires --canonicalizer")
            inputs, outputs = read_part(pathlib.Path(part))
            formula = run([str(canonicalizer), "--delay-aps",
                           inputs if model == "Moore" else outputs], input=formula)
    return formula.rstrip() + "\n", "syfco", ""
