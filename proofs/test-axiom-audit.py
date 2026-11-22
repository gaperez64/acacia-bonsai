"""Exercise the real semantic audit on tiny modules; no Lake build is needed."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


AUDIT = Path(__file__).with_name("AuditAxioms.lean").resolve()


class AxiomAuditTests(unittest.TestCase):
    def audit(self, source, modules=("Fixture",)):
        lean = shutil.which(os.environ.get("LEAN", "lean"))
        self.assertIsNotNone(lean, "put the pinned Lean on PATH or set LEAN")
        with tempfile.TemporaryDirectory(prefix="axiom-audit-") as directory:
            root = Path(directory)
            # The declaration namespace intentionally differs from the module.
            for module in modules:
                (root / f"{module}.lean").write_text("import Init\n" + source)
            env = dict(os.environ, LEAN_PATH=directory, LEAN_NUM_THREADS="1",
                       ELAN_TOOLCHAIN=AUDIT.with_name("lean-toolchain").read_text().strip(),
                       PATH=f"{Path(lean).parent}{os.pathsep}{os.environ['PATH']}")
            for module in modules:
                compiled = subprocess.run(
                    [lean, "-j1", "-s2048", "-o", f"{module}.olean", f"{module}.lean"],
                    cwd=root, env=env, capture_output=True, text=True, timeout=90)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            results = [subprocess.run(
                [lean, "-j1", "-s2048", "--run", str(AUDIT), module],
                cwd=root, env=env, capture_output=True, text=True, timeout=90)
                for module in modules]
            return results[0] if len(results) == 1 else results

    def test_allowed_axioms(self):
        result = self.audit("""
theorem ext (p q : Prop) (h : p ↔ q) : p = q := propext h
noncomputable def choose (α : Sort u) (h : Nonempty α) : α := Classical.choice h
theorem quotient {α : Sort u} (r : α → α → Prop) (a b : α) (h : r a b) :
    Quot.mk r a = Quot.mk r b := Quot.sound h
private theorem hidden : True := by trivial
""")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("audited axioms of", result.stdout)

    def test_executable_roots_are_isolated(self):
        results = self.audit("def main : IO Unit := pure ()\n", ("First", "Second"))
        for result in results:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("audited axioms of", result.stdout)

    def test_import_only_module(self):
        result = self.audit("")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stale_unimported_source_must_build(self):
        lake = shutil.which("lake")
        self.assertIsNotNone(lake, "put the pinned Lake on PATH")
        with tempfile.TemporaryDirectory(prefix="stale-axiom-audit-") as directory:
            root = Path(directory)
            (root / "lakefile.toml").write_text(
                'name = "auditFixture"\ndefaultTargets = ["Fixture"]\n'
                '[[lean_lib]]\nname = "Fixture"\n')
            (root / "Fixture.lean").write_text("import Init\n")
            nested = root / "Fixture/Nested"
            nested.mkdir(parents=True)
            unused = nested / "Unused.lean"
            unused.write_text("import Init\ntheorem unused : True := True.intro\n")
            env = dict(os.environ, LEAN_NUM_THREADS="1",
                       ELAN_TOOLCHAIN=AUDIT.with_name("lean-toolchain").read_text().strip())

            def build(*modules):
                return subprocess.run([lake, "--rehash", "build", *modules],
                                      cwd=root, env=env, capture_output=True,
                                      text=True, timeout=90)

            result = build("Fixture", "Fixture.Nested.Unused")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            stamp = unused.stat()
            unused.write_text("INVALID_LEAN\n")
            os.utime(unused, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
            result = build()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = build("Fixture", "Fixture.Nested.Unused")
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Unused.lean", result.stdout + result.stderr)

    def test_missing_module_fails(self):
        lean = shutil.which(os.environ.get("LEAN", "lean"))
        self.assertIsNotNone(lean, "put the pinned Lean on PATH or set LEAN")
        env = dict(os.environ, LEAN_NUM_THREADS="1",
                   ELAN_TOOLCHAIN=AUDIT.with_name("lean-toolchain").read_text().strip())
        result = subprocess.run([lean, "-j1", "--run", str(AUDIT), "MissingFixture"],
                                cwd=AUDIT.parent, env=env, capture_output=True,
                                text=True, timeout=90)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MissingFixture", result.stdout + result.stderr)

    def test_forbidden_axioms(self):
        for source, axiom in [
            ("private theorem hidden : 1 + 1 = 2 := by native_decide\n", "_native"),
            ("private theorem hidden : False := by sorry\n", "sorryAx"),
            ("axiom unchecked : False\ntheorem dependent : False := unchecked\n", "unchecked"),
        ]:
            with self.subTest(axiom=axiom):
                result = self.audit(source)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("forbidden axiom", result.stdout + result.stderr)
                self.assertIn(axiom, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
