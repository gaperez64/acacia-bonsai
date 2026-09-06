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


def test_registry_options_are_self_describing():
    module = load_module()
    options, _ = module.load_registry()

    for name, option in options["options"].items():
        assert isinstance(option["meson"], str) and option["meson"], name
        assert option["type"] in {"boolean", "integer", "combo", "string"}, name
        assert "default" in option, name
        assert option["description"] and "\n" not in option["description"], name
        assert isinstance(option["macros"], list) and option["macros"], name
        for macro in option["macros"]:
            assert isinstance(macro["name"], str) and macro["name"], name
            assert macro["emit"] in {"always", "true", "nonempty", "not_equal"}, name
            assert "order" not in macro, (
                f"{name}: emission order is the registry's own order, not a "
                "second thing to maintain per macro"
            )
    for name, family in options["families"].items():
        assert isinstance(family["meson"], str) and family["meson"], name


def test_registry_combo_defaults_and_macro_maps_match_choices():
    module = load_module()
    options, _ = module.load_registry()

    for name, option in options["options"].items():
        if option["type"] == "combo":
            assert option["default"] in option["choices"], name
            for macro in option["macros"]:
                if macro["encoding"] == "map":
                    assert set(macro["map"]) == set(option["choices"]), name


def test_registry_meson_names_and_macro_names_are_unique():
    module = load_module()
    options, _ = module.load_registry()
    meson_names = [option["meson"] for option in options["options"].values()]
    meson_names += [family["meson"] for family in options["families"].values()]
    macro_names = [
        macro["name"]
        for option in options["options"].values()
        for macro in option["macros"]
    ]
    macro_names += [family["macro"] for family in options["families"].values()]

    assert len(meson_names) == len(set(meson_names))
    assert len(macro_names) == len(set(macro_names))


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

    # docker_default is the pointer that says which configurations we ship, and
    # it is meant to be repointed when the measurements say so.  Pin its shape,
    # not its membership: pinning the four names makes every reselection a test
    # failure, which is what this assertion used to do.
    docker_default = presets["groups"]["docker_default"]
    assert len(docker_default) == 4
    assert len(set(docker_default)) == 4
    for name in docker_default:
        assert name in presets["presets"]
        assert not name.endswith("_diag"), f"{name} is a diagnostic build"


def test_meson_args_emit_every_option_with_lowercase_booleans():
    module = load_module()
    options, presets = module.load_registry()
    values = module.normalize_preset(options, presets, "best_decomp_mona")

    args = module.meson_args(options, values)
    mapping = {name: option["meson"] for name, option in options["options"].items()}
    mapping.update(
        (name, family["meson"]) for name, family in options["families"].items()
    )

    assert len(args) == len(mapping)
    assert [arg.split("=", 1)[0] for arg in args] == [
        f"-D{meson_name}" for meson_name in mapping.values()
    ]
    for meson_name in mapping.values():
        prefix = f"-D{meson_name}="
        assert sum(arg.startswith(prefix) for arg in args) == 1
    for key, meson_name in mapping.items():
        if isinstance(values[key], bool):
            expected = "true" if values[key] else "false"
            assert f"-D{meson_name}={expected}" in args
    assert not any("=True" in arg or "=False" in arg for arg in args)
    assert "-Dacacia_enable_tlsf_frontend=true" in args


def test_preprocessor_flags_preserve_encodings_and_emission_order():
    module = load_module()
    options, _ = module.load_registry()
    values = module.defaults(options)
    values.update({
        "default_k": 6,
        "default_unreal_x": "formula",
        "default_spot_fast": "det_and_gfg",
        "translation_pref": "small+any",
        "enable_realizability_simplifier": False,
        "k_schedule": "geometric",
        "simd_is_max": False,
        "no_simd": True,
        "compile_all_components": True,
        "enable_diagnostics": True,
        "default_arms": "real:identity:backward,unreal:identity:forward",
        "local_certificate": True,
        "forward_safety_solver": True,
        "forward_conditional_covering": True,
        "vector_impl": "vector_backed",
    })

    expected = [
        "-DDEFAULT_K=6",
        "-DDEFAULT_KMIN=2",
        "-DDEFAULT_KINC=3",
        "-DDEFAULT_UNREAL_X=UNREAL_X_FORMULA",
        "-DDEFAULT_SPOT_FAST=SPOT_FAST_DET_AND_GFG",
        "-DACACIA_TRANSLATION_PREF=spot::postprocessor::Small",
        "-DACACIA_TRANSLATION_PREFS=spot::postprocessor::Small, spot::postprocessor::Any",
        "-DACACIA_ENABLE_REALIZABILITY_SIMPLIFIER=0",
        "-DACACIA_ENABLE_SYNTACTIC_BYPASS=1",
        "-DACACIA_FORCED_OUTPUT_CONTRADICTION=0",
        "-DACACIA_PROFILE_DOMINANCE=0",
        "-DACACIA_K_SCHEDULE=acacia::k_schedule::kind::geometric",
        "-DACACIA_ENABLE_TLSF_FRONTEND=1",
        "-DACACIA_EQUIVARIANT_MAX_STATES=512",
        "-DACACIA_EQUIVARIANT_MIN_CLIENTS=3",
        "-DACACIA_EQUIVARIANT_MIN_BLOCKS=2",
        "-DSIMD_IS_MAX=false",
        "-DDECOMPOSE_SPEC=1",
        "-DCPRE_AVOID_UNIONS=0",
        "-DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed",
        "-DNO_SIMD",
        "-DACACIA_COMPILE_ALL_COMPONENTS=1",
        "-DACACIA_ENABLE_DIAGNOSTICS=1",
        r'-DACACIA_DEFAULT_ARMS=\"real:identity:backward,unreal:identity:forward\"',
        "-DACACIA_LOCAL_CERTIFICATE=1",
        "-DACACIA_FORWARD_SAFETY_SOLVER=1",
        "-DACACIA_FORWARD_CONDITIONAL_COVERING=1",
        "-DACACIA_ENABLE_EQUIVARIANT_SOLVER=1",
        "-DVECTOR_IMPL=vector_backed",
    ]
    flags = module.preprocessor_flags(options, values)

    assert flags[:len(expected)] == expected
    assert flags[len(expected)] == "-DAUT_PREPROCESSOR=aut_preprocessors::surely_losing"


def test_preprocessor_flags_omit_disabled_conditional_macros():
    module = load_module()
    options, _ = module.load_registry()
    values = module.defaults(options)
    values["enable_equivariant_solver"] = False
    flags = module.preprocessor_flags(options, values)
    names = {flag[2:].split("=", 1)[0] for flag in flags}

    assert names.isdisjoint({
        "NO_SIMD",
        "ACACIA_COMPILE_ALL_COMPONENTS",
        "ACACIA_ENABLE_DIAGNOSTICS",
        "ACACIA_DEFAULT_ARMS",
        "ACACIA_LOCAL_CERTIFICATE",
        "ACACIA_FORWARD_SAFETY_SOLVER",
        "ACACIA_FORWARD_CONDITIONAL_COVERING",
        "ACACIA_ENABLE_EQUIVARIANT_SOLVER",
        "VECTOR_IMPL",
    })


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

    for name, option in options["options"].items():
        if option["type"] == "combo":
            presets = {"presets": {"bad": {name: "invalid"}}}
            with pytest.raises(SystemExit, match=f"bad: invalid {name}=invalid"):
                module.validate_preset(options, presets, "bad")
