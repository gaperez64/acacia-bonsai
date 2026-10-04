#!/usr/bin/env python3
"""Fast synthetic regression for the S0 Markdown summarizer."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest


HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("s0_summarize", HERE / "summarize.py")
assert SPEC is not None and SPEC.loader is not None
summarize = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = summarize
SPEC.loader.exec_module(summarize)


class SummarizeTest(unittest.TestCase):
    def test_tiny_raw_directory_produces_complete_markdown(self) -> None:
        with tempfile.TemporaryDirectory(prefix="s0-summary-test-") as temporary:
            root = pathlib.Path(temporary)
            raw = root / "raw"
            (raw / "evidence").mkdir(parents=True)
            (raw / "invocations.tsv").write_text(
                "family\tn\texit_code\tdiagnostics\tevidence\n"
                "real_case\t2\t0\t/moved/real_case-n2.json\t/moved/evidence/real_case-n2.json\n"
                "unreal_case\t3\t1\t/moved/unreal_case-n3.json\t/moved/evidence/unreal_case-n3.json\n",
                encoding="utf-8",
            )

            def document(family: str, target: int, verdict: str,
                         censored: bool = False) -> dict:
                child = {
                    "tool": "generalize_gr1",
                    "phases": {
                        "target_check": {"calls": 1, "wall_s": 6.0},
                        "target_solve": {"calls": 1, "wall_s": 2.0},
                        "export": {"calls": 0, "wall_s": 0.0},
                    },
                    "censored": ([
                        {
                            "stage": "target_check", "kind": "phase",
                            "elapsed_lower_bound_s": 0.5,
                            "reason": "absolute_deadline_exhausted",
                        },
                        {
                            "stage": "target_check", "kind": "stage",
                            "elapsed_lower_bound_s": 0.5,
                            "reason": "absolute_deadline_exhausted",
                        },
                    ] if censored else []),
                    "checker_stats": {
                        "supported": True,
                        "attempts": [{
                            "label": f"target-{target}", "node_cap": 64,
                            "stats": {"visited": 12},
                        }],
                    },
                    "distributions": {
                        "projected_root_support_widths": [
                            {"width": 1}, {"width": 3}, {"width": 8}],
                        "ownership_tuple_arity_histogram": {"0": 2, "1": 5},
                        "distinct_subset_count": 2,
                        "subset_reuse": [
                            {"uses": 1, "uses_by_operation": {"projection": 1}},
                            {"uses": 3, "uses_by_operation": {"instantiation": 3}},
                        ],
                        "uint64_variable_mask_word_length_histogram": {"1": 2},
                        "mode_count_basis": "actual_checker_attempts",
                        "mode_count_histogram": {"4": 1},
                    },
                }
                return {
                    "tool": "param-lift-campaign",
                    "status": "completed",
                    "elapsed_s": 10.0,
                    "outcome": {"family": family, "target": target,
                                "verdict": verdict},
                    "cgroup_memory_peak": {"available": True,
                                           "bytes": 1073741824},
                    "censored": [],
                    "generalizer": child,
                }

            (raw / "real_case-n2.json").write_text(
                json.dumps(document("real_case", 2, "REALIZABLE")),
                encoding="utf-8",
            )
            (raw / "unreal_case-n3.json").write_text(
                json.dumps(document("unreal_case", 3, "UNREALIZABLE", True)),
                encoding="utf-8",
            )
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = summarize.main([str(raw)])
            report_path = root / "s0-summary.md"
            self.assertEqual(result, 0)
            self.assertTrue(report_path.is_file())
            report = report_path.read_text(encoding="utf-8")
            self.assertEqual(stdout.getvalue(), report)
            self.assertIn("real_case-n2 | REALIZABLE | 0", report)
            self.assertIn("unreal_case-n3 | UNREALIZABLE | 1", report)
            self.assertIn("target_check ≥ 0.500 s", report)
            self.assertIn("Support width min/median/max: 1 / 3 / 8", report)
            self.assertIn("Actual checker mode counts: 4: 1", report)
            self.assertIn("`{\"visited\":12}`", report)
            self.assertIn("## Cross-target phase dominance", report)
            self.assertIn("target_check — 6.000 s (60.0%)", report)
            self.assertIn(
                "target_check — ≥ 6.500 s (≥ 65.0%)", report)

    def test_active_repeated_phase_is_added_to_completed_total(self) -> None:
        target = summarize.Target(
            "repeated-phase", "124", {
                "tool": "generalize_gr1",
                "phases": {"from_aag": {"calls": 2, "wall_s": 6.0}},
                "censored": [
                    {
                        "stage": "from_aag", "kind": "phase",
                        "elapsed_lower_bound_s": 2.0,
                        "reason": "absolute_deadline_exhausted",
                    },
                    {
                        "stage": "from_aag", "kind": "stage",
                        "elapsed_lower_bound_s": 2.0,
                        "reason": "absolute_deadline_exhausted",
                    },
                ],
            },
            pathlib.Path("repeated-phase.json"),
        )
        self.assertEqual(
            summarize._dominant_phase_rows(target),
            [("from_aag", 8.0, True)],
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
