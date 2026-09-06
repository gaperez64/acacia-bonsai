from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "syntcomp-corpus.py"


def load_module():
    spec = importlib.util.spec_from_file_location("syntcomp_corpus", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_parametric_expansion_matches_known_template_and_csv_row(tmp_path):
    module = load_module()
    template = tmp_path / "known.tlsf"
    template.write_text(
        "INFO {\n"
        "    PARAMETERS {\n"
        "        n = 1; // replaced without retaining indentation or comment\n"
        "        keep = 7;\n"
        "    }\n"
        "}\n"
    )
    template.with_suffix(".csv").write_text(
        "n,status,refsize\n4,realizable,12\n"
    )

    row = module.parameter_rows(template)[0]

    assert module.expand_template(template, row) == (
        b"INFO {\n"
        b"    PARAMETERS {\n"
        b"n = 4;\n"
        b"        keep = 7;\n"
        b"    }\n"
        b"}\n"
        b"//#!SYNTCOMP\n"
        b"//STATUS : realizable\n"
        b"//REF_SIZE : 12\n"
        b"//#\n"
    )


def test_materialize_fails_when_source_no_longer_matches_manifest_hash(tmp_path):
    module = load_module()
    submodule = tmp_path / "syntcomp-benchmarks"
    source = submodule / "tlsf" / "family" / "example.tlsf"
    source.parent.mkdir(parents=True)
    good = b"INFO { TITLE: \"known-good\" }\n"
    source.write_bytes(good)
    manifest = tmp_path / "tlsf-manifest.tsv"
    manifest.write_text(
        module.MANIFEST_HEADER
        + "\n"
        + "example.tlsf\tdirect:tlsf/family/example.tlsf\t"
        + hashlib.sha256(good).hexdigest()
        + "\n"
    )

    source.write_bytes(b"INFO { TITLE: \"silently-corrupted\" }\n")
    out = tmp_path / "out"
    try:
        module.materialize(out, submodule=submodule, manifest=manifest, no_record=True)
    except module.CorpusError as error:
        assert "hash mismatch: example.tlsf" in str(error)
        assert hashlib.sha256(good).hexdigest() in str(error)
    else:
        assert False, "materialize accepted a file whose manifest hash did not match"

    assert (out / "example.tlsf").read_bytes() == source.read_bytes()


def test_flattened_alias_keeps_the_expanded_file_origin(tmp_path):
    module = load_module()
    submodule = tmp_path / "syntcomp-benchmarks"
    expanded = (
        submodule
        / "tlsf"
        / "family"
        / "F-G-contradiction-_pb_10_pe_.tlsf"
    )
    expanded.parent.mkdir(parents=True)
    expanded.write_bytes(b"INFO { TITLE: \"aliased\" }\n")
    reference = tmp_path / "reference"
    reference.mkdir()
    alias = reference / "F-G-contradiction-10.tlsf"
    alias.write_bytes(expanded.read_bytes())
    manifest = tmp_path / "tlsf-manifest.tsv"

    assert module.write_manifest(reference, submodule, manifest) == 1
    assert manifest.read_text().splitlines()[1].split("\t")[:2] == [
        alias.name,
        "direct:tlsf/family/F-G-contradiction-_pb_10_pe_.tlsf",
    ]

    out = tmp_path / "out"
    assert module.materialize(out, submodule, manifest, no_record=True) == 1
    assert (out / alias.name).read_bytes() == expanded.read_bytes()


@pytest.fixture
def materialization(tmp_path, monkeypatch):
    module = load_module()
    repo = tmp_path / "repo"
    repo.mkdir()
    monkeypatch.setattr(module, "ROOT", repo)
    submodule = tmp_path / "syntcomp-benchmarks"
    source = submodule / "tlsf" / "example.tlsf"
    source.parent.mkdir(parents=True)
    source.write_bytes(b'INFO { TITLE: "example" }\n')
    manifest = tmp_path / "tlsf-manifest.tsv"
    manifest.write_text(
        module.MANIFEST_HEADER + "\n"
        + "example.tlsf\tdirect:tlsf/example.tlsf\t"
        + hashlib.sha256(source.read_bytes()).hexdigest() + "\n"
    )
    return module, submodule, source, manifest


def test_materialize_records_verified_corpus(materialization, tmp_path, monkeypatch):
    module, submodule, source, manifest = materialization
    monkeypatch.chdir(tmp_path)
    out = pathlib.Path("relative corpus")

    assert module.materialize(out, submodule, manifest) == 1

    assert (out / source.name).read_bytes() == source.read_bytes()
    marker = json.loads((out / ".acacia-tlsf-corpus").read_text())
    assert marker["entries"] == 1
    assert marker["manifest_sha256"] == hashlib.sha256(manifest.read_bytes()).hexdigest()
    assert (module.ROOT / ".acacia-tlsf-corpus-path").read_text() == str(out.resolve()) + "\n"


@pytest.mark.parametrize("existing_pointer", [False, True])
def test_materialize_no_record_flag_preserves_pointer(
    materialization, tmp_path, monkeypatch, existing_pointer
):
    module, submodule, source, manifest = materialization
    out = tmp_path / "out"
    pointer = module.ROOT / ".acacia-tlsf-corpus-path"
    if existing_pointer:
        pointer.write_text("/previous/corpus\n")
    materialize = module.materialize
    monkeypatch.setattr(
        module, "materialize",
        lambda out, **kwargs: materialize(out, submodule, manifest, **kwargs),
    )
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), "materialize", "--out", str(out), "--no-record"])

    assert module.main() == 0

    assert (out / source.name).read_bytes() == source.read_bytes()
    assert json.loads((out / ".acacia-tlsf-corpus").read_text())["entries"] == 1
    if existing_pointer:
        assert pointer.read_text() == "/previous/corpus\n"
    else:
        assert not pointer.exists()


def test_failed_materialize_does_not_record_corpus(materialization, tmp_path):
    module, submodule, source, manifest = materialization
    source.write_bytes(b"corrupted\n")
    out = tmp_path / "out"
    pointer = module.ROOT / ".acacia-tlsf-corpus-path"
    pointer.write_text("/previous/corpus\n")

    with pytest.raises(module.CorpusError, match="hash mismatch: example.tlsf"):
        module.materialize(out, submodule, manifest)

    assert not (out / ".acacia-tlsf-corpus").exists()
    assert pointer.read_text() == "/previous/corpus\n"
