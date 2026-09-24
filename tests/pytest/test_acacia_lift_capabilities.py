"""Capability data replaces only the M4 runtime decisions."""

from __future__ import annotations

import csv
import hashlib
import json
import pathlib
import sys
from unittest import mock

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from acacia_lift import generalizer  # noqa: E402
from acacia_lift.capabilities import (  # noqa: E402
    CAPABILITIES, DATA_FILE, DECLINED_FAMILY_STABLE_FROM, load_capabilities,
)
from acacia_lift.evidence import write_result  # noqa: E402
from acacia_lift.schema import stable_from as schema_stable_from  # noqa: E402


EVIDENCE_DIR = ROOT / "benchmarking" / "param-lift-20260922"
M4_FILES = (
    EVIDENCE_DIR / "m4-alignment.tsv",
    EVIDENCE_DIR / "m4-invariant-separability.tsv",
    EVIDENCE_DIR / "m4-move-separability.tsv",
)


def _rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def test_capability_decisions_match_dated_measurements() -> None:
    alignment, invariants, moves = map(_rows, M4_FILES)
    for family, capability in CAPABILITIES.items():
        align = next((row for row in alignment if row["family"] == family), None)
        assert align is not None
        stable = int(align["stable_from"]) if align["stable_from"] else (
            3 if family == "round_robin_arbiter_unreal2" else None)
        roles = {int(value) for value in align["role_classes"].split(",") if value}
        expected_roles = next(iter(roles)) if len(roles) == 1 else None
        inv = tuple(sorted({int(row["min_k"]) for row in invariants
                            if row["family"] == family and row["min_k"]}))
        move = tuple(sorted({int(row["min_k"]) for row in moves
                             if row["family"] == family and row["min_k"]}))
        expected_arity = (inv[0] if len(inv) == 1 and
                          (not move or move == inv) else None)
        assert capability.stable_from == stable
        assert capability.role_class_count == expected_roles
        assert capability.invariant_arities == inv
        assert capability.move_arities == move
        assert generalizer.stable_from(family) == stable
        assert generalizer.measured_role_class_count(family) == expected_roles
        assert generalizer.measured_arity(family) == expected_arity


def test_runtime_decisions_work_when_m4_tables_are_unavailable() -> None:
    original_open = pathlib.Path.open

    def without_m4(self: pathlib.Path, *args: object, **kwargs: object):
        if self.resolve() in M4_FILES:
            raise FileNotFoundError(self)
        return original_open(self, *args, **kwargs)

    with mock.patch.object(pathlib.Path, "open", without_m4):
        registry = load_capabilities()
        for family in registry:
            assert generalizer.stable_from(family) == registry[family].stable_from
            assert generalizer.measured_arity(family) == registry[family].measured_arity
            assert generalizer.measured_role_class_count(family) == registry[family].role_class_count
        for family, stable in DECLINED_FAMILY_STABLE_FROM.items():
            assert generalizer.stable_from(family) == stable


def test_decline_only_families_keep_their_stable_regimes() -> None:
    alignment = _rows(M4_FILES[0])
    for row in alignment:
        if row["stable_from"] and row["family"] not in CAPABILITIES:
            assert schema_stable_from(row["family"]) == int(row["stable_from"])


def test_capability_schema_rejects_stale_template_and_invalid_fields(tmp_path: pathlib.Path) -> None:
    payload = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    payload["capabilities"][0]["template_sha256"] = "0" * 64
    path = tmp_path / "capabilities.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="stale capability template"):
        load_capabilities(path)

    payload["capabilities"][0]["template_sha256"] = CAPABILITIES["arbiter"].template_sha256
    payload["capabilities"][0]["stable_from"] = "2"
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="stable_from"):
        load_capabilities(path)


def test_vendored_templates_match_pins_and_corpus_provenance() -> None:
    payload = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    assert len(payload["capabilities"]) == 14
    for row in payload["capabilities"]:
        source = ROOT / row["source"]
        assert source.is_file()
        assert row["source"] == row["corpus_source"].replace(
            "tests/syntcomp-benchmarks/", "scripts/acacia_lift/data/templates/", 1
        )
        assert hashlib.sha256(source.read_bytes()).hexdigest() == row["template_sha256"]
        assert CAPABILITIES[row["family"]].corpus_source == row["corpus_source"]


def test_missing_template_is_distinct_from_hash_mismatch(tmp_path: pathlib.Path) -> None:
    payload = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    payload["capabilities"][0]["source"] = "scripts/acacia_lift/data/templates/missing.tlsf"
    path = tmp_path / "capabilities.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="missing capability template for arbiter"):
        load_capabilities(path)


def test_corpus_provenance_is_validated_without_reading_it(tmp_path: pathlib.Path) -> None:
    payload = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    path = tmp_path / "capabilities.json"
    source = ROOT / payload["capabilities"][0]["corpus_source"]
    original_read_bytes = pathlib.Path.read_bytes

    def no_corpus_read(self: pathlib.Path) -> bytes:
        if self == source:
            raise AssertionError("corpus provenance was read")
        return original_read_bytes(self)

    with mock.patch.object(pathlib.Path, "read_bytes", no_corpus_read):
        assert load_capabilities(path=DATA_FILE)["arbiter"].corpus_source == str(
            source.relative_to(ROOT)
        )

    payload["capabilities"][0]["corpus_source"] = "../outside.tlsf"
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="invalid corpus source path"):
        load_capabilities(path)


def test_results_artifact_never_reads_previous_ledger(tmp_path: pathlib.Path) -> None:
    path = tmp_path / "results.tsv"
    path.write_text("poisoned\tledger\n", encoding="utf-8")
    write_result(path, {"family": "arbiter", "target": 6, "verdict": "VERIFIED"})
    rows = _rows(path)
    assert len(rows) == 1
    assert rows[0]["family"] == "arbiter"
    assert rows[0]["target"] == "6"
    assert rows[0]["verdict"] == "VERIFIED"
