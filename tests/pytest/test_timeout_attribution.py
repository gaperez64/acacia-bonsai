"""Timeout delivery checks with the existing checked portfolio fixture binary."""
import os
from pathlib import Path
import runpy
import signal

import pytest

ROOT = Path(__file__).resolve().parents[2]
CHECKS = runpy.run_path(str(ROOT / "tests/check-attribution.py"))


@pytest.mark.parametrize("signum", [None, signal.SIGTERM, signal.SIGINT])
def test_five_worker_timeout_has_parent_terminals_and_flushed_writer(tmp_path, signum):
    build = os.environ.get("ACACIA_ATTRIBUTION_TEST_BUILD")
    if not build:
        pytest.skip("set ACACIA_ATTRIBUTION_TEST_BUILD to a checked build with portfolio hooks")
    binary = Path(build) / "tests/game-backend-integration"
    assert binary.is_file(), binary
    observed = CHECKS["timeout_portfolio"](binary, tmp_path, signum)
    if signum is None:
        assert CHECKS["timeout_portfolio"](binary, tmp_path, diagnostics=False) == observed


@pytest.mark.parametrize("missing", ["parent_terminal", "record_summary", "writer_summary"])
def test_timeout_delivery_oracle_rejects_missing_cleanup(missing):
    records = [
        {"event": "worker_spawn", "worker": 0, "worker_pid": 101, "emitter_pid": 100},
        {"event": "parent_terminal", "worker": 0, "worker_pid": 101, "emitter_pid": 100},
        {"phase": "record_summary", "emitter_pid": 100},
        {"event": "writer_summary", "delivered_records": 3, "failed_records": 0,
         "incomplete_packet": False, "emitter_pid": 102},
    ]
    CHECKS["complete_timeout_delivery"](records, 1, 100)
    incomplete = [row for row in records if row.get("event", row.get("phase")) != missing]
    with pytest.raises(AssertionError):
        CHECKS["complete_timeout_delivery"](incomplete, 1, 100)
