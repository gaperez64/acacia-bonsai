from __future__ import annotations

import importlib.util
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "syntcomp-pool.py"


def load_module():
    sys.path.insert(0, str(SCRIPT.parent))
    try:
        spec = importlib.util.spec_from_file_location("syntcomp_pool", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_import_deduplicates_exact_pairs_and_preserves_logical_names(tmp_path):
    module = load_module()
    first = tmp_path / "first"
    second = tmp_path / "second"
    pool = tmp_path / "ltl" / "syntcomp"
    maps = tmp_path / "suites" / "benchmarks"
    first.mkdir()
    second.mkdir()
    (first / "old-name.ltl").write_text("G(a)\n")
    (first / "old-name.part").write_text(".inputs\n.outputs a\n")
    (second / "new-name.ltl").write_text("G(a)\n")
    (second / "new-name.part").write_text(".inputs\n.outputs a\n")

    assert module.import_suite("syntcomp21", first, pool, maps) == 1
    assert module.import_suite("syntcomp26", second, pool, maps) == 1
    module.write_index(pool)

    assert len(list(pool.glob("*.ltl"))) == 1
    assert module.validate(pool, maps) == (1, 2)
    old_source = (maps / "syntcomp21" / "sources.tsv").read_text().splitlines()[1]
    new_source = (maps / "syntcomp26" / "sources.tsv").read_text().splitlines()[1]
    assert old_source.split("\t", 1)[1] == new_source.split("\t", 1)[1]


def test_pair_digest_includes_partition(tmp_path):
    module = load_module()
    formula = b"G(a)\n"

    assert module.pair_digest(formula, b".inputs\n.outputs a\n") != module.pair_digest(
        formula, b".inputs a\n.outputs\n"
    )
