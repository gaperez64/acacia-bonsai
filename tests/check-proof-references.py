"""Check the README's stable C++ anchors and coverage of proof source references."""

from pathlib import Path
import re


ANCHOR = re.compile(r"`(?P<name>[\w./]+\.(?:hh|cc))#(?P<anchor>[^`\n]+)`")
SOURCE = re.compile(r"[\w./]+\.(?:hh|cc)\b")


def check(root: Path) -> int:
    readme = (root / "proofs/README.md").read_text()
    references = list(ANCHOR.finditer(readme))
    assert references, "no C++ correspondence anchors found"
    documented = set()
    for match in references:
        name, anchor = match["name"], match["anchor"]
        # As in the correspondence table, paths are relative to src/.
        path = root / "src" / name
        lines = {line.strip() for line in path.read_text().splitlines()}
        assert anchor in lines, f"{name}: missing correspondence anchor {anchor!r}"
        documented.add(name)

    documents = [root / "proofs/README.md", *sorted((root / "proofs").rglob("*.lean"))]
    for document in documents:
        if ".lake" in document.parts:
            continue
        for name in SOURCE.findall(document.read_text()):
            assert any(path == name or path.endswith("/" + name) for path in documented), (
                f"{document}: {name} needs a stable anchor in proofs/README.md")
    return len(references)


if __name__ == "__main__":
    count = check(Path(__file__).resolve().parents[1])
    print(f"checked {count} C++ correspondence anchors and proof reference coverage")
