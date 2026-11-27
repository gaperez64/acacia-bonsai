"""Audit both raw turns, then compare checked winners to the parity arm."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


def check(command, expected):
    result = subprocess.run(command, capture_output=True, text=True, timeout=40)
    assert result.returncode == expected, (command, result.stdout, result.stderr)
    return result.stdout


def main():
    audit, symcount, parity, fixture = sys.argv[1:]
    base = Path(fixture).read_text()
    grant_bits = "&&[0 <= i < nbits(n)] (request[i] <-> X grant[i])"
    request_stable = "&&[0 <= i < nbits(n)] (request[i] <-> X request[i])"
    with tempfile.TemporaryDirectory() as directory:
        for size in (3, 4, 5):
            text = re.sub(r"n = \d+;", f"n = {size};", base)
            aliases = text.replace("grant[nbits(n)];", "grant[nbits(n+1)];")
            terms = [
                (0, aliases, grant_bits),
                (1, aliases, f"X ({request_stable})"),
            ]
            if size == 3:
                terms.append((1, text, f"X ({request_stable}); G (||[0 <= i < n] value(grant, i))"))
            if size == 4:
                terms.append((1, text, "G (&&[0 <= i < n] !value(grant, i))"))
            if size == 5:
                terms.append((1, aliases, "G !value(grant, 0)"))
                terms.append((0, aliases, f"X ({grant_bits})"))
            for i, (expected, source, term) in enumerate(terms):
                path = Path(directory) / f"encoded-{size}-{i}.tlsf"
                path.write_text(source.replace("GUARANTEE {", f"GUARANTEE {{ {term};"))
                assert "lifted strategy" in check([audit, path], expected)
                assert "check=quotient" in check([symcount, path], expected)
                check([parity, "--arms", "both:parity:spot", "-T", path], expected)


if __name__ == "__main__":
    main()
