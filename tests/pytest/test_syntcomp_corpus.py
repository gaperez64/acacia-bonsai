from __future__ import annotations

import hashlib
import importlib.util
import pathlib


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
        + f"example.tlsf\tdirect:tlsf/family/example.tlsf\t"
        + hashlib.sha256(good).hexdigest()
        + "\n"
    )

    source.write_bytes(b"INFO { TITLE: \"silently-corrupted\" }\n")
    out = tmp_path / "out"
    try:
        module.materialize(out, submodule=submodule, manifest=manifest)
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
    assert module.materialize(out, submodule, manifest) == 1
    assert (out / alias.name).read_bytes() == expanded.read_bytes()
