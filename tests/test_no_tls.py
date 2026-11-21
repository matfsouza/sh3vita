"""No thread-local variables in the port.

On the Vita, `__thread` compiles to emulated TLS (__emutls_get_address over gthread keys), and for the port's threads
(SDL / kernel threads, not pthreads) it gave every thread the same value. prep.c's `on_prep` (1 on the GL consumer
thread) was therefore seen as 1 by the game thread too: prep_call ran its function at once on the game thread instead
of after the queued GL work, so the inventory's 64x64 status picture was read before it was drawn (black), and GL was
called from two threads. Per-thread state is kept by comparing thread ids instead."""
import re
import shutil
import subprocess
from pathlib import Path
import os

import pytest

ROOT = Path(__file__).resolve().parents[1]
TLS = re.compile(r"\b(__thread|_Thread_local|thread_local)\b")


def test_no_thread_local_in_port_sources():
    found = []
    for p in (ROOT / "port" / "src").rglob("*"):
        if p.suffix not in (".c", ".h") or "third_party" in p.parts:
            continue
        for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("//")[0]
            if TLS.search(code):
                found.append(f"{p.relative_to(ROOT)}:{n}")
    assert not found, found


ELF = Path(os.environ.get("SH3_VITA_ELF", os.path.expanduser("~/tools/build/sh3vita/sh3")))
NM = shutil.which("arm-vita-eabi-nm")


@pytest.mark.skipif(NM is None or not ELF.exists(), reason="Vita ELF or arm-vita-eabi-nm missing")
def test_no_emulated_tls_variable_in_the_vita_build():
    syms = subprocess.run([NM, str(ELF)], capture_output=True, text=True, check=True).stdout
    assert not [l for l in syms.splitlines() if "__emutls_v." in l]
