import importlib.util
import pathlib
import re
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "acacia-config.py"

NEW_OPTION_CASES = [
    ("symmetry_profile", "ACACIA_SYMMETRY_PROFILE", False, True, "1"),
    ("equivariant_max_output_letters", "ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS",
     4096, 8192, "8192"),
    ("equivariant_max_orbits", "ACACIA_EQUIVARIANT_MAX_ORBITS", 4096, 8192, "8192"),
    ("equivariant_max_sweep_clients", "ACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS", 4, 0, "0"),
    ("equivariant_exhaustive_detect", "ACACIA_EQUIVARIANT_EXHAUSTIVE_DETECT",
     False, True, "1"),
    ("equivariant_validate_fast_recognition", "ACACIA_EQUIVARIANT_VALIDATE_FAST_RECOGNITION",
     False, True, "1"),
    ("symmetry_verbose_diagnostics", "ACACIA_SYMMETRY_VERBOSE_DIAGNOSTICS",
     False, True, "1"),
    ("ltl_frontend", "ACACIA_LTL_FRONTEND", "baseline", "mp_nba", "ACACIA_LTL_FRONTEND_MP_NBA"),
    ("forward_eager_minimal_successors", "ACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS",
     False, True, "1"),
    ("transition_acceptance", "ACACIA_TRANSITION_ACCEPTANCE", False, True, "1"),
]


def load_module(script=SCRIPT):
    sys.path.insert(0, str(script.parent))
    try:
        spec = importlib.util.spec_from_file_location("acacia_config", script)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def test_committed_registry_is_valid():
    module = load_module()
    options, presets = module.load_registry()

    module.command_validate(options, presets)


def test_every_solver_meson_option_has_a_registry_entry():
    module = load_module()
    options, _ = module.load_registry()
    frontends = load_module(ROOT / "tests" / "check-config-frontends.py")
    # Collect names independently of the default parser: an unparsed declaration
    # must not silently disappear from the coverage check.
    declared = set(re.findall(
        r"^\s*option\s*\(\s*'([^']+)'", (ROOT / "meson.options").read_text(), re.M
    ))
    registered = {
        entry["meson"]
        for section in ("options", "families")
        for entry in options[section].values()
    }
    assert frontends.NON_SOLVER_OPTIONS <= declared
    assert registered.isdisjoint(frontends.NON_SOLVER_OPTIONS)
    assert declared - frontends.NON_SOLVER_OPTIONS == registered
    assert set(frontends.meson_options(ROOT / "meson.options")) == declared


def test_registry_meson_types_choices_and_bounds_agree():
    module = load_module()
    options, _ = module.load_registry()
    frontends = load_module(ROOT / "tests" / "check-config-frontends.py")
    meson = frontends.meson_options(ROOT / "meson.options")
    for option in options["options"].values():
        declared = meson[option["meson"]]
        for key in ("type", "choices", "min", "max"):
            assert option.get(key) == declared.get(key), (option["meson"], key)
        if option["meson"] not in frontends.INTENTIONAL_DIVERGENCE:
            assert option["default"] == declared["default"], option["meson"]
    for family in options["families"].values():
        declared = meson[family["meson"]]
        assert declared["type"] == "combo"
        assert family["default"] == declared["default"], family["meson"]
        assert set(family["choices"]) == set(declared["choices"]), family["meson"]


@pytest.mark.parametrize("name,macro,default,selected,encoded", NEW_OPTION_CASES)
def test_new_options_preserve_defaults_and_flow_through_frontends(
    tmp_path, name, macro, default, selected, encoded,
):
    module = load_module()
    options, _ = module.load_registry()
    option = options["options"][name]
    assert option["default"] == default
    expected_default = (
        "ACACIA_LTL_FRONTEND_BASELINE" if name == "ltl_frontend" else str(int(default))
    )
    assert f"-D{macro}={expected_default}" in module.preprocessor_flags(
        options, module.defaults(options)
    )
    if isinstance(default, bool):
        assert option["type"] == "boolean"
        assert option["macros"] == [
            {"name": macro, "emit": "always", "encoding": "bool_int"}
        ]

    presets = {"presets": {"custom": {name: selected}}}
    module.validate_preset(options, presets, "custom")
    values = module.normalize_preset(options, presets, "custom")
    meson_value = str(selected).lower() if isinstance(selected, bool) else str(selected)
    assert f"-D{option['meson']}={meson_value}" in module.meson_args(options, values)
    assert f"-D{macro}={encoded}" in module.preprocessor_flags(options, values)

    header = tmp_path / "acacia_build_config.hh"
    module.emit_config_header(options, values, header)
    assert f"#ifndef {macro}\n# define {macro} {encoded}\n#endif" in header.read_text()
    template = (ROOT / "src/config/acacia_build_config.hh.in").read_text()
    assert f"#ifndef {macro}\n# define {macro} @{macro}@\n#endif" in template


def test_new_equivariant_options_match_existing_header_fallbacks():
    module = load_module()
    options, _ = module.load_registry()
    configuration = (ROOT / "src/configuration.hh").read_text()
    for name, macro, _, _, _ in NEW_OPTION_CASES[:7]:
        default = int(options["options"][name]["default"])
        assert f"#ifndef {macro}\n# define {macro} {default}\n#endif" in configuration


def test_transition_core_preset_selects_type_and_exactly_one_family_gate():
    module = load_module()
    options, _ = module.load_registry()
    presets = {"presets": {"custom": {"boolean_states": "transition_core"}}}
    module.validate_preset(options, presets, "custom")
    values = module.normalize_preset(options, presets, "custom")
    flags = module.preprocessor_flags(options, values)
    assert "-Dacacia_boolean_states=transition_core" in module.meson_args(options, values)
    assert "-DBOOLEAN_STATES=boolean_states::transition_core" in flags
    assert "-DACACIA_ENABLE_BOOLEAN_STATES_TRANSITION_CORE=1" in flags
    assert "-DACACIA_ENABLE_BOOLEAN_STATES_FORWARD_SATURATION=0" in flags
    assert "-DACACIA_ENABLE_BOOLEAN_STATES_NO_BOOLEAN_STATES=0" in flags
    default_flags = module.preprocessor_flags(options, module.defaults(options))
    assert "-DACACIA_ENABLE_BOOLEAN_STATES_TRANSITION_CORE=0" in default_flags


def test_constants_are_documentation_only(tmp_path):
    module = load_module()
    options, presets = module.load_registry()
    module.command_validate(options, presets)
    constants = options["constants"]
    assert set(constants) == {
        "VECTOR_ELT_T", "ACACIA_LTL_FRONTEND_BASELINE",
        "ACACIA_LTL_FRONTEND_MP_NBA", "VECTOR_IMPL_AUTO",
    }
    values = module.defaults(options)
    assert set(constants).isdisjoint(values)
    flags = module.preprocessor_flags(options, values)
    args = module.meson_args(options, values)
    header = tmp_path / "acacia_build_config.hh"
    module.emit_config_header(options, values, header)
    original_header = header.read_text()
    for constant in constants.values():
        assert all((ROOT / source).is_file() for source in constant["sources"])
        constant["value"] = "documentation changes do not affect builds"
    assert module.defaults(options) == values
    assert module.preprocessor_flags(options, values) == flags
    assert module.meson_args(options, values) == args
    module.emit_config_header(options, values, header)
    assert header.read_text() == original_header


@pytest.mark.parametrize("constants,reason", [
    ([], "constants must be an object"),
    ({"bad": 0}, "constants must contain named objects"),
    ({"bad": {"value": []}}, "constant value"),
    ({"bad": {"value": 0, "description": ""}}, "constant description"),
    ({"bad": {"value": 0, "description": "constant", "sources": "src/configuration.hh"}},
     "constant sources"),
])
def test_validate_checks_constant_documentation(constants, reason):
    module = load_module()
    options, presets = module.load_registry()
    options["constants"] = constants
    with pytest.raises(SystemExit, match=reason):
        module.command_validate(options, presets)


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
        "-DACACIA_LTL_FRONTEND=ACACIA_LTL_FRONTEND_BASELINE",
        "-DACACIA_ENABLE_REALIZABILITY_SIMPLIFIER=0",
        "-DACACIA_ENABLE_SYNTACTIC_BYPASS=1",
        "-DACACIA_FORCED_OUTPUT_CONTRADICTION=0",
        "-DACACIA_PROFILE_DOMINANCE=0",
        "-DACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS=0",
        "-DACACIA_K_SCHEDULE=acacia::k_schedule::kind::geometric",
        "-DACACIA_ENABLE_TLSF_FRONTEND=1",
        "-DACACIA_EQUIVARIANT_MAX_STATES=512",
        "-DACACIA_SYMMETRY_PROFILE=0",
        "-DACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS=4096",
        "-DACACIA_EQUIVARIANT_MAX_ORBITS=4096",
        "-DACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS=4",
        "-DACACIA_EQUIVARIANT_EXHAUSTIVE_DETECT=0",
        "-DACACIA_EQUIVARIANT_VALIDATE_FAST_RECOGNITION=0",
        "-DACACIA_SYMMETRY_VERBOSE_DIAGNOSTICS=0",
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
        "-DACACIA_TRANSITION_ACCEPTANCE=0",
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
