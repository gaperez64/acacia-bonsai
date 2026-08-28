import importlib.util
import pathlib
import sys
from types import SimpleNamespace


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "run-subset.py"


def load_module():
    sys.path.insert(0, str(SCRIPT.parent))
    try:
        spec = importlib.util.spec_from_file_location("run_subset", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_read_instance_list_ignores_comments_and_blank_lines(tmp_path):
    manifest = tmp_path / "panel.list"
    manifest.write_text(
        "# generated manifest\n\nfirst.ltl\n  # indented comment\n second.ltl \n"
    )

    assert load_module().read_instance_list(manifest) == ["first.ltl", "second.ltl"]


def test_default_source_map_is_derived_from_repository():
    module = load_module()

    assert module.DEFAULT_SOURCE_MAP == (
        ROOT / "tests/suites/benchmarks/syntcomp24/sources.tsv"
    )


def test_read_tlsf_map_accepts_headered_input(tmp_path):
    tlsf_map = tmp_path / "tlsf-sources.tsv"
    tlsf_map.write_text("instance\ttlsf\nAlarm.ltl\tAlarm.tlsf\n")

    assert load_module().read_tlsf_map(tlsf_map) == {"Alarm.ltl": "Alarm.tlsf"}


def test_read_tlsf_map_accepts_headerless_input(tmp_path):
    tlsf_map = tmp_path / "ad-hoc.tsv"
    tlsf_map.write_text("Alarm.ltl\tAlarm.tlsf\n")

    assert load_module().read_tlsf_map(tlsf_map) == {"Alarm.ltl": "Alarm.tlsf"}


def test_read_tlsf_map_keeps_paths_relative_without_corpus(tmp_path):
    tlsf_map = tmp_path / "ad-hoc.tsv"
    tlsf_map.write_text("Alarm.ltl\tnested/Alarm.tlsf\n")

    assert load_module().read_tlsf_map(tlsf_map)["Alarm.ltl"] == "nested/Alarm.tlsf"


def test_read_tlsf_map_resolves_paths_from_corpus(tmp_path):
    tlsf_map = tmp_path / "tlsf-sources.tsv"
    tlsf_map.write_text("instance\ttlsf\nAlarm.ltl\tnested/Alarm.tlsf\n")
    corpus = tmp_path / "corpus"

    assert load_module().read_tlsf_map(tlsf_map, corpus)["Alarm.ltl"] == str(
        corpus / "nested/Alarm.tlsf"
    )


def test_parse_tlsf_semantics_is_order_insensitive():
    parse = load_module()._parse_tlsf_semantics

    assert parse("Strict,Mealy") == ("Mealy", True)
    assert parse("Moore, Strict") == ("Moore", True)
    assert parse(" Mealy ") == ("Mealy", False)
    assert parse("Strict") is None
    assert parse("Mealy,Moore") is None
    assert parse("Mealy,Weak") is None


def test_convert_tlsf_returns_declared_model_without_overwriting_semantics(tmp_path):
    module = load_module()
    tlsf = tmp_path / "example.tlsf"
    tlsf.write_text("INFO {}\n")
    output_dir = tmp_path / "converted"
    output_dir.mkdir()
    log = tmp_path / "syfco.log"
    syfco = tmp_path / "syfco"
    syfco.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib\n"
        "import sys\n"
        f"log = pathlib.Path({str(log)!r})\n"
        "with log.open('a') as stream:\n"
        "    stream.write(' '.join(sys.argv[1:]) + '\\n')\n"
        "if sys.argv[1] == '--print-semantics':\n"
        "    print('Strict,Moore')\n"
        "else:\n"
        "    part = pathlib.Path(sys.argv[sys.argv.index('--part-file') + 1])\n"
        "    part.write_text('.inputs: i\\n.outputs: o\\n')\n"
        "    print('G(i -> o)')\n"
    )
    syfco.chmod(0o755)

    expected = (output_dir / "example.ltl", "Moore")
    assert module.convert_tlsf(str(syfco), tlsf, output_dir) == expected
    assert module.convert_tlsf(str(syfco), tlsf, output_dir) == expected

    calls = log.read_text().splitlines()
    assert calls == [
        f"--print-semantics {tlsf}",
        "--format ltlxba --mode fully "
        f"--part-file {output_dir / 'example.part'} {tlsf}",
        f"--print-semantics {tlsf}",
    ]
    assert "--overwrite-semantics" not in log.read_text()


def test_resource_limit_is_not_reported_as_unknown():
    module = load_module()
    run = SimpleNamespace(
        timed_out=False,
        resource_limited=True,
        stdout="UNKNOWN\n",
        stderr="",
        returncode=3,
    )

    assert module.classify_run(run) == "RESOURCE_LIMIT"


def test_failed_run_is_not_accepted_as_a_printed_verdict():
    module = load_module()
    run = SimpleNamespace(
        timed_out=False,
        resource_limited=False,
        stdout="REALIZABLE\n",
        stderr="solver failed after printing",
        returncode=3,
    )

    assert module.classify_run(run) == "ERROR"


def test_unknown_requires_the_documented_exit_code():
    module = load_module()
    run = SimpleNamespace(
        timed_out=False,
        resource_limited=False,
        stdout="UNKNOWN\n",
        stderr="",
        returncode=2,
    )

    assert module.classify_run(run) == "UNKNOWN"
