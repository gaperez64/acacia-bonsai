"""Validated capability measurements used to choose lifting schemas."""

from __future__ import annotations

from .capabilities import CAPABILITIES, DECLINED_FAMILY_STABLE_FROM


def stable_from(family: str) -> int | None:
    capability = CAPABILITIES.get(family)
    return (capability.stable_from if capability is not None
            else DECLINED_FAMILY_STABLE_FROM.get(family))


def measured_arity(family: str) -> int | None:
    capability = CAPABILITIES.get(family)
    return capability.measured_arity if capability is not None else None


def measured_role_class_count(family: str) -> int | None:
    capability = CAPABILITIES.get(family)
    return capability.role_class_count if capability is not None else None
