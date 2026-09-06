import pytest


@pytest.fixture(autouse=True)
def isolate_campaign_scope_guards(monkeypatch):
    # Driver tests must not inspect or stop a developer's real campaigns.
    # Scope-guard tests explicitly clear this marker and mock systemctl.
    monkeypatch.setenv("ACACIA_CAMPAIGN_SCOPE_GUARD", "pytest")
