import pytest


@pytest.fixture(autouse=True)
def isolate_campaign_scope_guards(monkeypatch):
    # Driver tests must not inspect or stop a developer's real campaigns.
    # Scope-guard tests explicitly clear this marker and mock systemctl.
    monkeypatch.setenv("ACACIA_CAMPAIGN_SCOPE_GUARD", "pytest")


@pytest.fixture
def master_benchmark_reader(tmp_path):
    import importlib.util
    import pathlib
    import subprocess
    import sys

    root = pathlib.Path(__file__).resolve().parents[2]
    def load(name):
        try:
            source = subprocess.run(
                ["git", "show", f"d9d3fd43:benchmarking/{name}"], cwd=root,
                capture_output=True, check=True).stdout
        except subprocess.CalledProcessError:
            pytest.skip("master reader fixture needs pinned d9d3fd43 history")
        script = tmp_path / f"master-{name}"
        script.write_bytes(source)
        spec = importlib.util.spec_from_file_location("master_" + name.replace("-", "_"), script)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module
    return load
