import importlib.util
import pathlib
import sys
from types import SimpleNamespace


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "tlsf-verdict-parity.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("tlsf_verdict_parity", SCRIPT)
tlsf_verdict_parity = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = tlsf_verdict_parity
SPEC.loader.exec_module(tlsf_verdict_parity)


def run(
    output: str,
    returncode: int,
    timed_out: bool = False,
    resource_limited: bool = False,
):
    return SimpleNamespace(
        stdout=output,
        stderr="",
        returncode=returncode,
        timed_out=timed_out,
        resource_limited=resource_limited,
    )


def test_classify_accepts_only_consistent_solver_exits():
    assert tlsf_verdict_parity.classify(run("REALIZABLE\n", 0)) == "REALIZABLE"
    assert tlsf_verdict_parity.classify(run("UNREALIZABLE\n", 1)) == "UNREALIZABLE"
    assert tlsf_verdict_parity.classify(run("UNKNOWN\n", 2)) == "UNKNOWN"


def test_classify_rejects_crashes_and_inconsistent_exits():
    assert tlsf_verdict_parity.classify(run("", -11)) == "ERROR"
    assert tlsf_verdict_parity.classify(run("REALIZABLE\n", 1)) == "ERROR"
    assert tlsf_verdict_parity.classify(run("UNKNOWN\n", 3)) == "ERROR"


def test_classify_preserves_runner_timeouts():
    assert tlsf_verdict_parity.classify(run("", 124, timed_out=True)) == "TIMEOUT"


def test_classify_reports_scope_oom_separately_from_solver_unknown():
    assert (
        tlsf_verdict_parity.classify(run("", 2, resource_limited=True))
        == "RESOURCE_LIMIT"
    )
