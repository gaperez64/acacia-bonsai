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




def test_gr1_prepass_preset_is_five_arms():
    module = load_module()
    options, presets = module.load_registry()
    candidate = module.normalize_preset(options, presets,
                                        "otf_sparse_formula_gr1_prepass")
    assert candidate["default_arms"].split(",") == [
        "real:small:backward",
        "real:small:forward",
        "unreal:formula:spot-guarded-sparse",
        "unreal:automaton:forward",
        "both:gr1-lift:oxidd",
    ]


def test_gr1_real_prepass_preset_is_six_arms():
    module = load_module()
    options, presets = module.load_registry()
    shipping = presets["groups"]["docker_default"]
    parent = module.normalize_preset(options, presets, shipping[2])
    name = shipping[1]
    data = presets["presets"][name]
    assert data["role"] == "shipping"
    assert name in presets["groups"]["docker_default"]
    candidate = module.normalize_preset(options, presets, name)
    assert candidate["default_arms"].split(",") == [
        "real:small:backward",
        "real:small:forward",
        "unreal:formula:spot-guarded-sparse",
        "unreal:automaton:forward",
        "both:gr1-real-lift:oxidd",
        "both:obligation:spot",
    ]
    assert {
        key for key in candidate if key not in {"_preset", "preset"} and candidate[key] != parent[key]
    } == {"default_arms"}


def test_obligation_membership_is_explicit_in_shipping_portfolios():
    module = load_module()
    options, presets = module.load_registry()
    shipping = presets["groups"]["docker_default"]
    arm = "both:obligation:spot"
    for name, data in presets["presets"].items():
        values = module.normalize_preset(options, presets, name)
        arms = values["default_arms"].split(",")
        if name in shipping and data.get("default_arms"):
            assert arms.count(arm) == 1, name
            assert f"-Dacacia_default_arms={values['default_arms']}" in module.meson_args(
                options, values)
        else:
            assert arm not in arms, name
    fallback = module.normalize_preset(
        options, presets, presets["groups"]["docker_default"][2])
    assert arm in fallback["default_arms"].split(",")
    assert options["options"]["default_arms"]["default"] == ""


def test_first_shipping_preset_adds_only_parity_to_the_previous_default():
    module = load_module()
    options, presets = module.load_registry()
    shipping = presets["groups"]["docker_default"]
    name, previous = shipping[:2]
    assert presets["presets"][name]["role"] == "shipping"
    baseline = module.normalize_preset(options, presets, previous)
    candidate = module.normalize_preset(options, presets, name)
    assert candidate["default_arms"] == baseline["default_arms"] + ",both:parity:spot"
    for key in baseline:
        if key not in {"_preset", "preset", "default_arms"}:
            assert candidate[key] == baseline[key], key


def test_docker_launcher_uses_registry_group_without_python():
    module = load_module()
    _options, presets = module.load_registry()
    names = (ROOT / "config/docker-default.list").read_text().splitlines()
    assert names == presets["groups"]["docker_default"]
    launcher = (ROOT / "scripts/acacia-bonsai.sh").read_text()
    assert 'mapfile -t CONFIGS < "$REPO_ROOT/config/docker-default.list"' in launcher
    assert "python" not in launcher.lower()


def test_config_validation_rejects_stale_docker_list(tmp_path, monkeypatch):
    module = load_module()
    options, presets = module.load_registry()
    stale = tmp_path / "docker-default.list"
    stale.write_text("base\n")
    monkeypatch.setattr(module, "DOCKER_DEFAULT_LIST", stale)
    with pytest.raises(SystemExit, match="docker-default.list must match"):
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
    assert len(grandfathered) == len(set(grandfathered))
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






def test_constants_are_documentation_only():
    module = load_module()
    options, presets = module.load_registry()
    module.command_validate(options, presets)
    constants = options["constants"]
    assert set(constants) == {"VECTOR_ELT_T", "VECTOR_IMPL_AUTO"}
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


def test_native_dependencies_have_no_feature_options():
    module = load_module()
    options, presets = module.load_registry()
    removed = {"enable_tlsf_frontend", "native_arms"}
    assert removed.isdisjoint(options["options"])
    for name in presets["presets"]:
        assert removed.isdisjoint(module.normalize_preset(options, presets, name))


def test_all_groups_reference_existing_presets_and_shipping_is_unique():
    module = load_module()
    _, presets = module.load_registry()

    for group in presets["groups"].values():
        assert all(name in presets["presets"] for name in group)

    # Shipping membership and order come from the registry, not a fixed menu.
    docker_default = presets["groups"]["docker_default"]
    assert len(docker_default) == 4
    assert len(set(docker_default)) == len(docker_default)
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

    assert "-Dacacia_spot_lazy_provider=false" in args
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

    values["spot_lazy_provider"] = True
    assert "-Dacacia_spot_lazy_provider=true" in module.meson_args(options, values)


def test_preprocessor_flags_preserve_encodings_and_emission_order():
    module = load_module()
    options, _ = module.load_registry()
    values = module.defaults(options)
    values.update({
        "translation_pref": "any",
        "simd_is_max": False,
        "enable_diagnostics": True,
        "default_arms": "real:identity:backward,unreal:identity:forward",
        "forward_safety_solver": True,
    })
    expected = [
        "-DACACIA_TRANSLATION_PREF=spot::postprocessor::Any",
        "-DACACIA_TRANSLATION_PREFS=spot::postprocessor::Any",
        "-DACACIA_FORCED_OUTPUT_CONTRADICTION=0",
        "-DSIMD_IS_MAX=false",
        "-DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed",
        "-DACACIA_ENABLE_DIAGNOSTICS=1",
        r'-DACACIA_DEFAULT_ARMS=\"real:identity:backward,unreal:identity:forward\"',
        "-DACACIA_FORWARD_SAFETY_SOLVER=1",
        "-DACACIA_ENABLE_EQUIVARIANT_SOLVER=1",
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


@pytest.mark.parametrize("count", [0, 3, 5])
def test_config_validation_requires_exactly_four_shipping_members(count):
    module = load_module()
    options, presets = module.load_registry()
    members = presets["groups"]["docker_default"]
    presets["groups"]["docker_default"] = (members + [members[0]])[:count]
    with pytest.raises(SystemExit, match="exactly four distinct members"):
        module.command_validate(options, presets)


def test_config_validation_rejects_duplicate_shipping_members():
    module = load_module()
    options, presets = module.load_registry()
    members = presets["groups"]["docker_default"]
    members[-1] = members[0]
    with pytest.raises(SystemExit, match="exactly four distinct members"):
        module.command_validate(options, presets)


def test_kept_presets_are_flat_and_each_selector_varies():
    module = load_module()
    options, presets = module.load_registry()
    assert all("inherits" not in data for data in presets["presets"].values())
    resolved = [module.normalize_preset(options, presets, name) for name in presets["presets"]]
    for section in ("options", "families"):
        for name, option in options[section].items():
            if name in {"preset"}:
                continue
            assert any(values[name] != option["default"] for values in resolved), name
