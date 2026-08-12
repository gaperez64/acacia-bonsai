import importlib.util
import pathlib
import sys


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


def test_default_instances_dir_is_derived_from_repository():
    module = load_module()

    assert module.DEFAULT_INSTANCES_DIR == ROOT / "tests/ltl/syntcomp24"
