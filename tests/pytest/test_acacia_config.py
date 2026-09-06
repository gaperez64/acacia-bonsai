import copy
import hashlib
import importlib.util
import json
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


def test_every_preset_has_its_own_description_and_role():
    module = load_module()
    options, presets = module.load_registry()
    for name, data in presets["presets"].items():
        assert isinstance(data["description"], str) and data["description"].strip(), name
        assert data["description"].splitlines() == [data["description"]], name
        assert data["role"] in {"shipping", "reference", "sweep", "diagnostic", "legacy"}, name
        if module.normalize_preset(options, presets, name)["enable_diagnostics"]:
            assert data["role"] == "diagnostic", name

    assert presets["presets"]["best_decomp_mona"]["role"] == "reference"
    for group in ("posets_downset_sweep", "local_tuning_default"):
        for name in presets["groups"][group]:
            # The plain-vector reference and shipped translation arm also
            # participate in these comparisons; their primary roles take precedence.
            if name != "best_decomp_mona" and name not in presets["groups"]["docker_default"]:
                assert presets["presets"][name]["role"] == "sweep", name


@pytest.mark.parametrize("key,value", [
    ("description", None), ("description", ""), ("description", "  "),
    ("description", "two\nlines"), ("description", "trailing newline\n"),
    ("description", 42), ("role", None), ("role", "fastest"), ("role", []),
])
def test_validate_requires_valid_metadata_on_each_preset(key, value):
    module = load_module()
    options, presets = module.load_registry()
    name = "best_decomp_rank_bucketed_mona"
    if value is None:
        del presets["presets"][name][key]
    else:
        presets["presets"][name][key] = value
    with pytest.raises(SystemExit, match=f"{name}: {key} must be"):
        module.command_validate(options, presets)


def test_metadata_never_changes_resolved_options_or_hashes():
    module = load_module()
    options, presets = module.load_registry()
    without_metadata = copy.deepcopy(presets)
    for data in without_metadata["presets"].values():
        del data["description"]
        del data["role"]

    for name in presets["presets"]:
        expected = module.normalize_preset(options, without_metadata, name)
        values = module.normalize_preset(options, presets, name)
        assert {"description", "role", "inherits"}.isdisjoint(values), name
        assert values == expected, name
        assert module.stable_hash(values) == module.stable_hash(expected), name
        module.validate_preset(options, presets, name)


def test_shipping_roles_match_docker_default_exactly():
    module = load_module()
    _, presets = module.load_registry()
    shipping = {name for name, data in presets["presets"].items() if data["role"] == "shipping"}
    assert shipping == set(presets["groups"]["docker_default"])


@pytest.mark.parametrize("promote", [False, True])
def test_validate_rejects_shipping_role_drift(promote):
    module = load_module()
    options, presets = module.load_registry()
    name = "base" if promote else presets["groups"]["docker_default"][0]
    presets["presets"][name]["role"] = "shipping" if promote else "reference"
    with pytest.raises(SystemExit, match="role shipping must match docker_default membership"):
        module.command_validate(options, presets)


def test_naming_lint_accepts_all_grandfathered_names():
    module = load_module()
    _, presets = module.load_registry()
    grandfathered = presets["naming"]["grandfathered"]
    assert len(grandfathered) == len(set(grandfathered)) == 43
    assert set(grandfathered) <= set(presets["presets"]) | set(presets["aliases"])
    # Make the limits deliberately impossible for these names: the exemption
    # must cover both length and tokens, including the bare name 'best'.
    presets["presets"] = dict.fromkeys(grandfathered)
    presets["naming"]["max_length"] = 1
    presets["naming"]["forbidden_tokens"] += ["base"]
    module.validate_naming(presets)


@pytest.mark.parametrize("name,reason", [
    ("best_new", "forbidden token"), ("new_best", "forbidden token"),
    ("new-best-arm", "forbidden token"), ("BEST_new", "forbidden token"),
    (None, "exceeds max_length"),
])
def test_naming_lint_rejects_new_claims_and_long_names(name, reason):
    module = load_module()
    options, presets = module.load_registry()
    if name is None:
        name = "x" * (presets["naming"]["max_length"] + 1)
    presets["presets"][name] = {
        "inherits": "base", "description": "Compare a new configuration.", "role": "sweep",
    }
    with pytest.raises(SystemExit, match=reason) as exc:
        module.command_validate(options, presets)
    message = str(exc.value)
    assert "a claim no test can keep honest" in message
    assert "groups" in message and "docker_default" in message and "repointed" in message


def test_naming_lint_accepts_short_new_names_and_complete_tokens():
    module = load_module()
    options, presets = module.load_registry()
    for name in ("forward_probe", "bestow", "x" * presets["naming"]["max_length"]):
        presets["presets"][name] = {
            "inherits": "base", "description": "Compare a new configuration.", "role": "sweep",
        }
    module.command_validate(options, presets)


def test_alias_lookup_uses_the_target_identity_and_hash(capsys):
    module = load_module()
    options, presets = module.load_registry()
    name = "best_decomp_rank_bucketed_mona"
    presets["aliases"]["old_label"] = name
    module.command_validate(options, presets)
    assert module.preset_data(presets, "old_label") == presets["presets"][name]
    values = module.normalize_preset(options, presets, "old_label")
    expected = module.normalize_preset(options, presets, name)
    assert values == expected
    assert values["_preset"] == name
    assert module.stable_hash(values) == module.stable_hash(expected)
    captured = capsys.readouterr()
    assert captured.out == ""
    assert f"alias old_label resolves to {name}" in captured.err


@pytest.mark.parametrize("aliases,reason", [
    ({"base": "best_decomp_mona"}, "alias collides with a real preset name"),
    ({"old_label": "missing"}, "alias target is not an existing preset"),
    ({"old_label": "other_alias", "other_alias": "base"}, "alias target is not an existing preset"),
    ({"old_label": []}, "alias target is not an existing preset"),
    ({"": "base"}, "alias names must be non-empty strings"),
    ([], "aliases must be an object"),
])
def test_validate_rejects_invalid_alias_tables(aliases, reason):
    module = load_module()
    options, presets = module.load_registry()
    presets["aliases"] = aliases
    with pytest.raises(SystemExit, match=reason):
        module.command_validate(options, presets)


def test_aliases_work_in_inheritance_and_groups_and_detect_cycles():
    module = load_module()
    options, presets = module.load_registry()
    presets["aliases"]["old_base"] = "base"
    presets["presets"]["child"] = {
        "inherits": "old_base", "description": "Compare with registry defaults.", "role": "sweep",
    }
    presets["groups"]["alias_group"] = ["old_base", "child"]
    module.command_validate(options, presets)
    assert module.normalize_preset(options, presets, "child") == {
        **module.normalize_preset(options, presets, "base"), "_preset": "child", "preset": "child",
    }
    presets["presets"]["base"]["inherits"] = "child"
    with pytest.raises(SystemExit, match="preset inheritance cycle: child -> base -> child"):
        module.normalize_preset(options, presets, "child")


@pytest.mark.parametrize("arguments", [
    ["show", "old_label"], ["show", "old_label", "--describe"],
    ["hash", "old_label"], ["meson-args", "old_label"],
])
def test_cli_alias_outputs_match_target_and_note_stays_on_stderr(arguments, monkeypatch, capsys):
    module = load_module()
    options, presets = module.load_registry()
    name = "best_decomp_rank_bucketed_mona"
    presets["aliases"]["old_label"] = name
    monkeypatch.setattr(module, "load_registry", lambda: (options, presets))
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), *arguments])
    assert module.main() == 0
    actual = capsys.readouterr()
    target_arguments = [name if arg == "old_label" else arg for arg in arguments]
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), *target_arguments])
    assert module.main() == 0
    expected = capsys.readouterr()
    assert actual.out == expected.out
    assert actual.err == f"note: Acacia preset alias old_label resolves to {name}\n"
    assert expected.err == ""


def test_list_presets_plain_and_long_output(monkeypatch, capsys):
    module = load_module()
    _, presets = module.load_registry()
    names = sorted(presets["presets"])
    monkeypatch.setattr(sys, "argv", [str(SCRIPT), "list-presets"])
    assert module.main() == 0
    plain = capsys.readouterr()
    assert plain.out == "".join(name + "\n" for name in names)
    assert plain.err == ""

    monkeypatch.setattr(sys, "argv", [str(SCRIPT), "list-presets", "--long"])
    assert module.main() == 0
    detailed = capsys.readouterr()
    rows = detailed.out.splitlines()
    assert len(rows) == len(names)
    role_columns, description_columns = set(), set()
    for name, row in zip(names, rows):
        data = presets["presets"][name]
        assert row.split(maxsplit=2) == [name, data["role"], data["description"]]
        role_columns.add(row.index(data["role"], len(name)))
        description_columns.add(row.index(data["description"]))
    assert len(role_columns) == len(description_columns) == 1
    assert detailed.err == ""


def test_show_describe_keeps_metadata_outside_the_fingerprint(monkeypatch, capsys):
    module = load_module()
    options, presets = module.load_registry()
    for name, data in presets["presets"].items():
        monkeypatch.setattr(sys, "argv", [str(SCRIPT), "show", name])
        assert module.main() == 0
        plain = capsys.readouterr()
        values = {key: value for key, value in
                  module.normalize_preset(options, presets, name).items() if key != "preset"}
        assert plain.out == json.dumps(values, sort_keys=True, indent=2) + "\n"
        assert plain.err == ""
        monkeypatch.setattr(sys, "argv", [str(SCRIPT), "show", name, "--describe"])
        assert module.main() == 0
        detailed = capsys.readouterr()
        assert json.loads(detailed.out) == {
            "description": data["description"], "role": data["role"], "options": values,
        }
        assert detailed.err == ""


def test_preset_provenance_is_excluded_from_hash_and_show(monkeypatch, capsys):
    module = load_module()
    options, presets = module.load_registry()
    for name in presets["presets"]:
        values = module.normalize_preset(options, presets, name)
        # Compute the historical fingerprint independently of the helper under test.
        historical = {key: value for key, value in values.items() if key != "preset"}
        expected_hash = hashlib.sha256(
            json.dumps(historical, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()[:12]
        for recorded in ("", name, "another_configuration"):
            assert module.stable_hash({**values, "preset": recorded}) == expected_hash
        for command, expected in (
            ("hash", expected_hash + "\n"),
            ("show", json.dumps(historical, sort_keys=True, indent=2) + "\n"),
        ):
            monkeypatch.setattr(sys, "argv", [str(SCRIPT), command, name])
            assert module.main() == 0
            assert capsys.readouterr().out == expected


def test_named_presets_emit_provenance_and_plain_defaults_omit_it(monkeypatch, capsys):
    module = load_module()
    options, presets = module.load_registry()
    defaults = module.defaults(options)
    assert defaults["preset"] == ""
    assert "-Dacacia_preset=" in module.meson_args(options, defaults)
    assert not any(flag.startswith("-DACACIA_PRESET")
                   for flag in module.preprocessor_flags(options, defaults))
    for name in presets["presets"]:
        values = module.normalize_preset(options, presets, name)
        assert values["preset"] == name
        assert f'-DACACIA_PRESET=\\"{name}\\"' in module.preprocessor_flags(options, values)
        monkeypatch.setattr(sys, "argv", [str(SCRIPT), "meson-args", name])
        assert module.main() == 0
        args = capsys.readouterr().out.split()
        assert [arg for arg in args if arg.startswith("-Dacacia_preset=")] == [
            f"-Dacacia_preset={name}",
        ]


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
    name, macro, default, selected, encoded,
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

    template = (ROOT / "src/config/acacia_build_config.hh.in").read_text()
    assert f"#ifndef {macro}\n# define {macro} @{macro}@\n#endif" in template


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


def test_constants_are_documentation_only():
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
    for constant in constants.values():
        assert all((ROOT / source).is_file() for source in constant["sources"])
        constant["value"] = "documentation changes do not affect builds"
    assert module.defaults(options) == values
    assert module.preprocessor_flags(options, values) == flags
    assert module.meson_args(options, values) == args


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
