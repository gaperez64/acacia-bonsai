from __future__ import annotations

import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
NATIVE_CONVERTER = ROOT / "benchmarking" / "convert-tlsf-corpus-native.py"


def test_native_converter_handles_empty_signal_class_and_removes_stale_failure(
    tmp_path,
):
    source = tmp_path / "tlsf"
    output = tmp_path / "ltl"
    source.mkdir()
    output.mkdir()
    (source / "good.tlsf").write_text("good\n")
    (source / "bad.tlsf").write_text("bad\n")
    inspector = tmp_path / "inspect"
    inspector.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        "if pathlib.Path(sys.argv[1]).stem == 'bad':\n"
        "    raise SystemExit(1)\n"
        "sys.stdout.buffer.write(b'G i\\0i\\0\\0Mealy\\0Mealy\\0Mealy\\0')\n"
    )
    inspector.chmod(0o755)

    good_selection = tmp_path / "good.list"
    good_selection.write_text("# selected\ngood.tlsf\n")
    result = subprocess.run(
        [
            sys.executable,
            str(NATIVE_CONVERTER),
            str(source),
            str(output),
            "--native-inspect",
            str(inspector),
            "--selection",
            str(good_selection),
        ],
        check=False,
    )
    assert result.returncode == 0
    assert (output / "good.ltl").read_text() == "G i\n"
    assert (output / "good.part").read_text() == ".inputs i\n.outputs\n"

    (output / "bad.ltl").write_text("stale\n")
    (output / "bad.part").write_text("stale\n")
    bad_selection = tmp_path / "bad.list"
    bad_selection.write_text("bad.tlsf\n")
    result = subprocess.run(
        [
            sys.executable,
            str(NATIVE_CONVERTER),
            str(source),
            str(output),
            "--native-inspect",
            str(inspector),
            "--selection",
            str(bad_selection),
        ],
        check=False,
    )
    assert result.returncode == 1
    assert not (output / "bad.ltl").exists()
    assert not (output / "bad.part").exists()
