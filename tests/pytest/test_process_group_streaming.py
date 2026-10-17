import importlib.util
import pathlib
import sys
import time

BENCHLIB = pathlib.Path(__file__).resolve().parents[2] / "benchmarking/benchlib.py"


def load_benchlib():
    spec = importlib.util.spec_from_file_location("benchlib_under_test", BENCHLIB)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
        return module
    finally:
        sys.modules.pop(spec.name, None)


def test_process_group_streaming_consumer_bounds_retained_output():
    module = load_benchlib()
    consumed: list[str] = []

    result = module.run_process_group(
        [
            sys.executable,
            "-c",
            "import sys; print('noise'); print('ACACIA_DIAG checkpoint=test', file=sys.stderr)",
        ],
        5,
        capture_consumer=lambda line: consumed.append(line),
    )

    assert result.returncode == 0
    assert result.stdout == result.stderr == ""
    assert sorted(consumed) == ["ACACIA_DIAG checkpoint=test\n", "noise\n"]
    assert result.stdout_bytes + result.stderr_bytes > 0


def test_process_group_cleans_descendant_after_successful_leader_exit():
    module = load_benchlib()
    result = module.run_process_group(
        [
            sys.executable,
            "-c",
            "import subprocess; "
            "p = subprocess.Popen(['sleep', '60'], stdout=subprocess.DEVNULL, "
            "stderr=subprocess.DEVNULL); print(p.pid, flush=True)",
        ],
        5,
    )

    descendant = pathlib.Path(f"/proc/{int(result.stdout.strip())}/stat")

    def descendant_is_running():
        try:
            return descendant.read_text().split()[2] != "Z"
        except FileNotFoundError:
            return False

    for _ in range(20):
        if not descendant_is_running():
            break
        time.sleep(0.05)

    assert not descendant_is_running()

