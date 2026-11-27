"""Check the inspector's code alphabet and its finite action."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True, timeout=20).stdout


def inspect(inspector, spec):
    return run(inspector, "--encoded", spec).splitlines()


def resize(text, size):
    return re.sub(r"\bn\s*=\s*\d+;", f"n = {size};", text, count=1)


def check_certificate(lines, size, fixed=False, codes=None):
    moving = list(range(int(fixed), size))
    coordinates = moving + ([0] if fixed else [])
    assert lines[0].split() == ["certified", str(len(moving)), *map(str, coordinates)], lines[:3]
    buses = [line.split()[1:] for line in lines if line.startswith("bus ")]
    assert [owner for owner, *_ in buses] == ["1", "0"]
    widths = [int(width) for _, width, *_ in buses]
    capacity = [1 << width for width in widths]
    projection = (1 << (size - 1).bit_length()) - 1
    expected_codes = coordinates if codes is None else codes
    aliases = [{}, {}]
    if widths[1] > projection.bit_length():
        aliases[1] = {code: (projection + 1) | code for code in expected_codes}
    for b, (_, _, *mapping) in enumerate(buses):
        assert mapping == [f"{code}/{aliases[b][code]}" if code in aliases[b] else str(code)
                           for code in expected_codes]

    unused = [sorted(set(range(cap)) - set(expected_codes) - set(alias.values()))
              for cap, alias in zip(capacity, aliases, strict=True)]
    assert [list(map(int, line.split()[1:])) for line in lines if line.startswith("unused")] == [
        group for group in unused if group
    ]
    generators = [[tuple(map(int, entry.split("/"))) for entry in line.split()[1:]]
                  for line in lines if line.startswith("swap ")]
    stars = []
    for i in range(1, len(moving)):
        star = []
        for b, alias in enumerate(aliases):
            first, second = expected_codes[0], expected_codes[i]
            star.append((b, first, second))
            if alias:
                star.append((b, alias[first], alias[second]))
        stars.append(star)
    assert generators == stars + [[(b, group[0], code)] for b, group in enumerate(unused)
                                  for code in group[1:]]

    tables = [line.split()[1:] for line in lines if line.startswith("table ")]
    stable = "".join("1" if a & projection == b & projection else "0"
                     for b in range(capacity[1]) for a in range(capacity[1]))
    assert ["1/0", "1/1", stable] in tables
    if fixed:
        assert ["0/0", "0" + "1" * (capacity[0] - 1)] in tables
    for *slots, truth in tables:
        owners = [int(slot.split("/")[0]) for slot in slots]
        for swaps in generators:
            perm = [dict() for _ in capacity]
            for bus, a, b in swaps:
                perm[bus].update({a: b, b: a})
            for row, value in enumerate(truth):
                stride, image = 1, 0
                for bus in owners:
                    code = row // stride % capacity[bus]
                    image += stride * perm[bus].get(code, code)
                    stride *= capacity[bus]
                assert value == truth[image]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inspector")
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--obfuscator")
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    if bool(args.manifest) != bool(args.corpus) or (args.manifest and not args.obfuscator):
        parser.error("a corpus campaign needs --corpus, --manifest and --obfuscator")

    base = args.fixture.read_text().replace("grant[nbits(n)]", "grant[nbits(n+1)]")
    response = "&&[0 <= i < n] (value(request, i) -> F value(grant, i));"
    fixed = base.replace(response, response.replace("0 <=", "0 <") + """
      G (value(request, 0) -> X (&&[0 < i < n] !value(grant, i) U value(grant, 0)));
    """).replace("  INVARIANTS {", """
    ASSUMPTIONS { G F (||[0 <= i < nbits(n)] request[i]); }
    INVARIANTS {
    """)
    with tempfile.TemporaryDirectory(prefix="encoded-family-") as tmp:
        plain = Path(tmp) / "plain.tlsf"
        renamed = Path(tmp) / "renamed.tlsf"
        mapping = Path(tmp) / "names.tsv"

        def positive(label, text, size, distinguished=False, codes=None):
            plain.write_text(text)
            result = inspect(args.inspector, plain)
            check_certificate(result, size, distinguished, codes)
            if args.obfuscator:
                renamed.write_text(run(args.obfuscator, "--mode", "rename", "--seed", "471",
                                       "--map", mapping, plain))
                assert renamed.read_text() != text
                other = inspect(args.inspector, renamed)
                check_certificate(other, size, distinguished, codes)
                assert sorted(result) == sorted(other), label
                print(f"{label} plain=certified renamed=certified", flush=True)

        for size in (3, 4, 5, 8):
            positive(f"simple/{size}", resize(base, size), size)
            positive(f"fixed/{size}", resize(fixed, size), size, True)
        positive("reversed bits", base.replace("bus[i]", "bus[j-1-i]"), 5, codes=[0, 4, 2, 6, 1])
        positive("target adaptation", base.replace("SEMANTICS: Mealy TARGET: Mealy",
                                                   "SEMANTICS: Moore TARGET: Moore"), 5)
        positive("all sections", base.replace("  INVARIANTS {", """
          INITIALLY { ||[0 <= i < n] value(request, i); }
          PRESET { ||[0 <= i < n] value(grant, i); }
          REQUIRE { ||[0 <= i < n] value(request, i); }
          ASSUMPTIONS { F (||[0 <= i < n] value(request, i)); }
          INVARIANTS {
        """), 5)

        def extra(text, term):
            return text.replace("GUARANTEE {", f"GUARANTEE {{ {term};")

        positive("invariant extra bit", extra(resize(base, 4), "G grant[2]"), 4)
        plain.write_text(extra(base, "G (&&[0 <= i < nbits(n)] (request[i] <-> grant[i]))"))
        joint = inspect(args.inspector, plain)
        assert joint[0] == "certified 5 0 1 2 3 4"
        assert [line for line in joint if line.startswith("unused ")] == [
            "unused 5", "unused 6", "unused 7"
        ] * 2
        equality = "".join(str(int(a == b)) for b in range(8) for a in range(8))
        assert f"table 0/0 1/0 {equality}" in joint
        negative = {
            "raw-bit conjunct": extra(base, "G grant[0]"),
            "different equality mask": base.replace("< nbits(n)", "< nbits(n)-1"),
            "misbound selector": extra(
                base, "&&[0 <= i < n] (value(request, i) -> F value(grant, (i+1)%n))"),
            "conflicting axes": extra(
                base.replace("n = 5;", "n = 5; m = 5;"),
                response.replace("< n]", "< m]").rstrip(";")),
            "enum bus": base.replace("DEFINITIONS {", "DEFINITIONS { enum Phase = idle: 00 busy: 10;")
                            .replace("OUTPUTS {", "OUTPUTS { Phase mode;"),
            "oversized bus": base.replace("request[nbits(n)]", "request[nbits(n*16777216)]"),
        }
        for label, text in negative.items():
            plain.write_text(text)
            assert inspect(args.inspector, plain) == ["declined"], label

        if args.manifest:
            small = set()
            for name in args.manifest.read_text().splitlines():
                path = args.corpus / Path(name).with_suffix(".tlsf")
                text = path.read_text()
                size = int(re.search(r"\bn\s*=\s*(\d+);", text)[1])
                distinguished = "value(request, 0)" in text
                positive(path.stem, text, size, distinguished)
                for count in (3, 4, 5, 8):
                    reduced = resize(text, count)
                    if reduced not in small:
                        small.add(reduced)
                        positive(f"{path.stem}/{count}", reduced, count, distinguished)
                plain.write_text(text)
                renamed.write_text(run(args.obfuscator, "--mode", "all", "--seed", "137", plain))
                reordered = inspect(args.inspector, renamed)
                check_certificate(reordered, size, distinguished)
                assert sorted(inspect(args.inspector, plain)) == sorted(reordered), name


if __name__ == "__main__":
    main()
