"""Reads a Vita crash dump (ux0:data/psp2core-*.psp2dmp) of the port. Usage:
    .venv/Scripts/python tools/vita_core.py <dump> <unstripped ELF of that build> <ref>
<ref>: the runtime address of rt_call, as printed by the log of that run ("ref F_00554100=0x...", SH3_PROFILE builds)
or computed from another known address; it gives the load offset of the executable.
Prints every thread (name, state, pc/lr/sp), the crashed thread's registers, its function (arm-vita-eabi-addr2line)
and the game call chain found on the guest stack (return addresses into sh3.exe .text)."""
import gzip
import os
import struct
import subprocess
import sys

STOP_REASONS = {0x30002: "undefined instruction", 0x30003: "prefetch abort", 0x30004: "data abort"}


def load(path):
    raw = open(path, "rb").read()
    data = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
    phoff = struct.unpack_from("<I", data, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    notes, segs = {}, []
    for i in range(phnum):
        typ, off, va, _, fsz, _, _, _ = struct.unpack_from("<8I", data, phoff + i * phentsize)
        if typ == 1:
            segs.append((va, off, fsz))
        elif typ == 4:
            p, end = off, off + fsz
            while p < end:
                nsz, dsz, _ = struct.unpack_from("<3I", data, p)
                name = data[p + 12:p + 12 + nsz].rstrip(b"\0").decode()
                q = p + 12 + ((nsz + 3) & ~3)
                notes[name] = data[q:q + dsz]
                p = q + ((dsz + 3) & ~3)
    return data, notes, segs


def threads(notes):
    ti, tr = notes["THREAD_INFO"], notes["THREAD_REG_INFO"]
    info, off = {}, 8
    for _ in range(struct.unpack_from("<I", ti, 4)[0]):
        size = struct.unpack_from("<I", ti, off)[0]
        rec = ti[off:off + size]
        words = struct.unpack_from("<%dI" % (size // 4), rec)
        uid = words[1]
        stop = next((w for w in words[10:] if w in STOP_REASONS), 0)
        info[uid] = {"name": rec[8:40].split(b"\0")[0].decode(errors="replace"), "status": words[12], "stop": stop}
        off += size
    off = 8
    for _ in range(struct.unpack_from("<I", tr, 4)[0]):
        size = struct.unpack_from("<I", tr, off)[0]
        rec = tr[off:off + size]
        uid = struct.unpack_from("<I", rec, 4)[0]
        info.setdefault(uid, {"name": hex(uid), "status": 0, "stop": 0})["regs"] = struct.unpack_from("<16I", rec, 8)
        off += size
    return info


def read(data, segs, addr, n):
    for va, off, fsz in segs:
        if va <= addr and addr + n <= va + fsz:
            return data[off + addr - va:off + addr - va + n]
    return None


def main(dump, elf, ref):
    data, notes, segs = load(dump)
    nm = subprocess.run(["arm-vita-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    rt_call = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(" rt_call"))
    delta = (int(ref, 16) & ~1) - rt_call
    print(f"load offset {delta:#x}")
    crashed = None
    for uid, t in threads(notes).items():
        r = t.get("regs", (0,) * 16)
        mark = f"  <== {STOP_REASONS[t['stop']]}" if t["stop"] else ""
        print(f"{t['name']:28s} pc={r[15]:08x} lr={r[14]:08x} sp={r[13]:08x}{mark}")
        if t["stop"]:
            crashed = t
    if not crashed:
        return
    r = crashed["regs"]
    print("registers:", " ".join(f"r{i}={v:08x}" for i, v in enumerate(r)))
    link = r[15] - delta
    out = subprocess.run(["arm-vita-eabi-addr2line", "-f", "-e", elf, f"{link:x}"], capture_output=True, text=True).stdout
    print(f"function: {out.split()[0] if out else '?'} (link {link:08x})")
    # Guest memory base and esp: CPU struct pointer in r4 is common in generated code; the guest stack is found by
    # scanning dumped segments for words that are return addresses into sh3.exe (.text 0x401000-0x68a000).
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "recomp"))
    import pe as P
    exe = P.PE(os.path.join(os.path.dirname(__file__), "..", "game", "sh3.exe"))
    gmem = int(os.environ.get("SH3_GMEM", "0"), 16)
    esp = int(os.environ.get("SH3_ESP", "0"), 16)
    if gmem and esp:
        print("guest call chain (return addresses above esp):")
        for a in range(esp - 0x40, esp + 0x600, 4):
            b = read(data, segs, gmem + a, 4)
            if b is None:
                continue
            v = struct.unpack("<I", b)[0]
            o = exe.off(v - 5) if 0x401000 <= v < 0x68a000 else None
            if o is not None and exe.data[o] == 0xE8:
                print(f"  [esp{a - esp:+#06x}] {v:08x}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
