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
    grant_stable = "&&[0 <= i < nbits(n+1)] (grant[i] <-> X grant[i])"
    grant_unused = "grant[nbits(n)]"
    with tempfile.TemporaryDirectory() as directory:
        for size in (2, 3, 4, 5):
            text = re.sub(r"n = \d+;", f"n = {size};", base)
            aliases = text.replace("grant[nbits(n)];", "grant[nbits(n+1)];")
            terms = [
                (0, aliases, grant_bits),
                (1, aliases, f"X ({request_stable})"),
            ]
            if size == 2:
                wide = aliases.replace("request[nbits(n)];", "request[nbits(n+1)];")
                scalar = wide.replace("OUTPUTS {", "OUTPUTS { echo;")
                same = grant_bits.replace(" <-> X ", " <-> ")
                prefix = f"!({same}); X ({same});"
                terms = [
                    (0, wide, f"{prefix} X ({grant_bits})"),
                    (1, wide, f"{prefix} ({grant_bits})"),
                    (1, scalar, f"echo <-> ({request_stable})"),
                ]
            if size == 3:
                terms.append((1, text, f"X ({request_stable}); G (||[0 <= i < n] value(grant, i))"))
                immediate = text.replace(" <-> X grant[i]", " <-> grant[i]")
                terms.append((0, immediate, "true"))
            if size == 4:
                residual = aliases.replace("i < nbits(n)] (grant[i]", "i < nbits(n+1)] (grant[i]")
                terms.extend([
                    (1, text, "G (&&[0 <= i < n] !value(grant, i))"),
                    (1, aliases, f"(||[0 <= i < n] value(grant, i)) <-> ({request_stable})"),
                    (0, aliases, f"X ({grant_bits}); X X {grant_unused}"),
                    (0, residual, f"X {grant_unused}; X X {grant_unused}; X ({grant_stable})"),
                    (0, residual, f"X {grant_unused}; X X {grant_unused}; X !({grant_stable})"),
                ])
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
