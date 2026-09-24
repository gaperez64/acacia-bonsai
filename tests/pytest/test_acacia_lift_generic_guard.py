"""Keep source identities and historical registries out of production decisions."""

from __future__ import annotations

import ast
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = [ROOT / "scripts/acacia-lift-portfolio.py", *
              (ROOT / "scripts/acacia_lift").rglob("*.py"),
              ROOT / "subprojects/tlsf-tools/scripts/gr1_monitor_game.py"]
NATIVE_ROUTE = [ROOT / "subprojects/tlsf-tools" / path for path in (
    "src/main_tlsfsolve.c", "src/main_tlsfcertcheck.c", "src/gr1_oxidd.c",
    "include/tlsf/oxidd_common.h")]
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
    for path in PRODUCTION:
        assert not violations(path.read_text(encoding="utf-8"),
                              check_source_flow=path.parent !=
                              ROOT / "subprojects/tlsf-tools/scripts"), path
    for path in NATIVE_ROUTE:
        source = path.read_text(encoding="utf-8")
        assert not any(fragment in source for fragment in FORBIDDEN_PATHS), path
        assert not any((label == token if label == "lift" else label in token)
                       for token in re.findall(r'"(?:\\.|[^"\\])*"', source)
                       for label in FAMILY_LABELS), path


@pytest.mark.parametrize("mutant", [
    'decision = Path(source).name.startswith("ar" + "biter")',
    'p = Path(source)\nn = p.name\ndecision = n.startswith("ar" + "biter")',
    'key = sha256_file(source)\ndecision = {"a" * 64: "REALIZABLE"}.get(key)',
    'key = (len(inputs), len(outputs))\ndecision = {(1, 2): "REALIZABLE"}.get(key)',
    'p = Path(source)\nkey = sha256_file(p)\ndecision = verdicts[key]',
    'rows = Path("benchmarking/" + "param-lift-20260922/x.tsv").read_text()',
])
def test_guard_rejects_registry_mutations(mutant: str) -> None:
    assert violations(mutant)
