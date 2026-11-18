"""Report of the PC sampling profile (SH3_PROF=start,end writes prof.txt next to sh3.exe, src/pc/prof.c). Usage:
    .venv/Scripts/python tools/prof_report.py build/pc2/sh3.exe build/pc2/prof.txt [top]
Each sample is the game thread's instruction pointer; it is matched to the function symbol of the PC build (nm) and
counted: F_xxxxxxxx are recompiled game functions (xxxxxxxx = address in sh3.exe), the rest is the port (D3D, Win32,
x87 helpers...)."""
import bisect
import collections
import subprocess
import sys


def symbols(exe):
    out = subprocess.run(["nm", "-n", "--defined-only", exe], capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].lower() == "t":
            addrs.append(int(parts[0], 16))
            names.append(parts[2])
    return addrs, names


def report(exe, samples_path, top=40):
    addrs, names = symbols(exe)
    counts = collections.Counter()
    total = 0
    for line in open(samples_path):
        line = line.strip()
        if not line:
            continue
        a = int(line, 16)
        i = bisect.bisect_right(addrs, a) - 1
        counts[names[i] if i >= 0 else "?"] += 1
        total += 1
    game = sum(n for name, n in counts.items() if name.startswith("F_"))
    lines = [f"{total} samples; recompiled game code {100 * game / max(total, 1):.1f}%"]
    for name, n in counts.most_common(top):
        lines.append(f"{100 * n / total:5.1f}%  {n:6d}  {name}")
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    print(report(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 40))
