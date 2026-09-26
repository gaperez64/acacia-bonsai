"""Skip lift integration tests when their configured dependencies are unavailable."""

import pytest

from acacia_lift.tools import ProbeError, ToolConfiguration, _probe_bindings


def require_buddy(config: ToolConfiguration) -> None:
    try:
        _probe_bindings(config)
    except ProbeError as error:
        pytest.skip(f"BuDDy bindings unavailable: {error}")


def require_lift_tools(config: ToolConfiguration) -> None:
    required = (config.solver, config.checker, config.tlsf2tlsf,
                config.tlsf2ltl, config.tlsfinfo, config.monitor)
    if not all(path.is_file() for path in required):
        pytest.skip("tlsf-tools build unavailable")
    require_buddy(config)
