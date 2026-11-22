"""Exercise the real semantic audit on tiny modules; no Lake build is needed."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


AUDIT = Path(__file__).with_name("AuditAxioms.lean").resolve()


class AxiomAuditTests(unittest.TestCase):
    def audit(self, source):
        lean = shutil.which(os.environ.get("LEAN", "lean"))
        self.assertIsNotNone(lean, "put the pinned Lean on PATH or set LEAN")
        with tempfile.TemporaryDirectory(prefix="axiom-audit-") as directory:
            root = Path(directory)
            # The declaration namespace intentionally differs from the module.
            (root / "Fixture.lean").write_text("import Init\n" + source)
            env = dict(os.environ, LEAN_PATH=directory, LEAN_NUM_THREADS="1",
                       PATH=f"{Path(lean).parent}{os.pathsep}{os.environ['PATH']}")
            compiled = subprocess.run(
                [lean, "-j1", "-s2048", "-o", "Fixture.olean", "Fixture.lean"],
                cwd=root, env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            return subprocess.run(
                [lean, "-j1", "-s2048", "--run", str(AUDIT), "Fixture"],
                cwd=root, env=env, capture_output=True, text=True, timeout=90)

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
