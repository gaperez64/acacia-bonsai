from __future__ import annotations

import csv
import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCHMARKS = ROOT / "tests" / "suites" / "benchmarks"
MANIFEST = BENCHMARKS / "tlsf-manifest.tsv"
EXPAND_LISTS = ROOT / "tests" / "suites" / "expand-lists.py"


def load_expand_lists():
    spec = importlib.util.spec_from_file_location("expand_lists", EXPAND_LISTS)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def fail(path: pathlib.Path, line: int, instance: str, detail: str) -> None:
    raise AssertionError(f"{path}:{line}: instance {instance!r}: {detail}")


def manifest_instances() -> set[str]:
    with MANIFEST.open(newline="", encoding="utf-8") as handle:
        return {row["instance"] for row in csv.DictReader(handle, delimiter="\t")}


def instance_location(
    folder: pathlib.Path, instance: str
) -> tuple[pathlib.Path, int]:
    """Find a source location for an entry returned by expand-lists.py."""
    for list_file in sorted(folder.glob("*.list")):
        for line_number, raw in enumerate(
            list_file.read_text(encoding="utf-8").splitlines(), 1
        ):
            if raw.split("#", 1)[0].strip() == instance:
                return list_file, line_number
    raise AssertionError(f"could not locate expanded instance {instance!r} in {folder}")


def check_tlsf_sources(path: pathlib.Path) -> None:
    """Validate one suite's TLSF source map and its list coverage."""
    lines = path.read_text(encoding="utf-8").splitlines()
    expected_header = "instance\ttlsf"
    if not lines or lines[0] != expected_header:
        fail(path, 1, "<header>", f"expected header {expected_header!r}")

    available_tlsf = manifest_instances()
    tlsf_sources: dict[str, str] = {}
    first_lines: dict[str, int] = {}
    for line_number, raw in enumerate(lines[1:], 2):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        instance = fields[0]
        if len(fields) != 2:
            fail(path, line_number, instance, "expected two tab-separated fields")
        instance, tlsf = fields
        if (
            pathlib.PurePosixPath(instance).name != instance
            or not instance.endswith(".ltl")
        ):
            fail(path, line_number, instance, "expected a plain .ltl filename")
        if instance in tlsf_sources:
            fail(
                path,
                line_number,
                instance,
                f"duplicate instance (first mapped on line {first_lines[instance]})",
            )

        expected_tlsf = f"{instance[:-len('.ltl')]}.tlsf"
        if tlsf != expected_tlsf:
            fail(
                path,
                line_number,
                instance,
                f"maps to {tlsf!r}; expected {expected_tlsf!r}",
            )
        if tlsf not in available_tlsf:
            fail(path, line_number, instance, f"{tlsf!r} is absent from {MANIFEST}")

        tlsf_sources[instance] = tlsf
        first_lines[instance] = line_number

    expand_lists = load_expand_lists()
    sources_path = path.parent / "sources.tsv"
    sources = (
        expand_lists.parse_sources(sources_path) if sources_path.is_file() else {}
    )
    for list_file in sorted(path.parent.glob("*.list")):
        for instance in expand_lists.parse_list(list_file):
            if instance not in sources and instance not in tlsf_sources:
                source_file, line_number = instance_location(path.parent, instance)
                fail(
                    source_file,
                    line_number,
                    instance,
                    f"does not resolve through {sources_path.name} or {path.name}",
                )


def test_tlsf_source_maps():
    maps = sorted(BENCHMARKS.rglob("tlsf-sources.tsv"))
    assert maps, f"no tlsf-sources.tsv maps found under {BENCHMARKS}"
    for path in maps:
        check_tlsf_sources(path)


def test_tlsf_source_map_rejects_swapped_specification(tmp_path):
    source = BENCHMARKS / "syntcomp25" / "tlsf-sources.tsv"
    original = "gf-unreal2.ltl\tgf-unreal2.tlsf"
    mutated = "gf-unreal2.ltl\tgf-unreal35.tlsf"
    text = source.read_text(encoding="utf-8")
    assert text.count(original) == 1, f"expected one {original!r} row in {source}"

    path = tmp_path / "tlsf-sources.tsv"
    path.write_text(text.replace(original, mutated, 1), encoding="utf-8")
    line_number = text[: text.index(original)].count("\n") + 1

    try:
        check_tlsf_sources(path)
    except AssertionError as error:
        message = str(error)
        assert f"{path}:{line_number}: instance 'gf-unreal2.ltl'" in message
        assert "expected 'gf-unreal2.tlsf'" in message
    else:
        assert False, "TLSF source-map validation accepted a swapped specification"
