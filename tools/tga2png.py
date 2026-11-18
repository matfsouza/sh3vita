"""Converte um TGA 24/32 bits sem compressão (como os de SH3_SHOT) em PNG, sem bibliotecas externas.

Uso: python tools/tga2png.py entrada.tga saida.png
"""
import struct
import sys
import zlib


def tga_to_png(src, dst):
    d = open(src, "rb").read()
    idlen, _, kind = d[0], d[1], d[2]
    w, h, bpp, desc = struct.unpack_from("<HHBB", d, 12)
    if kind != 2 or bpp not in (24, 32):
        raise SystemExit(f"TGA não suportado: tipo {kind}, {bpp} bits")
    px, step = d[18 + idlen:], bpp // 8
    rows = []
    for y in range(h):
        sy = y if desc & 0x20 else h - 1 - y   # bit 5: origin at the top
        line = px[sy * w * step:(sy + 1) * w * step]
        rgb = bytearray()
        for x in range(w):
            b, g, r = line[x * step:x * step + 3]
            rgb += bytes((r, g, b))
        rows.append(b"\0" + bytes(rgb))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)


if __name__ == "__main__":
    tga_to_png(sys.argv[1], sys.argv[2])
