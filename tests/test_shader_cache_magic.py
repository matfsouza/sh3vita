"""BUG-036: the shader cache writer and reader must agree on the file magic, or every program recompiles on every run."""
import pathlib
import re

DRAW = pathlib.Path(__file__).resolve().parent.parent / "port" / "src" / "d3d8" / "draw.c"


def test_writer_magic_is_accepted_by_reader():
    src = DRAW.read_text(encoding="utf-8")
    chars = re.search(r"ShcHeader header = \{\{((?:'.',)+)0\}", src).group(1)
    written = "".join(re.findall(r"'(.)'", chars))
    assert f'"{written}\\0"' in src
