#!/usr/bin/env python3
"""Synthetic accounting records: no solver invocation or measurement inputs."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prof_lib", ROOT / "benchmarking/prof/prof_lib.py")
prof = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prof)


class Accounting(unittest.TestCase):
    def test_exclusive_interleaved_and_censored(self):
        def entry(i, parent, wall, cpu, phase):
            return dict(
                _pid=3,
                event="accounting_entry",
                phase_id=i,
                parent_phase_id=parent,
                entry_ns=wall,
                entry_cpu_ns=cpu,
                mono_ns=wall,
                phase=phase,
            )

        records = [
            entry(1, 0, 10, 5, "native_r_candidate"),
            entry(2, 1, 20, 10, "native_candidate_reduction"),
            dict(
                entry(2, 1, 20, 10, "native_candidate_reduction"),
                event="accounting_end",
                mono_ns=40,
                wall_ns=20,
                cpu_ns=8,
            ),
            dict(
                entry(1, 0, 10, 5, "native_r_candidate"),
                event="accounting_end",
                mono_ns=60,
                wall_ns=50,
                cpu_ns=25,
            ),
        ]
        scopes = {s["phase"]: s for s in prof.accounting_stages(records)}
        self.assertEqual(scopes["native_r_candidate"]["exclusive_cpu_ns"], 17)
        self.assertEqual(scopes["native_r_candidate"]["exclusive_wall_ns"], 30)
        records[-2]["cpu_ns"] = None
        scopes = {s["phase"]: s for s in prof.accounting_stages(records)}
        self.assertIsNone(scopes["native_r_candidate"]["exclusive_cpu_ns"])
        records[-2]["cpu_ns"] = 8
        records.pop()
        scopes = {s["phase"]: s for s in prof.accounting_stages(records)}
        self.assertIsNone(scopes["native_r_candidate"]["raw_cpu_ns"])
        self.assertFalse(scopes["native_r_candidate"]["complete"])
        recovered = dict(records[0], event="accounting_censored", mono_ns=70, cpu_ns=None)
        censored = {s["phase"]: s for s in prof.accounting_stages([recovered] + records)}
        self.assertEqual(censored["native_r_candidate"]["mono_ns"], 70)
        self.assertIsNone(censored["native_r_candidate"]["raw_cpu_ns"])
        records[-1]["dropped_records"] = 1
        scopes = {s["phase"]: s for s in prof.accounting_stages(records)}
        self.assertIsNone(scopes["native_candidate_reduction"]["exclusive_cpu_ns"])

    def test_lost_children_and_invalid_residuals_are_unknown(self):
        for child_cpu in (20, 120):
            for failure in ("missing_entry", "packet_loss", "negative"):
                with self.subTest(child_cpu=child_cpu, failure=failure):
                    parent = dict(_pid=3, stage_id=1, stage="search", seq=1,
                                  mono_ns=10, dropped_records=0)
                    child = dict(_pid=3, stage_id=2, stage="translation", seq=2,
                                 mono_ns=20, dropped_records=0)
                    records = [dict(parent, event="stage_entry"),
                               dict(child, event="stage_entry"),
                               dict(child, event="stage_completion", seq=3, mono_ns=40,
                                    wall_ns=20, cpu_ns=child_cpu),
                               dict(parent, event="stage_completion", seq=4, mono_ns=110,
                                    wall_ns=100, cpu_ns=80, dropped_records=int(failure == "packet_loss"))]
                    # A missing child entry invalidates its enclosing measurement,
                    # including when the producer's loss count did not change.
                    if failure == "missing_entry":
                        records.pop(1)
                    scopes = {e["stage_id"]: e for e in prof.timed_stages(records)}
                    if failure != "negative" or child_cpu > 80:
                        self.assertIsNone(scopes[1]["exclusive_cpu_ns"])
                    else:
                        self.assertEqual(scopes[1]["exclusive_cpu_ns"], 60)
                    if failure == "missing_entry":
                        self.assertIsNone(scopes[2]["exclusive_cpu_ns"])
                    for e in records:
                        e.update(phase_id=e["stage_id"], phase=e["stage"],
                                 parent_phase_id=1 if e["stage_id"] == 2 else 0)
                        e["event"] = {"stage_entry": "accounting_entry",
                                      "stage_completion": "accounting_end"}[e["event"]]
                    scopes = {e["phase_id"]: e for e in prof.accounting_stages(records)}
                    if failure != "negative" or child_cpu > 80:
                        self.assertIsNone(scopes[1]["exclusive_cpu_ns"])
                    else:
                        self.assertEqual(scopes[1]["exclusive_cpu_ns"], 60)

    def test_orphaned_tree_has_unknown_exclusive_cpu(self):
        records = []
        for pid, phase_id, parent, cpu in [(3, 1, 0, 80), (3, 2, 1, 60),
                                           (3, 3, 2, 40), (3, 4, 3, 10), (4, 1, 0, 80)]:
            scope = dict(_pid=pid, phase_id=phase_id, parent_phase_id=parent,
                         phase="search", dropped_records=0)
            records.extend([dict(scope, event="accounting_entry"),
                            dict(scope, event="accounting_end", cpu_ns=cpu, wall_ns=cpu)])
        for missing in ({"accounting_entry"}, {"accounting_end"},
                        {"accounting_entry", "accounting_end"}):
            with self.subTest(missing=missing):
                damaged = [e for e in records if not (
                    e["_pid"] == 3 and e["phase_id"] == 3 and e["event"] in missing)]
                scopes = {(e["_pid"], e["phase_id"]): e for e in prof.accounting_stages(damaged)}
                for phase_id in (1, 2):
                    self.assertIsNone(scopes[3, phase_id]["exclusive_cpu_ns"])
                self.assertEqual(scopes[4, 1]["exclusive_cpu_ns"], 80)

    def test_identity_loss_never_inherits_another_work_unit(self):
        with tempfile.TemporaryDirectory() as location:
            root = Path(location)
            row = dict(instance="input", solver_label="test", cap_s="17", run_index="0")
            directory = prof.phase_directory(root, row)
            directory.mkdir(parents=True)
            identity = dict(event="work_identity", worker_pid=3, subjob=1, run_id=1,
                            mono_ns=10, formula_sha256="formula", alphabet_sha256="alphabet",
                            provider="spot", translation_options_sha256="translation",
                            preprocessing_options_sha256="preprocessing")
            events = [dict(event="worker_spec", worker_pid=3, kind="legacy"), identity]
            for phase_id, subjob, run_id in [(1, 1, 1), (2, 2, 1), (3, 1, 2), (4, None, None)]:
                scope = dict(worker_pid=3, phase_id=phase_id, parent_phase_id=0,
                             phase="translation", subjob=subjob, run_id=run_id, entry_ns=20)
                events.extend([dict(scope, event="accounting_entry", mono_ns=20),
                               dict(scope, event="accounting_end", mono_ns=40, wall_ns=20,
                                    cpu_ns=10)])
            (directory / "3.jsonl").write_text("".join(json.dumps(e) + "\n" for e in events))
            candidates = prof.analyze_invocation(row, root)["duplicates"]
            self.assertEqual(len(candidates), 1)
            self.assertEqual((candidates[0]["subjob"], candidates[0]["run_id"]), (1, 1))
            events.append(dict(identity, formula_sha256="different-formula"))
            (directory / "3.jsonl").write_text("".join(json.dumps(e) + "\n" for e in events))
            self.assertEqual(prof.analyze_invocation(row, root)["duplicates"], [])
            first = dict(candidates[0], arm="first")
            other = dict(first, arm="second", worker_pid=4)
            self.assertEqual(len(prof.duplicate_estimates([first, other])), 2)
            for field, value in [("record_directory", "another-invocation"),
                                 ("subjob", 2), ("run_id", 2), ("worker_pid", None)]:
                self.assertEqual(prof.duplicate_estimates([first, dict(other, **{field: value})]), [])

    def test_final_usage_boundaries_and_unknowns(self):
        with tempfile.TemporaryDirectory() as location:
            root = Path(location)
            row = dict(instance="input", solver_label="test", cap_s="17", run_index="0")
            directory = prof.phase_directory(root, row)
            directory.mkdir(parents=True)
            events = [
                dict(
                    event="worker_spec",
                    worker_pid=3,
                    mono_ns=10,
                    kind="legacy",
                    translation="small",
                    requested_backend="backward",
                    requested_polarity="REAL",
                ),
                dict(
                    event="worker_reap",
                    worker_pid=3,
                    mono_ns=1_000_000_010,
                    user_ns=400_000_000,
                    sys_ns=100_000_000,
                    self_cpu_ns=450_000_000,
                    peak_rss_kb=1234,
                    dropped_records=7,
                ),
                dict(
                    event="worker_resource",
                    boundary="decline",
                    worker_pid=3,
                    mono_ns=500_000_010,
                    process_cpu_ns=200_000_000,
                    cpu_resolution_ns=1,
                    rss_kb=100,
                ),
                dict(
                    event="descendant_reap",
                    worker_pid=3,
                    descendant_pid=4,
                    user_ns=30_000_000,
                    sys_ns=20_000_000,
                ),
                dict(
                    event="worker_resource",
                    boundary="terminal_result",
                    worker_pid=3,
                    mono_ns=900_000_010,
                    process_cpu_ns=None,
                    rss_kb=None,
                ),
            ]
            scope = dict(worker_pid=3, phase_id=1, parent_phase_id=0,
                         phase="obligation_spot", entry_ns=10, entry_cpu_ns=5)
            events.extend([
                dict(scope, event="accounting_entry", mono_ns=10),
                dict(scope, event="accounting_end", mono_ns=100_000_010,
                     wall_ns=100_000_000, cpu_ns=80_000_000),
            ])
            (directory / "2-final.jsonl").write_text("".join(json.dumps(e) + "\n" for e in events))
            data = prof.analyze_invocation(row, root)
            phases = {e["phase"]: e for e in data["phases"]}
            self.assertEqual(phases["obligation_opaque"]["cpu_s"], 0.08)
            self.assertEqual(phases["obligation_opaque"]["wall_s"], 0.1)
            self.assertIsNone(phases["translation"]["cpu_s"])
            self.assertIsNone(phases["game_solving"]["cpu_s"])
            worker = data["workers"][0]
            self.assertEqual(worker["arm"], "real:small:backward")
            self.assertEqual(worker["peak_rss_observed_bytes"], 1234 * 1024)
            self.assertEqual(worker["peak_status"], "wait4_final")
            self.assertEqual(worker["descendant_cpu_s"], 0.05)
            self.assertEqual(worker["lifetime_wall_s"], 1)
            life = {e["trigger"]: e for e in data["lifetimes"]}
            self.assertEqual(life["decline"]["cpu_boundary_to_reap_s"], 0.25)
            self.assertEqual(life["decline"]["memory_after_trigger_bytes"], 102400)
            self.assertIsNone(life["decline"]["retained_memory_integral_byte_s"])
            self.assertIsNone(life["terminal_result"]["cpu_boundary_to_reap_s"])
            self.assertEqual(data["quality"]["dropped_records"], 7)
            events[1].update(user_ns=None, sys_ns=None, self_cpu_ns=None)
            (directory / "2-final.jsonl").write_text("".join(json.dumps(e) + "\n" for e in events))
            data = prof.analyze_invocation(row, root)
            self.assertIsNone(data["workers"][0]["self_cpu_s"])
            self.assertTrue(all(e["cpu_boundary_to_reap_s"] is None for e in data["lifetimes"]))


if __name__ == "__main__":
    unittest.main()
