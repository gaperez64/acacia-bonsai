#!/usr/bin/env python3
"""Regress the component requirements for transition-based acceptance."""

import importlib.util
import itertools
import pathlib
import sys
import unittest


ROOT = pathlib.Path(sys.argv.pop(1) if len(sys.argv) > 1 else ".").resolve()
SPEC = importlib.util.spec_from_file_location("acacia_config", ROOT / "scripts/acacia-config.py")
CONFIG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONFIG)


class TransitionAcceptanceConfigTest(unittest.TestCase):
    def setUp(self):
        self.options, self.presets = CONFIG.load_registry()
        self.compatible = {
            "transition_acceptance": True,
            "boolean_states": "transition_core",
            "aut_preprocessor": "standard",
            "ios_precomputer": "standard",
            "actioner": "standard",
            "spot_guarded_backend": False,
        }

    def validate(self, overrides):
        # Exercise normalization and inherited values, not just a flat dict.
        presets = {"presets": {"parent": self.compatible,
                               "child": {"inherits": "parent", **overrides}}}
        CONFIG.validate_preset(self.options, presets, "child")

    def test_compatible_families(self):
        for boolean, preprocessor, ios in itertools.product(
            ["transition_core", "no_boolean_states"],
            ["standard", "no_preprocessing"],
            ["standard", "powset", "fake_vars"],
        ):
            with self.subTest(boolean=boolean, preprocessor=preprocessor, ios=ios):
                self.validate({"boolean_states": boolean, "aut_preprocessor": preprocessor,
                               "ios_precomputer": ios})

    def test_rejects_state_acceptance_components(self):
        for family, choice, required in [
            ("boolean_states", "forward_saturation", "transition_core or no_boolean_states"),
            ("aut_preprocessor", "surely_losing", "standard or no_preprocessing"),
            ("aut_preprocessor", "elevator", "standard or no_preprocessing"),
            ("ios_precomputer", "mona", "standard, powset or fake_vars"),
            ("ios_precomputer", "semantic_mona", "standard, powset or fake_vars"),
            ("ios_precomputer", "delegate", "standard, powset or fake_vars"),
            ("actioner", "no_ios_precomputation", "standard"),
        ]:
            with self.subTest(family=family, choice=choice):
                overrides = {family: choice}
                if family == "actioner":
                    # Satisfy the older actioner/ios_precomputer rule first.
                    overrides["ios_precomputer"] = "delegate"
                with self.assertRaises(SystemExit) as caught:
                    self.validate(overrides)
                self.assertIn("transition_acceptance=true", str(caught.exception))
                self.assertIn(f"{family}={required}", str(caught.exception))

    def test_disabled_preserves_existing_family_choices(self):
        for family in ["boolean_states", "aut_preprocessor", "ios_precomputer", "actioner"]:
            for choice in self.options["families"][family]["choices"]:
                with self.subTest(family=family, choice=choice):
                    overrides = {"transition_acceptance": False, family: choice}
                    if family == "actioner" and choice == "no_ios_precomputation":
                        overrides["ios_precomputer"] = "delegate"
                    self.validate(overrides)

    def test_guarded_backend_requires_state_acceptance(self):
        with self.assertRaises(SystemExit) as caught:
            self.validate({"spot_guarded_backend": True})
        self.assertEqual(
            str(caught.exception),
            "child: transition_acceptance=true requires spot_guarded_backend=false",
        )
        self.validate({"spot_guarded_backend": False})
        self.validate({"transition_acceptance": False, "spot_guarded_backend": True})

    def test_equivariant_solver_remains_compatible(self):
        self.validate({"enable_equivariant_solver": True})

    def test_validate_rejects_shipping_override(self):
        CONFIG.command_validate(self.options, self.presets)
        name = "otf_sparse_formula"
        self.presets["presets"][name]["transition_acceptance"] = True
        with self.assertRaises(SystemExit) as caught:
            CONFIG.validate_preset(self.options, self.presets, name)
        self.assertIn(name, str(caught.exception))
        self.assertIn("transition_acceptance=true", str(caught.exception))
        self.assertIn("boolean_states=transition_core or no_boolean_states",
                      str(caught.exception))
        # A derived preset may be visited before its parent by `validate`.
        with self.assertRaisesRegex(SystemExit, "transition_acceptance=true"):
            CONFIG.command_validate(self.options, self.presets)


if __name__ == "__main__":
    unittest.main()
