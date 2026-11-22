"""Exercise Lean CI selection and failure handling without running Lean."""

import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]
CHECK_REFERENCES = runpy.run_path(str(ROOT / "tests/check-proof-references.py"))["check"]


def test_current_proof_references():
    assert CHECK_REFERENCES(ROOT) > 0


def test_correspondence_accepts_line_moves_and_rejects_lost_anchors(tmp_path):
    (tmp_path / "src").mkdir()
    (tmp_path / "proofs").mkdir()
    source = tmp_path / "src/example.hh"
    source.write_text("void step () {\n}\n")
    (tmp_path / "proofs/README.md").write_text("`example.hh#void step () {`\n")
    proof = tmp_path / "proofs/Example.lean"
    proof.write_text("-- Historical reference: example.hh:900–910.\n")
    assert CHECK_REFERENCES(tmp_path) == 1
    source.write_text("// Inserted lines\n\nvoid step () {\n}\n")
    assert CHECK_REFERENCES(tmp_path) == 1
    source.write_text("void changed () {\n}\n")
    with pytest.raises(AssertionError, match="missing correspondence anchor"):
        CHECK_REFERENCES(tmp_path)
    source.write_text("void step () {\n}\n")
    proof.write_text("-- New implementation reference: unlisted.hh:10.\n")
    with pytest.raises(AssertionError, match="needs a stable anchor"):
        CHECK_REFERENCES(tmp_path)


@pytest.mark.parametrize("removed", [0, 1])
def test_correspondence_counts_identical_occurrences(tmp_path, removed):
    (tmp_path / "src").mkdir()
    (tmp_path / "proofs").mkdir()
    anchor = "const auto next_k = detail::checked_next_k (k, kinc);"
    source = tmp_path / "src/example.hh"
    occurrences = [f"// solver {i}\n{anchor}\n" for i in range(2)]
    source.write_text("".join(occurrences))
    (tmp_path / "proofs/README.md").write_text(
        f"`example.hh#{anchor}`; `example.hh#{anchor}`\n")
    assert CHECK_REFERENCES(tmp_path) == 2
    occurrences[removed] = "// unchecked increment\n"
    source.write_text("".join(occurrences))
    with pytest.raises(AssertionError, match="expected 2, found 1"):
        CHECK_REFERENCES(tmp_path)
    source.write_text((anchor + "\n") * 3)
    with pytest.raises(AssertionError, match="expected 2, found 3"):
        CHECK_REFERENCES(tmp_path)


@pytest.fixture
def history(tmp_path):
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=tmp_path, text=True).strip()

    git("init", "-q")
    (tmp_path / "proofs").mkdir()
    (tmp_path / "proofs/Example.lean").write_text("-- original\n")
    commit(tmp_path)
    return tmp_path, git("rev-parse", "HEAD")


def commit(directory):
    subprocess.run(["git", "add", "-A"], cwd=directory, check=True)
    subprocess.run(["git", "-c", "user.name=Lean CI",
                    "-c", "user.email=lean-ci@example.invalid", "commit", "-qm", "Fixture"],
                   cwd=directory, check=True)


def needed(directory, event, base):
    return subprocess.check_output(
        [ROOT / "proofs/ci-needed.sh", event, base], cwd=directory, text=True).strip()


@pytest.mark.parametrize("path,expected", [
    ("README.md", "false"),
    ("src/solver/example.hh", "false"),
    ("tools/golden/unrelated.lean", "false"),
    ("proofs/Example.lean", "true"),
    ("proofs/lean-toolchain", "true"),
    ("proofs/check.sh", "true"),
    (".github/workflows/lean.yml", "true"),
    ("proofs/Δ.lean", "true"),
    ("proofs/name with spaces.lean", "true"),
    ("proofs/name\nwith newline.lean", "true"),
    ('proofs/quoted"name.lean', "true"),
    ("docs/Δ with spaces\nand newline.md", "false"),
    ("docs/line\nproofs/NotAProof.lean", "false"),
])
@pytest.mark.parametrize("event", ["pull_request", "push"])
def test_change_detection(history, path, expected, event):
    directory, base = history
    changed = directory / path
    changed.parent.mkdir(parents=True, exist_ok=True)
    changed.write_text("changed\n")
    commit(directory)
    assert needed(directory, event, base) == expected


@pytest.mark.parametrize("event,base", [
    ("workflow_dispatch", ""), ("schedule", ""),
    ("push", ""), ("push", "0" * 40), ("push", "f" * 40),
])
def test_missing_base_and_unconditional_events(history, event, base):
    assert needed(history[0], event, base) == "true"


def test_renaming_a_proof_out_still_runs(history):
    directory, base = history
    (directory / "proofs/Example.lean").rename(directory / "Moved.lean")
    commit(directory)
    assert needed(directory, "push", base) == "true"


@pytest.mark.parametrize("old,new,expected", [
    ("proofs/Δ with spaces\nand newline.lean", "Moved Δ.lean", "true"),
    ("Moved Δ.lean", "proofs/Δ with spaces\nand newline.lean", "true"),
    (".github/workflows/lean.yml", "docs/old Δ workflow\n.yml", "true"),
    ("docs/old Δ workflow\n.yml", ".github/workflows/lean.yml", "true"),
    ("docs/Δ\nold.md", "docs/Δ\nnew.md", "false"),
])
@pytest.mark.parametrize("event", ["pull_request", "push"])
def test_rename_endpoints(history, old, new, expected, event):
    directory, _ = history
    source, destination = directory / old, directory / new
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text("unchanged contents\n")
    commit(directory)
    base = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=directory, text=True).strip()
    destination.parent.mkdir(parents=True, exist_ok=True)
    source.rename(destination)
    commit(directory)
    assert needed(directory, event, base) == expected


@pytest.mark.parametrize("failed_diff", [1, 2])
def test_diff_failure_is_propagated(tmp_path, failed_diff):
    binary = tmp_path / "git"
    binary.write_text('''#!/usr/bin/env python3
import os
from pathlib import Path
import sys
if sys.argv[1] == "cat-file":
    sys.exit(0)
assert sys.argv[1:5] == ["diff", "-z", "--name-only", "--find-renames"]
counter = Path(os.environ["DIFF_COUNT"])
count = int(counter.read_text()) + 1 if counter.exists() else 1
counter.write_text(str(count))
if count == int(os.environ["FAIL_DIFF"]):
    sys.exit(42)
sys.stdout.buffer.write(b"proofs/Example.lean\\0")
''')
    binary.chmod(0o755)
    env = dict(os.environ, PATH=f"{tmp_path}{os.pathsep}{os.environ['PATH']}",
               DIFF_COUNT=str(tmp_path / "count"), FAIL_DIFF=str(failed_diff))
    result = subprocess.run([ROOT / "proofs/ci-needed.sh", "push", "a" * 40],
                            cwd=tmp_path, env=env, text=True, capture_output=True)
    assert result.returncode == 42
    assert result.stdout == ""


@pytest.mark.parametrize("detector", ["success", "failure", "cancelled", "skipped", ""])
@pytest.mark.parametrize("lean_needed", ["true", "false", ""])
@pytest.mark.parametrize("lean", ["success", "failure", "cancelled", "skipped", ""])
def test_required_gate(detector, lean_needed, lean):
    env = dict(os.environ, DETECTOR_RESULT=detector, LEAN_NEEDED=lean_needed,
               LEAN_RESULT=lean)
    result = subprocess.run(["bash", ROOT / "proofs/ci-gate.sh"], env=env,
                            text=True, capture_output=True)
    expected = detector == "success" and (
        (lean_needed in {"true", "false"} and lean == "success") or
        (lean_needed == "false" and lean == "skipped"))
    assert (result.returncode == 0) == expected, result.stderr


@pytest.fixture
def fake_lake(tmp_path):
    proofs = tmp_path / "proofs"
    shutil.copytree(ROOT / "proofs", proofs, ignore=shutil.ignore_patterns(".lake"))
    # Simulate an exact/stale cache hit; source checks must still execute.
    (proofs / ".lake").mkdir()
    (proofs / ".lake/stale-cache").touch()
    binary = tmp_path / "lake"
    binary.write_text('''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys
with open(os.environ["LAKE_LOG"], "a") as log:
    log.write(json.dumps(sys.argv[1:]) + "\\n")
if sys.argv[1:3] == ["env", "lean"]:
    if "--run" in sys.argv:
        if os.environ.get("FAIL_AUDIT"):
            sys.exit(7)
    elif "INVALID_LEAN" in Path(sys.argv[-1]).read_text():
        sys.exit(9)
elif os.environ.get("FAIL_BUILD"):
    sys.exit(8)
''')
    binary.chmod(0o755)
    env = dict(os.environ, PATH=f"{tmp_path}{os.pathsep}{os.environ['PATH']}",
               LAKE_LOG=str(tmp_path / "lake.log"))
    for name in ("LEAN_NUM_THREADS", "LEAN_STACK_SIZE_KB", "LEAN_MAIN_USE_THREAD"):
        env.pop(name, None)
    return proofs, env


def run_check(proofs, env):
    return subprocess.run([proofs / "check.sh"], env=env, text=True, capture_output=True)


@pytest.mark.parametrize("threads", [None, "2"])
def test_every_module_elaborates_before_its_build_even_with_cache(fake_lake, threads):
    proofs, env = fake_lake
    if threads:
        env["LEAN_NUM_THREADS"] = threads
    result = run_check(proofs, env)
    assert result.returncode == 0, result.stderr
    calls = [json.loads(line) for line in Path(env["LAKE_LOG"]).read_text().splitlines()]
    sources = set()
    for check, build in zip(calls[:-3:2], calls[1:-3:2]):
        assert check[:2] == ["env", "lean"]
        assert f"-j{threads or '1'}" in check
        assert "-DwarningAsError=true" in check
        source = Path(check[-1])
        sources.add(source.name)
        assert build == ["build", f"SymmetryProofs.{source.stem}"]
    assert sources == {path.name for path in (proofs / "SymmetryProofs").glob("*.lean")}
    assert calls[-3][-1] == "SymmetryProofs.lean"
    assert calls[-2] == ["build"]
    audit = calls[-1]
    assert audit[:2] == ["env", "lean"]
    assert audit[audit.index("--run") + 1] == "AuditAxioms.lean"
    assert set(audit[audit.index("--run") + 2:]) == {
        "SymmetryProofs", *(f"SymmetryProofs.{Path(name).stem}" for name in sources)}


def test_semantic_audit_includes_unimported_nested_modules(fake_lake):
    proofs, env = fake_lake
    nested = proofs / "SymmetryProofs/Nested"
    nested.mkdir()
    (nested / "Unimported.lean").write_text("theorem unused : True := True.intro\n")
    result = run_check(proofs, env)
    assert result.returncode == 0, result.stderr
    audit = json.loads(Path(env["LAKE_LOG"]).read_text().splitlines()[-1])
    assert "SymmetryProofs.Nested.Unimported" in audit


@pytest.mark.parametrize("token", ["sorry", "admit", "axiom", "native_decide", "partial"])
def test_placeholder_audit_stops_before_lean(fake_lake, token):
    proofs, env = fake_lake
    (proofs / "SymmetryProofs/Automaton.lean").write_text(f"{token}\n")
    assert run_check(proofs, env).returncode == 1
    assert not Path(env["LAKE_LOG"]).exists()


def test_placeholder_audit_covers_nested_sources(fake_lake):
    proofs, env = fake_lake
    nested = proofs / "SymmetryProofs/Nested"
    nested.mkdir()
    (nested / "Example.lean").write_text("axiom unchecked : True\n")
    assert run_check(proofs, env).returncode == 1
    assert not Path(env["LAKE_LOG"]).exists()


@pytest.mark.parametrize("stage,expected", [("elaboration", 9), ("build", 8), ("audit", 7)])
def test_source_or_build_failure_is_not_hidden_by_cache(fake_lake, stage, expected):
    proofs, env = fake_lake
    if stage == "elaboration":
        (proofs / "SymmetryProofs/Automaton.lean").write_text("INVALID_LEAN\n")
    elif stage == "build":
        env["FAIL_BUILD"] = "1"
    else:
        env["FAIL_AUDIT"] = "1"
    assert run_check(proofs, env).returncode == expected
