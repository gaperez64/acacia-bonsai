"""Exercise the unscoped memory bound and finite-input replay with real children."""

import importlib.util
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace

import pytest


SPEC = 'INFO { SEMANTICS: Finite,Moore; TARGET: Mealy; }\nMAIN { GUARANTEE { X[!](true); } }\n'
INFINITE_SPEC = 'INFO { SEMANTICS: Moore; TARGET: Mealy; }\nMAIN { GUARANTEE { X(true); } }\n'
pytestmark = pytest.mark.skipif(not shutil.which("prlimit"), reason="Linux prlimit required")


@pytest.fixture
def driver(tmp_path, monkeypatch):
    path = Path(__file__).resolve().parents[1] / "check-fuzz-portfolio.py"
    spec = importlib.util.spec_from_file_location("fuzz_portfolio", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module.shutil, "which", lambda _: None)
    monkeypatch.delenv("ACACIA_TEST_SKIP_RLIMIT_AS", raising=False)
    monkeypatch.setattr(module.subprocess, "run", lambda command, **kwargs: SimpleNamespace(
        stdout="one\ntwo\n" if command[-1] == "--default-arms" else
        INFINITE_SPEC if command[1] == "decode-infinite" else SPEC))
    seed = tmp_path / "seed"
    seed.write_bytes(b"")
    monkeypatch.setattr(sys, "argv", [str(path), "--binary", str(tmp_path / "solver"),
                                     "--replayer", str(tmp_path / "decoder"),
                                     "--work-dir", str(tmp_path), str(seed)])
    return module


def test_fallback_limits_children_and_preserves_finite_input(driver, tmp_path, capsys):
    solver = tmp_path / "solver"
    solver.write_text(f"#!{sys.executable}\n" + '''
import json
import mmap
from pathlib import Path
import resource
import subprocess
import sys

assert resource.getrlimit(resource.RLIMIT_DATA) == (8 << 30, 8 << 30)
subprocess.check_call([sys.executable, "-c",
    "import resource; assert resource.getrlimit(resource.RLIMIT_DATA) == (8 << 30, 8 << 30)"])
# Reserve virtual space without allocating writable data, as Rust stacks do.
with mmap.mmap(-1, 16 << 30, flags=mmap.MAP_PRIVATE, prot=0):
    pass
try:
    mmap.mmap(-1, 9 << 30, flags=mmap.MAP_PRIVATE, prot=mmap.PROT_READ | mmap.PROT_WRITE)
except OSError:
    pass
else:
    raise AssertionError("writable allocation escaped the limit")
text = Path(sys.argv[-1]).read_text()
with Path(__file__).with_name("calls.jsonl").open("a") as stream:
    stream.write(json.dumps(text) + "\\n")
if "Finite" in text:
    sys.exit(2)
print("REALIZABLE")
''')
    solver.chmod(0o755)
    assert driver.main() == 0
    output = capsys.readouterr().out
    assert "8 GiB RLIMIT_DATA" in output
    assert "checked=1 skipped=1" in output
    assert "finite-variants=1" in output
    calls = [json.loads(line) for line in (tmp_path / "calls.jsonl").read_text().splitlines()]
    assert calls == [SPEC, SPEC, INFINITE_SPEC, INFINITE_SPEC]


def test_sanitizer_without_scopes_skips_before_launch(driver, monkeypatch, capsys):
    monkeypatch.setenv("ACACIA_TEST_SKIP_RLIMIT_AS", "1")
    monkeypatch.setattr(driver, "run_process_group", lambda *args: pytest.fail("launched arm"))
    assert driver.main() == 77
    assert "sanitizer shadow memory needs systemd scopes" in capsys.readouterr().out
