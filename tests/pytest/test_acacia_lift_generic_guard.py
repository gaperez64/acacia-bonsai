"""Keep source identities and historical registries out of native solver code."""

from __future__ import annotations

import ast
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
ORACLE = ROOT / "benchmarking/gr1-par2-20260923/oracle"
MONITOR = ROOT / "subprojects/tlsf-tools/scripts/gr1_monitor_game.py"
ORACLE_FILES = [ORACLE / "acacia-lift-portfolio.py",
                *(ORACLE / "acacia_lift").rglob("*.py"), MONITOR]
NATIVE_ROUTE = [ROOT / "src/acacia-bonsai.cc", ROOT / "src/arg_parser.hh",
                ROOT / "src/portfolio_arm.hh", *(ROOT / "src").glob("native_*.hh"),
                *(ROOT / "subprojects/tlsf-tools/src/native").glob("*.c"),
                *(ROOT / "subprojects/tlsf-tools/src/native").glob("*.cc"),
                *(ROOT / "subprojects/tlsf-tools" / path for path in (
    "src/main_tlsfsolve.c", "src/main_tlsfcertcheck.c", "src/gr1_oxidd.c",
    "include/tlsf/oxidd_common.h"))]
FAMILY_LABELS = (
    "abcg_arbiter", "amba_case_study", "amba_case_study_unreal",
    "amba_decomposed_arbiter", "amba_decomposed_encode", "amba_decomposed_lock",
    "arbiter", "arbiter_on_inpchange", "arbiter_with_buffer", "arbiter_with_cancel",
    "chomp", "collector_v1", "collector_v3", "full_arbiter_enc", "lift",
    "lift_unary_enc", "load_balancer", "load_balancer_unreal1",
    "load_balancer_unreal2", "prioritized_arbiter", "prioritized_arbiter_enc",
    "prioritized_arbiter_unreal2", "robot_grid", "round_robin_arbiter",
    "round_robin_arbiter_unreal1", "round_robin_arbiter_unreal2", "rw_arbiter",
    "simple_arbiter_enc", "simple_arbiter_with_hints",
)
INPUT_NAMES = {"source", "tlsf", "input_path", "input_file", "lift_source",
               "source_argument"}
FORBIDDEN_PATHS = ("syntcomp-benchmarks", "tests/suites/benchmarks", "tlsf-corpus",
                   "param-lift-20260922", "legacy_lift", "capabilities-v1",
                   "generic-census.tsv", "m4-analysis",
                   "eligibility-v3.tsv")
VERDICTS = {"REALIZABLE", "UNREALIZABLE", "UNKNOWN"}


def cxx_strings(source: str) -> list[str]:
    """Include adjacent and `+`-joined literals in the native source scan."""
    tokens = list(re.finditer(r'"(?:\\.|[^"\\])*"', source))
    values = []
    for index, token in enumerate(tokens):
        value = token.group()[1:-1]
        values.append(value)
        previous_end = token.end()
        for follower in tokens[index + 1:]:
            gap = source[previous_end:follower.start()]
            if not re.fullmatch(r'\s*(?:\+\s*)?', gap):
                break
            value += follower.group()[1:-1]
            values.append(value)
            previous_end = follower.end()
    return values


def folded_string(node: ast.AST) -> str | None:
    """Resolve literals built from constants, including simple join and f-strings."""
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        left, right = folded_string(node.left), folded_string(node.right)
        return left + right if left is not None and right is not None else None
    if (isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult) and
            isinstance(node.right, ast.Constant) and
            isinstance(node.right.value, int) and 0 <= node.right.value <= 256):
        value = folded_string(node.left)
        return value * node.right.value if value is not None else None
    if isinstance(node, ast.JoinedStr):
        parts = [folded_string(part) for part in node.values]
        return "".join(parts) if all(part is not None for part in parts) else None
    if isinstance(node, ast.FormattedValue):
        return folded_string(node.value)
    if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
            and node.func.attr == "join" and len(node.args) == 1
            and isinstance(node.args[0], (ast.Tuple, ast.List))):
        separator = folded_string(node.func.value)
        parts = [folded_string(part) for part in node.args[0].elts]
        if separator is not None and all(part is not None for part in parts):
            return separator.join(parts)
    return None


def named_targets(node: ast.AST) -> set[str]:
    if isinstance(node, ast.Name):
        return {node.id}
    if isinstance(node, (ast.Tuple, ast.List)):
        return set().union(*(named_targets(part) for part in node.elts))
    return set()


def _branch_keyed_to_source(test: ast.AST) -> list[str]:
    """Reject literal gates on spelling, size, or a particular formula."""
    errors = []
    for branch in (node for node in ast.walk(test)
                   if isinstance(node, (ast.Compare, ast.Call))):
        operands = ([branch.left, *branch.comparators]
                    if isinstance(branch, ast.Compare) else [branch])
        literals = [node.value for operand in operands for node in ast.walk(operand)
                    if isinstance(node, ast.Constant)]
        if not literals:
            continue
        names = {node.id.lower() for operand in operands for node in ast.walk(operand)
                 if isinstance(node, ast.Name)}
        keys = {folded_string(node.slice) for operand in operands for node in ast.walk(operand)
                if isinstance(node, ast.Subscript)}
        fields = names | {key.lower() for key in keys if key is not None}
        if any(isinstance(value, str) for value in literals):
            if fields & {"source_name", "signal_name", "input_name", "output_name",
                         "formula", "formula_text", "spec_formula"}:
                errors.append("signal or formula literal decision")
        if any(isinstance(value, int) and not isinstance(value, bool) and abs(value) > 1
               for value in literals):
            counted = any(isinstance(node, ast.Call) and
                          isinstance(node.func, ast.Name) and node.func.id == "len" and
                          node.args and any(isinstance(part, ast.Name) and
                                            part.id.lower() in {"inputs", "outputs", "signals"}
                                            for part in ast.walk(node.args[0]))
                          for operand in operands for node in ast.walk(operand))
            if counted or fields & {"signal_count", "input_count", "output_count",
                                    "source_size", "instance_size", "size", "n"}:
                errors.append("fixed signal count or size decision")
    return errors


def violations(source: str, *, check_source_flow: bool = True) -> list[str]:
    tree = ast.parse(source)
    errors: list[str] = []
    strings = [value for node in ast.walk(tree)
               if (value := folded_string(node)) is not None]
    for label in FAMILY_LABELS:
        if any((value == label if label == "lift" else label in value)
               for value in strings):
            errors.append(f"family literal: {label}")
    if any(fragment in value for value in strings for fragment in FORBIDDEN_PATHS):
        errors.append("corpus or historical registry path")
    if any(re.search(r"\b(?:family|capabilities|corpus|registry)(?:_|\b)",
                     name.id, re.IGNORECASE) for name in ast.walk(tree)
           if isinstance(name, ast.Name)):
        errors.append("runtime family/corpus registry")
    for branch in ast.walk(tree):
        if isinstance(branch, (ast.If, ast.IfExp, ast.While)):
            errors.extend(_branch_keyed_to_source(branch.test))
    if not check_source_flow:
        return sorted(set(errors))

    # Follow simple aliases and function calls from source paths/bytes to a
    # basename, hash or signature. This catches e.g. `p = Path(source)` and
    # `key = sha256_file(p)` before a table lookup.
    derived = set(INPUT_NAMES)
    assignments = [node for node in ast.walk(tree)
                   if isinstance(node, (ast.Assign, ast.AnnAssign))]
    for _ in range(len(assignments) + 1):
        before = len(derived)
        for node in assignments:
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if node.value is not None and any(isinstance(part, ast.Name) and
                                              part.id in derived
                                              for part in ast.walk(node.value)):
                derived.update(name for target in targets for name in named_targets(target))
        if len(derived) == before:
            break
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr in {"name", "stem", "suffixes"}:
            if any(isinstance(part, ast.Name) and part.id in derived
                   for part in ast.walk(node.value)):
                errors.append("source-derived basename")
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
            if node.func.attr in {"basename", "splitext"} and any(
                    isinstance(part, ast.Name) and part.id in derived
                    for arg in node.args for part in ast.walk(arg)):
                errors.append("source-derived basename")
            if node.func.attr == "get" and node.args and any(
                    isinstance(part, ast.Name) and part.id in derived
                    for part in ast.walk(node.args[0])):
                errors.append("source-derived table lookup")
        if isinstance(node, ast.Subscript) and not isinstance(node.slice, ast.Constant):
            verdict_exit = (isinstance(node.value, ast.Name) and
                            node.value.id == "DECISIVE_EXITS" and
                            isinstance(node.slice, ast.Name) and
                            node.slice.id == "verdict")
            if not verdict_exit and any(isinstance(part, ast.Name) and part.id in derived
                                        for part in ast.walk(node.slice)):
                errors.append("source-derived table lookup")
        if isinstance(node, ast.Dict):
            for key, value in zip(node.keys, node.values, strict=True):
                if key is None:
                    continue
                key_text = folded_string(key)
                fingerprint = key_text is not None and re.fullmatch(r"[0-9a-f]{64}", key_text)
                signature = isinstance(key, ast.Tuple) and all(
                    isinstance(part, ast.Constant) and isinstance(part.value, int)
                    for part in key.elts)
                decision = (folded_string(value) in VERDICTS or
                            isinstance(value, ast.Constant) and
                            isinstance(value.value, (bool, int)))
                if (fingerprint or signature) and decision or (
                        folded_string(value) in VERDICTS and key_text != "verdict"):
                    errors.append("source-keyed verdict registry")
    return sorted(set(errors))


def test_no_family_labels_or_runtime_corpus_reads() -> None:
    assert len(FAMILY_LABELS) == 29
    assert (ORACLE / "acacia-lift-portfolio.py").is_file()
    assert (ORACLE / "acacia_lift/runner.py").is_file()
    assert len(ORACLE_FILES) > 2
    for path in ORACLE_FILES:
        assert path.is_file(), path
        assert not violations(path.read_text(encoding="utf-8"),
                              check_source_flow=path != MONITOR), path
    assert not (ROOT / "scripts/acacia-lift-portfolio.py").exists()
    assert not (ROOT / "scripts/acacia_lift").exists()
    assert "benchmarking/gr1-par2-20260923/oracle/" in (ROOT / ".dockerignore").read_text()
    assert NATIVE_ROUTE
    for path in NATIVE_ROUTE:
        assert path.is_file(), path
        source = path.read_text(encoding="utf-8")
        assert not any(fragment in source for fragment in FORBIDDEN_PATHS), path
        # `lift` is the public transform/stage name in the native API.
        assert not any(label in token
                       for token in cxx_strings(source)
                       for label in FAMILY_LABELS if label != "lift"), path


def test_native_guard_folds_split_family_literals() -> None:
    assert "arbiter" in cxx_strings('"arb" "iter"')
    assert "arbiter" in cxx_strings('"arb" + "iter"')


def test_live_oracle_guard_rejects_mutated_copy(tmp_path: Path) -> None:
    mutant = tmp_path / "runner.py"
    mutant.write_text((ORACLE / "acacia_lift/runner.py").read_text(encoding="utf-8") +
                      '\nfamily_results = {"arbiter": "REALIZABLE"}\n', encoding="utf-8")
    with pytest.raises(AssertionError):
        assert not violations(mutant.read_text(encoding="utf-8")), mutant


@pytest.mark.parametrize("mutant", [
    'decision = Path(source).name.startswith("ar" + "biter")',
    'p = Path(source)\nn = p.name\ndecision = n.startswith("ar" + "biter")',
    'key = sha256_file(source)\ndecision = {"a" * 64: "REALIZABLE"}.get(key)',
    'key = (len(inputs), len(outputs))\ndecision = {(1, 2): "REALIZABLE"}.get(key)',
    'p = Path(source)\nkey = sha256_file(p)\ndecision = verdicts[key]',
    'rows = Path("benchmarking/" + "param-lift-20260922/x.tsv").read_text()',
    'if row["source_name"] == "g":\n    decision = True',
    'if len(inputs) == 7:\n    decision = True',
    'if formula == "G (g -> F r);":\n    decision = True',
    'if row["source_name"].startswith("g"):\n    decision = True',
    'if size == 7:\n    decision = True',
    'if formula in {"G (g -> F r);"}:\n    decision = True',
])
def test_guard_rejects_registry_mutations(mutant: str) -> None:
    assert violations(mutant)
