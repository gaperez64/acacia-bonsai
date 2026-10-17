"""Keep source identities and historical registries out of native solver code."""

from __future__ import annotations

import ast
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE_ROUTE = [ROOT / "src/acacia-bonsai.cc", ROOT / "src/arg_parser.hh",
                ROOT / "src/arg_parser.cc",
                ROOT / "src/portfolio_arm.hh", ROOT / "src/portfolio_arm.cc",
                *(ROOT / "src").glob("native_*.hh"),
                *(ROOT / "src").glob("native_*.cc"),
                *(ROOT / "subprojects/tlsf-tools" / path for path in (
    "src/lib/gr1_check.c", "src/lib/gr1_check_internal.h",
    "src/lib/gr1_reduction.cc", "src/lib/gr1_lift.cc",
    "src/lib/gr1_service.c", "src/lib/pipeline.c",
    "src/lib/pipeline_source.c", "src/lib/pipeline_source_internal.h",
    "src/lib/spec_parse.c", "src/tools/common/cli.c",
    "src/lib/gr1_oxidd.c", "src/lib/oxidd_common.c",
    "src/lib/oxidd_common.h", "src/lib/oxidd_order.c",
    "include/tlsf/oxidd_options.h",
    "src/tools/tlsfsolve/main.c", "src/tools/tlsfcertcheck/main.c"))]
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
FORBIDDEN_PATHS = ("syntcomp-benchmarks", "tests/suites/benchmarks", "tlsf-corpus",
                   "param-lift-20260922", "legacy_lift", "capabilities-v1",
                   "generic-census.tsv", "m4-analysis",
                   "eligibility-v3.tsv")


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


def test_no_family_labels_or_runtime_corpus_reads() -> None:
    assert len(FAMILY_LABELS) == 29
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
