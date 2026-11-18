"""Summary of a sh3log.txt from the real Vita. Usage:
    .venv/Scripts/python tools/vita_log_summary.py sh3log.txt
Memory obtained, frames presented, [perf] frame times (only valid on the real Vita at 444 MHz), movie decoding cost
and the first FATAL with the lines after it (ret? stack)."""
import re
import sys


def summarize(text):
    lines = text.splitlines()
    s = {"memory_mb": None, "presents": 0, "ms_per_frame": [], "slow_frames": (0, 0), "longest_ms": 0.0,
         "movie_decode_ms_per_s": [], "fatal": None}
    slow = total = 0
    for i, line in enumerate(lines):
        if m := re.match(r"\[mem\] guest memory: (\d+) MB obtained", line):
            s["memory_mb"] = int(m.group(1))
        elif m := re.match(r"\[stats\] presents=(\d+)", line):
            s["presents"] = max(s["presents"], int(m.group(1)))
        elif m := re.match(r"\[perf\] ([\d.]+) ms/frame", line):
            s["ms_per_frame"].append(float(m.group(1)))
        elif m := re.match(r"\[perf\] frames > 36 ms: (\d+) of ([\d.]+), longest ([\d.]+) ms", line):
            slow += int(m.group(1))
            total += int(float(m.group(2)))
            s["longest_ms"] = max(s["longest_ms"], float(m.group(3)))
        elif m := re.match(r"\[movie\] t=[\d.]+ s: .*decode ([\d.]+) ms", line):
            s["movie_decode_ms_per_s"].append(float(m.group(1)))
        elif line.startswith("FATAL") and s["fatal"] is None:
            s["fatal"] = "\n".join(lines[i:i + 12])
    s["slow_frames"] = (slow, total)
    return s


def report(s):
    out = [f"Memória: {s['memory_mb']} MB" if s["memory_mb"] else "Memória: (não registrada)",
           f"Quadros apresentados: {s['presents']}"]
    if s["ms_per_frame"]:
        avg = sum(s["ms_per_frame"]) / len(s["ms_per_frame"])
        out.append(f"Tempo por quadro: média {avg:.1f} ms ({1000 / avg:.1f} FPS), "
                   f"{s['slow_frames'][0]} de {s['slow_frames'][1]} quadros acima de 36 ms, maior {s['longest_ms']:.1f} ms")
    if s["movie_decode_ms_per_s"]:
        d = s["movie_decode_ms_per_s"]
        out.append(f"Vídeo: decodificação média {sum(d) / len(d):.0f} ms por segundo de vídeo (máx. {max(d):.0f})")
    out.append(s["fatal"] if s["fatal"] else "Sem FATAL")
    return "\n".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    print(report(summarize(open(sys.argv[1], encoding="utf-8", errors="replace").read())))
