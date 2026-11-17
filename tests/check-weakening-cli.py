#!/usr/bin/env python3
"""Retired weakening options must fail before launching a solver."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check_rejections(binary):
    retired = [(["--weakening", "extended"], "--weakening extended"),
               (["--weakening=extended"], "--weakening extended")]
    for option in ("--weakening-attempt-ms", "--weakening-total-ms"):
        for value in ("0", "250", "-1", "invalid"):
            retired.extend([([option, value], option), ([f"{option}={value}"], option)])
        retired.append(([option], option))
    for flags, name in retired:
        for mode in ([], ["--weakening", "basic"], ["--weakening", "off"]):
            result = subprocess.run([str(binary), "-f", "true", "-i", "a", "-o", "b",
                                     *mode, *flags],
                                    capture_output=True, text=True, timeout=8)
            assert result.returncode == 3, (flags, mode, result)
            assert name in result.stderr and "removed" in result.stderr, result
            assert "basic or off" in result.stderr and not result.stdout, result


if __name__ == "__main__":
    binary = Path(sys.argv[1]).resolve()
    help_text = subprocess.check_output([str(binary), "--help"], text=True)
    assert "[basic|off] (default basic)" in help_text
    assert "extended" not in help_text
    assert "--weakening-attempt-ms" not in help_text
    assert "--weakening-total-ms" not in help_text
    check_rejections(binary)
    invalid = subprocess.run([str(binary), "-f", "true", "--weakening", "invalid"],
                             capture_output=True, text=True, timeout=8)
    assert invalid.returncode == 3
    assert invalid.stderr == "Error: --weakening expects basic or off.\n"
    # Exercise the Docker wrapper with the real solver: forwarding must preserve
    # both accepted modes and the explicit retirement errors.
    source = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix="weakening-cli-") as temporary:
        root = Path(temporary)
        (root / "scripts").mkdir()
        (root / "config").mkdir()
        wrapper = root / "scripts/acacia-bonsai.sh"
        shutil.copy2(source / "scripts/acacia-bonsai.sh", wrapper)
        registry = source / "config/docker-default.list"
        shutil.copy2(registry, root / "config/docker-default.list")
        target = root / f"build_{registry.read_text().splitlines()[0]}" / "src/acacia-bonsai"
        target.parent.mkdir(parents=True)
        target.symlink_to(binary)
        check_rejections(wrapper)
        for mode in ("basic", "off"):
            actual = subprocess.run([str(wrapper), "-f", "true", "-i", "a", "-o", "b",
                                     "--weakening", mode], capture_output=True, text=True,
                                    timeout=8)
            assert actual.returncode == 0 and actual.stdout.strip() == "REALIZABLE", actual
    print("retired weakening options rejected; basic/off help retained")
