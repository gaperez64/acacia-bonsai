import importlib.util
import pathlib
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "acacia-config.py"


def load_module():
    sys.path.insert(0, str(SCRIPT.parent))
    try:
        spec = importlib.util.spec_from_file_location("acacia_config", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_committed_registry_is_valid():
    module = load_module()
    options, presets = module.load_registry()

    for name in presets["presets"]:
        module.validate_preset(options, presets, name)
    for name in presets["tool_baselines"]:
        module.validate_tool(presets, name)


def test_normalize_preset_resolves_inheritance():
    module = load_module()
    options, presets = module.load_registry()

    values = module.normalize_preset(
        options, presets, "best_decomp_rank_bucketed_mona"
    )

    assert values["ios_precomputer"] == "mona"
    assert values["input_picker"] == "critical"
    assert values["vector_downset"] == "rank_bucketed_vector_backed"
    assert values["_preset"] == "best_decomp_rank_bucketed_mona"


def test_tlsf_frontend_default_flows_through_every_preset():
    module = load_module()
    options, presets = module.load_registry()

    # This shared default is what makes the retired *_tlsf presets redundant.
    for name in presets["presets"]:
        values = module.normalize_preset(options, presets, name)
        assert values["enable_tlsf_frontend"] is True


def test_all_groups_reference_existing_presets_and_docker_defaults_are_fixed():
    module = load_module()
    _, presets = module.load_registry()

    for group in presets["groups"].values():
        assert all(name in presets["presets"] for name in group)

    docker_default = presets["groups"]["docker_default"]
    assert len(docker_default) == 4
    assert docker_default == [
        "best_decomp_rank_bucketed_mona",
        "best_decomp_mona",
        "best_decomp_mona_any",
        "best_decomp_bboxtree_mona",
    ]


def test_meson_args_emit_every_option_with_lowercase_booleans():
    module = load_module()
    options, presets = module.load_registry()
    values = module.normalize_preset(options, presets, "best_decomp_mona")

    args = module.meson_args(values)

    assert len(args) == len(module.MESON_OPTION_NAMES)
    for meson_name in module.MESON_OPTION_NAMES.values():
        prefix = f"-D{meson_name}="
        assert sum(arg.startswith(prefix) for arg in args) == 1
    for key, meson_name in module.MESON_OPTION_NAMES.items():
        if isinstance(values[key], bool):
            expected = "true" if values[key] else "false"
            assert f"-D{meson_name}={expected}" in args
    assert not any("=True" in arg or "=False" in arg for arg in args)
    assert "-Dacacia_enable_tlsf_frontend=true" in args


def test_unknown_preset_is_rejected():
    module = load_module()
    options, presets = module.load_registry()

    with pytest.raises(SystemExit, match="unknown Acacia preset"):
        module.preset_data(presets, "does_not_exist")
    with pytest.raises(SystemExit, match="unknown Acacia preset"):
        module.normalize_preset(options, presets, "does_not_exist")


def test_preset_inheritance_cycle_is_rejected():
    module = load_module()
    options, _ = module.load_registry()
    presets = {
        "presets": {
            "a": {"inherits": "b"},
            "b": {"inherits": "a"},
        }
    }

    with pytest.raises(SystemExit, match=r"preset inheritance cycle: a -> b -> a"):
        module.normalize_preset(options, presets, "a")


def test_validate_preset_rejects_unknown_option():
    module = load_module()
    options, _ = module.load_registry()
    presets = {"presets": {"bad": {"unknown_option": True}}}

    with pytest.raises(SystemExit, match="bad: unknown option unknown_option"):
        module.validate_preset(options, presets, "bad")


def test_validate_preset_rejects_invalid_family_value():
    module = load_module()
    options, _ = module.load_registry()
    presets = {"presets": {"bad": {"ios_precomputer": "invalid"}}}

    with pytest.raises(SystemExit, match="bad: invalid ios_precomputer=invalid"):
        module.validate_preset(options, presets, "bad")


def test_validate_preset_rejects_invalid_choice_value():
    module = load_module()
    options, _ = module.load_registry()
    presets = {"presets": {"bad": {"translation_pref": "invalid"}}}

    with pytest.raises(SystemExit, match="bad: invalid translation_pref=invalid"):
        module.validate_preset(options, presets, "bad")
