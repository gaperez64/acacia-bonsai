from __future__ import annotations

import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]


def test_unlabelled_unknown_is_rejected(tmp_path):
    """A K-bound-style exit 2 must not pass an unlabelled benchmark."""
    fake_acacia = tmp_path / "fake-acacia"
    fake_acacia.write_text("#!/bin/sh\nprintf 'UNKNOWN\\n'\nexit 2\n")
    fake_acacia.chmod(0o755)

    template = (ROOT / "tests/check-real-correct.sh.in").read_text()
    wrapper = tmp_path / "check-real-correct.sh"
    wrapper.write_text(
        template.replace("@ACABONSAI@", str(fake_acacia)).replace(
            "@LTLSYNT@", "/does/not/exist/ltlsynt"
        )
    )
    wrapper.chmod(0o755)

    result = subprocess.run(
        [
            "/bin/zsh",
            "-f",
            str(wrapper),
            "-a",
            "-F",
            str(ROOT / "tests/ltl/syntcomp24/lift4.ltl"),
        ],
        capture_output=True,
        text=True,
        check=False,
    )

    assert result.returncode == 3
    assert "FAILED: NO VERDICT: UNKNOWN (return 2)" in result.stdout
    assert "PASS." not in result.stdout
