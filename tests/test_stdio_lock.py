"""The Vita's newlib has no stdio locking (_RETARGETABLE_LOCKING off, __sfp_lock_acquire is `bx lr`): two threads in
fopen / fclose at once can be handed the same FILE from the pool, and when one closes it the other's FILE has
fileno -1, so its next fseek crashes in __vita_fd_grab.

Every call that takes or returns a FILE from the pool goes through port/src/vita/stdio_lock.c, wired by the linker
(-Wl,--wrap), so the static libraries (vitaGL, SDL, pl_mpeg) are covered too. The last test checks the built ELF:
no direct call to the real functions outside their wrapper."""
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
WRAPPED = ("fopen", "fclose", "freopen", "fdopen")


def test_linker_wraps_every_pool_function():
    cmake = (ROOT / "port" / "CMakeLists.txt").read_text(encoding="utf-8")
    for name in WRAPPED:
        assert f"--wrap={name}" in cmake, name


def test_wrappers_call_the_real_function_under_the_lock():
    src = (ROOT / "port" / "src" / "vita" / "stdio_lock.c").read_text(encoding="utf-8")
    for name in WRAPPED:
        body = re.search(rf"__wrap_{name}\s*\([^)]*\)\s*\{{(.*?)\n\}}", src, re.S)
        assert body, f"__wrap_{name} missing"
        text = body.group(1)
        assert "stdio_lock()" in text and "stdio_unlock()" in text and f"__real_{name}(" in text, name
        assert text.index("stdio_lock()") < text.index(f"__real_{name}(") < text.index("stdio_unlock()"), name


ELF = Path(os.environ.get("SH3_VITA_ELF", os.path.expanduser("~/tools/build/sh3vita/sh3")))
OBJDUMP = shutil.which("arm-vita-eabi-objdump")


@pytest.mark.skipif(OBJDUMP is None or not ELF.exists(), reason="Vita ELF or arm-vita-eabi-objdump missing")
def test_no_direct_call_outside_the_wrappers():
    asm = subprocess.run([OBJDUMP, "-d", str(ELF)], capture_output=True, text=True, check=True).stdout
    current, stray = None, []
    for line in asm.splitlines():
        head = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
        if head:
            current = head.group(1)
            continue
        call = re.search(r"\bbl[x]?\s+[0-9a-f]+ <(\w+)>", line)
        if call and call.group(1) in WRAPPED and current != f"__wrap_{call.group(1)}":
            stray.append(f"{current} -> {call.group(1)}")
    assert not stray, stray
