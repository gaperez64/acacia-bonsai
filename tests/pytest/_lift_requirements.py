"""Skip lift integration tests when their configured dependencies are unavailable."""

import pytest

from acacia_lift.tools import (ProbeError, ToolConfiguration, _probe_bindings,
                               probe_frontend_toolchain)


def require_buddy(config: ToolConfiguration) -> None:
    try:
        _probe_bindings(config)
    except ProbeError as error:
        pytest.skip(f"BuDDy bindings unavailable: {error}")


def require_lift_tools(config: ToolConfiguration) -> None:
    required = (config.solver, config.checker, config.tlsf2tlsf,
                config.tlsf2ltl, config.tlsfinfo, config.monitor)
    if not all(path.is_file() for path in required):
        pytest.skip("oracle tlsf-tools build unavailable; run scripts/build-oracle-toolchain.sh")
    # A present but stale build is a configuration error, never a skipped test.
    probe_frontend_toolchain(config)
    require_buddy(config)
